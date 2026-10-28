#include "ui/apps/ledger.h"

#include <cstdio>
#include <limits>

namespace ui {
namespace {
enum : WidgetId { kPage = 1, kDay, kPrice, kMinus, kPlus, kApply };
struct State {
    int page = 0;
    int day = 0;
    int target = 0;
    int32_t draft = 0;
    const core::World* owner = nullptr;
    const core::GameData* data = nullptr;
    const char* pages[2]{};
    const char* today = nullptr;
    const char* date_fmt = nullptr;
    const char* unavailable = nullptr;
    const char* empty = nullptr;
    const char* revenue = nullptr;
    const char* expenses = nullptr;
    const char* profit = nullptr;
    const char* visitors_fmt = nullptr;
    const char* balance_fmt = nullptr;
    const char* history = nullptr;
    const char* hour = nullptr;
    const char* night = nullptr;
    const char* minus = nullptr;
    const char* plus = nullptr;
    const char* apply = nullptr;
    const char* applied = nullptr;
    const char* failed = nullptr;
    const char* tariff_hint = nullptr;
    const char* status = nullptr;
} g_state;

LedgerPoint totals(const core::DayStats& day) {
    LedgerPoint p{};
    p.date = day.date;
    for (const int32_t value : day.revenue) p.revenue += value;
    for (const int32_t value : day.expenses) p.expenses += value;
    p.profit = p.revenue - p.expenses;
    return p;
}

const char* page_text(int i, char*, int, void* user) { return static_cast<State*>(user)->pages[i]; }

const char* day_text(int i, char* buf, int cap, void* user) {
    const Context& ctx = *static_cast<Context*>(user);
    if (i == 0) return g_state.today;
    const core::DayStats* day = core::stats_days_ago(*ctx.world, i - 1);
    if (!day) return g_state.empty;
    const core::Ymd ymd = core::ymd_from_date(day->date);
    std::snprintf(buf, static_cast<size_t>(cap), g_state.date_fmt, ymd.day, ymd.month, ymd.year);
    return buf;
}

const char* target_text(int i, char*, int, void* user) {
    const core::GameData& d = *static_cast<Context*>(user)->world->data;
    if (i == 0) return g_state.hour;
    if (i == 1) return g_state.night;
    return core::data_string(d, d.goods[i - 2].name);
}

int32_t current_price(const core::World& w, int target) {
    if (target == 0) return w.economy.price_hour;
    if (target == 1) return w.economy.price_night;
    return w.economy.goods_price[target - 2];
}

void number_label(Context& ctx, Rect rect, int64_t value) {
    char buf[48];
    if (value >= std::numeric_limits<int32_t>::min() && value <= std::numeric_limits<int32_t>::max())
        format_money(static_cast<int32_t>(value), buf, sizeof(buf));
    else std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(value));
    label(ctx, rect, buf, value < 0 ? kColBad : kColText, Font::kUi12, Align::kRight);
}

void draw_history(Context& ctx, const State& s) {
    LedgerPoint points[core::kStatsDays];
    const int count = ledger_history(*ctx.world, points);
    label(ctx, {4, 108, 468, 16}, s.history, kColAccent);
    if (count == 0) { label(ctx, {4, 142, 468, 28}, s.empty, kColTextDim); return; }
    int64_t low = 0, high = 0;
    for (int i = 0; i < count; ++i) {
        if (points[i].profit < low) low = points[i].profit;
        if (points[i].profit > high) high = points[i].profit;
    }
    const float span = high == low ? 1.0f : static_cast<float>(high - low);
    constexpr int kTop = 146, kHeight = 66, kLeft = 84, kWidth = 384;
    const int zero = kTop + static_cast<int>(static_cast<float>(high) / span * kHeight);
    fill(ctx, {kLeft, kTop, kWidth, kHeight + 1}, kColField);
    fill(ctx, {kLeft, static_cast<int16_t>(zero), kWidth, 1}, kColShadow);
    number_label(ctx, {4, kTop - 5, 76, 14}, high);
    number_label(ctx, {4, kTop + kHeight - 9, 76, 14}, low);
    for (int i = 0; i < count; ++i) {
        const int y = kTop + static_cast<int>(static_cast<float>(high - points[i].profit) / span * kHeight);
        const int top = y < zero ? y : zero;
        const int height = y == zero ? 1 : (y < zero ? zero - y : y - zero);
        const int x = kLeft + i * kWidth / count;
        fill(ctx, {static_cast<int16_t>(x), static_cast<int16_t>(top),
                   static_cast<int16_t>(kWidth / count - 2), static_cast<int16_t>(height)},
             points[i].profit < 0 ? kColBad : kColGood);
    }
    char buf[48];
    const core::Ymd first = core::ymd_from_date(points[0].date);
    std::snprintf(buf, sizeof(buf), s.date_fmt, first.day, first.month, first.year);
    label(ctx, {84, 216, 150, 16}, buf, kColTextDim);
    const core::Ymd last = core::ymd_from_date(points[count - 1].date);
    std::snprintf(buf, sizeof(buf), s.date_fmt, last.day, last.month, last.year);
    label(ctx, {316, 216, 152, 16}, buf, kColTextDim, Font::kUi12, Align::kRight);
}

void on_open(Context&, void* user) {
    State& s = *static_cast<State*>(user);
    s = State{};
    s.pages[0] = str("os.ledger.summary");
    s.pages[1] = str("os.ledger.prices");
    s.today = str("os.ledger.today");
    s.date_fmt = str("os.ledger.date");
    s.unavailable = str("os.ledger.unavailable");
    s.empty = str("os.ledger.empty");
    s.revenue = str("os.ledger.revenue");
    s.expenses = str("os.ledger.expenses");
    s.profit = str("os.ledger.profit");
    s.visitors_fmt = str("os.ledger.visitors");
    s.balance_fmt = str("os.ledger.balance");
    s.history = str("os.ledger.history");
    s.hour = str("os.ledger.hour");
    s.night = str("os.ledger.night");
    s.minus = str("os.ledger.minus");
    s.plus = str("os.ledger.plus");
    s.apply = str("os.ledger.apply");
    s.applied = str("os.ledger.applied");
    s.failed = str("os.ledger.failed");
    s.tariff_hint = str("os.ledger.tariff_hint");
}

void on_frame(Context& ctx, void* user) {
    State& s = *static_cast<State*>(user);
    if (!ctx.world || !ctx.world->data) { label(ctx, {4, 4, 468, 28}, s.unavailable); return; }
    if (s.owner != ctx.world || s.data != ctx.world->data) {
        s.target = 0;
        s.day = 0;
        s.draft = current_price(*ctx.world, s.target);
        s.status = nullptr;
    }
    s.owner = ctx.world;
    s.data = ctx.world->data;
    choice(ctx, kPage, {4, 4, 468, 16}, &s.page, 2, page_text, &s);
    if (s.page == 1) {
        if (choice(ctx, kPrice, {4, 30, 468, 16}, &s.target, 2 + s.data->goods_count, target_text, &ctx)) {
            s.draft = current_price(*ctx.world, s.target);
            s.status = nullptr;
        }
        if (button(ctx, kMinus, {4, 58, 112, 16}, s.minus, s.draft <= 1 ? kWidgetDisabled : 0)) --s.draft;
        if (button(ctx, kPlus, {356, 58, 116, 16}, s.plus,
                   s.draft == std::numeric_limits<int32_t>::max() ? kWidgetDisabled : 0)) ++s.draft;
        number_label(ctx, {124, 58, 224, 16}, s.draft);
        if (button(ctx, kApply, {4, 90, 468, 16}, s.apply)) {
            core::CmdResult result;
            if (s.target == 0) result = core::cmd_set_price_hour(*ctx.world, s.draft);
            else if (s.target == 1) result = core::cmd_set_price_night(*ctx.world, s.draft);
            else result = core::cmd_set_goods_price(*ctx.world, static_cast<core::Index>(s.target - 2), s.draft);
            s.status = result == core::CmdResult::kOk ? s.applied : s.failed;
        }
        label(ctx, {4, 122, 468, 32}, s.tariff_hint, kColTextDim);
        if (s.status) label(ctx, {4, 174, 468, 32}, s.status);
        return;
    }
    if (s.day > ctx.world->stats.count) s.day = 0;
    choice(ctx, kDay, {4, 26, 468, 16}, &s.day, ctx.world->stats.count + 1, day_text, &ctx);
    const core::DayStats* day = s.day == 0 ? &ctx.world->stats.today : core::stats_days_ago(*ctx.world, s.day - 1);
    if (!day) return;
    const LedgerPoint p = totals(*day);
    label(ctx, {4, 46, 150, 14}, s.revenue);
    label(ctx, {162, 46, 150, 14}, s.expenses);
    label(ctx, {320, 46, 152, 14}, s.profit);
    number_label(ctx, {4, 62, 150, 16}, p.revenue);
    number_label(ctx, {162, 62, 150, 16}, p.expenses);
    number_label(ctx, {320, 62, 152, 16}, p.profit);
    char buf[128], money[32], debt[32];
    std::snprintf(buf, sizeof(buf), s.visitors_fmt, day->arrived, day->served);
    label(ctx, {4, 84, 236, 16}, buf);
    format_money(s.day == 0 ? ctx.world->economy.money : day->money_end, money, sizeof(money));
    format_money(s.day == 0 ? ctx.world->economy.debt : day->debt_end, debt, sizeof(debt));
    std::snprintf(buf, sizeof(buf), s.balance_fmt, money, debt);
    label(ctx, {244, 84, 228, 16}, buf);
    draw_history(ctx, s);
}
}  // namespace

int ledger_history(const core::World& world, LedgerPoint (&out)[core::kStatsDays]) {
    const int count = world.stats.count;
    for (int i = 0; i < count; ++i) out[i] = totals(*core::stats_days_ago(world, count - 1 - i));
    for (int i = count; i < core::kStatsDays; ++i) out[i] = LedgerPoint{};
    return count;
}

AppId ledger_register() {
    AppDesc d{};
    d.id = "ledger";
    d.title_key = "os.ledger.title";
    d.icon_text = "os.ledger.icon";
    d.icon_color = kColAccent;
    d.flags = kAppOnDesktop | kAppMaximized;
    d.on_open = on_open;
    d.on_frame = on_frame;
    d.user = &g_state;
    return register_app(d);
}
}  // namespace ui
