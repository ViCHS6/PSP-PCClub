#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "ui/apps/shop.h"
#include "ui/apps/mail.h"
#include "ui/apps/ledger.h"
#include "ui/apps/records.h"

namespace {

// Виджеты заменены вводом по id и записью результата; команды и запросы core настоящие.
ui::AppDesc g_app;
ui::WidgetId g_click = 0, g_select_widget = 0;
int g_select = -1;
bool g_activate = false;
uint32_t g_pressed = 0;
struct Text { ui::Rect rect; std::string value; };
struct Filled { ui::Rect rect; ui::Color color; };
struct Rows { ui::WidgetId id; std::vector<std::vector<std::string>> cells; };
std::vector<Text> g_labels;
std::vector<Filled> g_fills;
std::vector<Rows> g_rows;
std::vector<ui::WidgetId> g_disabled;

bool has_text(const char* text) {
    for (const Text& label : g_labels) if (label.value.find(text) != std::string::npos) return true;
    for (const Rows& rows : g_rows)
        for (const auto& row : rows.cells)
            for (const auto& cell : row) if (cell.find(text) != std::string::npos) return true;
    return false;
}

const Rows& rows_of(ui::WidgetId id) {
    for (const Rows& rows : g_rows) if (rows.id == id) return rows;
    static const Rows kEmpty{};
    return kEmpty;
}

struct Fixture {
    core::HardwareRec hardware[2]{};
    core::GameRec games[3]{};
    core::GoodsRec goods[1]{};
    core::EventRec events[1]{};
    core::ChoiceRec choices[2]{};
    core::CondRec conds[1]{};
    core::EffectRec effects[1]{};
    core::MinigameRec minigames[2]{};
    core::RegularRec regulars[1]{};
    core::ArchetypeRec archetypes[1]{};
    core::EconomyRec economy{};
    core::GameData data{};
    core::World world{};
    std::vector<uint8_t> strings;

    Fixture() {
        const char* texts[] = {"hardware", "future hardware", "new game", "old game", "future game",
                               "snack", "event body: choose carefully", "accept", "locked", "snake",
                               "mines", "regular", "student"};
        constexpr size_t kCount = sizeof(texts) / sizeof(texts[0]);
        strings.resize((kCount + 1) * 4, 0);
        for (size_t i = 0; i < kCount; ++i) {
            const auto offset = static_cast<uint32_t>(strings.size());
            std::memcpy(strings.data() + i * 4, &offset, 4);
            strings.insert(strings.end(), texts[i], texts[i] + std::strlen(texts[i]) + 1);
        }
        const auto end = static_cast<uint32_t>(strings.size());
        std::memcpy(strings.data() + kCount * 4, &end, 4);
        data.strings = strings.data();
        data.string_count = static_cast<uint16_t>(kCount);
        data.hardware = hardware; data.hardware_count = 2;
        data.games = games; data.game_count = 3;
        data.goods = goods; data.goods_count = 1;
        data.events = events; data.event_count = 1;
        data.choices = choices; data.choice_count = 2;
        data.conds = conds; data.cond_count = 1;
        data.effects = effects; data.effect_count = 1;
        data.minigames = minigames; data.minigame_count = 2;
        data.regulars = regulars; data.regular_count = 1;
        data.archetypes = archetypes; data.archetype_count = 1;
        data.economy = &economy;
        world.data = &data;
        world.clock.date = core::date_from_ymd(2001, 9, 10);
        world.clock.phase = core::Phase::kMorning;
        world.economy.money = 5000;
        world.economy.price_hour = 10;
        world.economy.price_night = 50;
        world.economy.goods_price[0] = 12;
        hardware[0].name = 0; hardware[0].base_price = 100;
        hardware[0].release = world.clock.date - 40;
        hardware[1].name = 1; hardware[1].release = world.clock.date + 1;
        games[0].title = 2; games[0].release = world.clock.date - 1;
        games[1].title = 3; games[1].release = world.clock.date - 7;
        games[2].title = 4; games[2].release = world.clock.date + 1;
        for (auto& game : games) { game.price_license = 200; game.price_pirate = 25; }
        goods[0].name = 5; goods[0].buy_price = 5;
        events[0].present_text = 6; events[0].choice_count = 2;
        choices[0].text = 7; choices[0].effect_count = 1;
        choices[1].text = 8; choices[1].req_count = 1;
        effects[0].type = core::kEffMoney; effects[0].ival = 42;
        conds[0] = {core::kVarMoney, core::kOpGe, core::kNone, 99999.0f};
        minigames[0].name = 9; minigames[1].name = 10; minigames[1].invert = 1;
        regulars[0].name = 11; archetypes[0].name = 12;
        for (auto& modifier : world.modifiers) modifier.target = core::kModTargetCount;
    }

    ui::Context context() {
        ui::Context ctx{};
        ctx.app = 0; ctx.client = {2, 16, ui::kMaxClientWidth, ui::kMaxClientHeight};
        ctx.is_active = true; ctx.world = &world; ctx.data = &data;
        return ctx;
    }

    void mail(core::MailKind kind, core::DayNum date, int32_t amount = 0) {
        auto& box = world.mailbox;
        core::Mail& m = box.mail[box.count++];
        m.kind = kind; m.date = date; m.amount = amount;
        ++box.unread;
    }

    void replace_text(core::StrId id, const std::string& text) {
        const auto offset = static_cast<uint32_t>(strings.size());
        std::memcpy(strings.data() + id * 4, &offset, 4);
        strings.insert(strings.end(), text.begin(), text.end());
        strings.push_back(0);
        data.strings = strings.data();
    }
};

void frame(ui::Context& ctx, ui::WidgetId click = 0, ui::WidgetId select_widget = 0,
           int selected = -1, bool activate = false, uint32_t pressed = 0) {
    g_click = click; g_select_widget = select_widget; g_select = selected;
    g_activate = activate; g_pressed = pressed;
    g_labels.clear(); g_fills.clear(); g_rows.clear(); g_disabled.clear();
    g_app.on_frame(ctx, g_app.user);
    for (const Text& label : g_labels) {
        CHECK(label.rect.x >= 0); CHECK(label.rect.y >= 0);
        CHECK(label.rect.x + label.rect.w <= ctx.client.w);
        CHECK(label.rect.y + label.rect.h <= ctx.client.h);
    }
    for (const Filled& fill : g_fills) {
        CHECK(fill.rect.h > 0); CHECK(fill.rect.w > 0);
        CHECK(fill.rect.x >= 0); CHECK(fill.rect.y >= 0);
        CHECK(fill.rect.x + fill.rect.w <= ctx.client.w);
        CHECK(fill.rect.y + fill.rect.h <= ctx.client.h);
    }
}

void start(ui::AppId (*register_app)(), ui::Context& ctx) {
    REQUIRE(register_app() != ui::kNoApp);
    REQUIRE(g_app.on_open != nullptr);
    CHECK((g_app.flags & ui::kAppMaximized) != 0);
    CHECK(std::strncmp(g_app.title_key, "os.", 3) == 0);
    g_app.on_open(ctx, g_app.user);
    frame(ctx);
}
}  // namespace

namespace ui {
AppId register_app(const AppDesc& desc) { g_app = desc; return 0; }
const char* str(const char* key) {
    struct Pair { const char* key; const char* text; };
    static const Pair kFormats[] = {
        {"os.shop.row", "%d x %s: %s"}, {"os.shop.total", "total %s"},
        {"os.mail.row", "%s %02d.%02d %s"}, {"os.mail.locked", "locked: %s"},
        {"os.mail.report_body", "profit %s"}, {"os.mail.bill_body", "paid %s"},
        {"os.mail.date", "%02d.%02d.%04d %s"}, {"os.ledger.date", "%02d.%02d.%04d"},
        {"os.ledger.visitors", "arrived %d served %d"}, {"os.ledger.balance", "cash %s debt %s"},
        {"os.records.date", "%02d.%02d.%04d"}};
    for (const Pair& p : kFormats) if (std::strcmp(p.key, key) == 0) return p.text;
    return key;
}
void label(Context&, Rect r, const char* text, Color, Font, Align) { g_labels.push_back({r, text ? text : ""}); }
void fill(Context&, Rect r, Color color) { g_fills.push_back({r, color}); }
int text_width(const char* text, Font) {
    int n = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p)
        if ((*p & 0xC0) != 0x80) ++n;
    return n * 6;
}
void set_focus(Context&, WidgetId) {}
uint32_t consume(Context& ctx, uint32_t mask) {
    const uint32_t result = ctx.is_active ? g_pressed & mask : 0;
    g_pressed &= ~result;
    return result;
}
bool button(Context& ctx, WidgetId id, Rect r, const char* text, uint8_t flags) {
    g_labels.push_back({r, text});
    if (flags & kWidgetDisabled) { g_disabled.push_back(id); return false; }
    return ctx.is_active && g_click == id;
}
bool choice(Context& ctx, WidgetId id, Rect, int* selected, int count, ItemTextFn text, void* user, uint8_t flags) {
    bool changed = false;
    if (ctx.is_active && !(flags & kWidgetDisabled) && g_select_widget == id && g_select >= 0 && g_select < count) {
        changed = *selected != g_select;
        *selected = g_select;
    }
    Rows rows{id, {}};
    for (int i = 0; i < count; ++i) { char buf[96]{}; rows.cells.push_back({text(i, buf, sizeof(buf), user)}); }
    g_rows.push_back(rows);
    return changed;
}
ListEvent table(Context& ctx, WidgetId id, Rect, ListState& state, const Column*, int columns, int count,
                CellTextFn text, void* user) {
    if (count == 0) state.selected = -1;
    else if (state.selected >= count) state.selected = static_cast<int16_t>(count - 1);
    if (ctx.is_active && g_select_widget == id && g_select >= 0 && g_select < count)
        state.selected = static_cast<int16_t>(g_select);
    Rows rows{id, {}};
    for (int i = 0; i < count; ++i) {
        std::vector<std::string> row;
        for (int col = 0; col < columns; ++col) { char buf[96]{}; row.emplace_back(text(i, col, buf, sizeof(buf), user)); }
        rows.cells.push_back(row);
    }
    g_rows.push_back(rows);
    return ctx.is_active && g_select_widget == id && g_activate ? ListEvent::kActivated : ListEvent::kNone;
}
ListEvent list(Context& ctx, WidgetId id, Rect r, ListState& state, int count, ItemTextFn text, void* user) {
    struct Adapter { ItemTextFn fn; void* user; } adapter{text, user};
    return table(ctx, id, r, state, nullptr, 1, count,
        [](int row, int, char* buf, int cap, void* u) {
            const auto& a = *static_cast<Adapter*>(u);
            return a.fn(row, buf, cap, a.user);
        }, &adapter);
}
}  // namespace ui

TEST_CASE("T032 apps tolerate missing world and data") {
    Fixture f; auto ctx = f.context();
    const std::array<ui::AppId (*)(), 4> apps = {ui::shop_register, ui::mail_register,
                                               ui::ledger_register, ui::records_register};
    for (auto app : apps) {
        ctx.world = nullptr; start(app, ctx);
        CHECK(has_text("unavailable"));
        ctx.world = &f.world; f.world.data = nullptr; frame(ctx);
        CHECK(has_text("unavailable"));
        f.world.data = &f.data;
    }
}

TEST_CASE("T032 shop purchases a merged basket and delivery is next morning") {
    Fixture f; auto ctx = f.context(); start(ui::shop_register, ctx);
    CHECK(rows_of(2).cells.size() == 1);
    CHECK(has_text("os.shop.empty"));
    const int32_t price = core::current_hardware_price(f.world, 0);
    frame(ctx, 0, 2, 0, true); frame(ctx, 3);
    REQUIRE(rows_of(4).cells.size() == 1);
    CHECK(f.world.economy.money == 5000);
    frame(ctx, 6);
    CHECK(f.world.economy.money == 5000 - 2 * price);
    CHECK(f.world.inventory.order_count == 1);
    CHECK(f.world.inventory.orders[0].count == 2);
    CHECK(f.world.inventory.orders[0].arrives == f.world.clock.date + 1);
    CHECK(f.world.inventory.hardware[0] == 0);
    CHECK(f.world.stats.today.expenses[core::kExpPurchases] == 2 * price);
    frame(ctx);
    CHECK(rows_of(4).cells.empty());
    frame(ctx, 6);
    CHECK(f.world.inventory.order_count == 1);
}

TEST_CASE("T032 shop orders licenses pirate disc and buffet without unreleased games") {
    Fixture f; auto ctx = f.context(); start(ui::shop_register, ctx);
    frame(ctx, 0, 1, 1); CHECK(rows_of(2).cells.size() == 2);
    frame(ctx, 0, 2, 0, true);
    frame(ctx, 0, 1, 2); frame(ctx, 0, 2, 1, true);
    frame(ctx, 0, 1, 3); frame(ctx, 0, 2, 0, true);
    frame(ctx, 6);
    REQUIRE(f.world.inventory.order_count == 3);
    CHECK(f.world.inventory.orders[0].kind == core::OrderKind::kLicense);
    CHECK(f.world.inventory.orders[1].kind == core::OrderKind::kPirate);
    CHECK(f.world.inventory.orders[2].kind == core::OrderKind::kGoods);
    CHECK(f.world.economy.money == 5000 - 200 - 25 - 5);
}

TEST_CASE("T032 shop preserves basket on no money order limit or game over") {
    Fixture f; auto ctx = f.context(); start(ui::shop_register, ctx);
    frame(ctx, 0, 2, 0, true);
    SUBCASE("no money") {
        f.world.economy.money = 0; frame(ctx, 6);
        CHECK(has_text("os.shop.no_money"));
    }
    SUBCASE("limit") {
        f.world.inventory.order_count = core::kMaxOrders; frame(ctx, 6);
        CHECK(has_text("os.shop.limit"));
    }
    SUBCASE("phase") {
        f.world.clock.phase = core::Phase::kGameOver; frame(ctx, 6);
        CHECK(has_text("os.shop.wrong_phase"));
    }
    frame(ctx);
    CHECK(rows_of(4).cells.size() == 1);
    CHECK(f.world.stats.today.expenses[core::kExpPurchases] == 0);
}

TEST_CASE("T032 shop retains only unsent rows after partial checkout") {
    Fixture f; auto ctx = f.context(); start(ui::shop_register, ctx);
    frame(ctx, 0, 2, 0, true);
    frame(ctx, 0, 1, 2); frame(ctx, 0, 2, 0, true);
    f.world.library.pirate[0] = 1;
    frame(ctx, 6);
    CHECK(has_text("os.shop.invalid"));
    const int32_t charged = f.world.economy.money;
    REQUIRE(f.world.inventory.order_count == 1);
    frame(ctx); REQUIRE(rows_of(4).cells.size() == 1);
    frame(ctx, 6);
    CHECK(f.world.economy.money == charged);
    CHECK(f.world.inventory.order_count == 1);
    frame(ctx, 0, 4, 0); frame(ctx, 5); frame(ctx);
    CHECK(rows_of(4).cells.empty());
}

TEST_CASE("T032 mail marks opened event read and chooses only available answers") {
    Fixture f; f.mail(core::MailKind::kEvent, f.world.clock.date);
    f.world.mailbox.mail[0].ref = 0;
    f.world.events.active[0].event = 0; f.world.events.active[0].mail = 0;
    auto ctx = f.context(); start(ui::mail_register, ctx);
    CHECK(f.world.mailbox.unread == 1);
    frame(ctx, 0, 1, 0, true);
    CHECK(f.world.mailbox.unread == 0); CHECK(f.world.mailbox.mail[0].read == 1);
    frame(ctx);
    CHECK(has_text("event body: choose carefully"));
    frame(ctx, 0, 4, 1); frame(ctx, 5);
    CHECK(f.world.mailbox.mail[0].answered == 0);
    CHECK(std::find(g_disabled.begin(), g_disabled.end(), 5) != g_disabled.end());
    frame(ctx, 0, 4, 0); frame(ctx, 5);
    CHECK(f.world.mailbox.mail[0].answered == 1);
    CHECK(f.world.mailbox.mail[0].choice == 0);
    CHECK(f.world.events.active[0].event == core::kNone);
    CHECK(f.world.economy.money == 5042);
    frame(ctx, 5); CHECK(f.world.economy.money == 5042);
    CHECK(has_text("accept"));
    frame(ctx, 0, 0, -1, false, platform::kButtonCircle);
    CHECK(g_pressed == 0);
    frame(ctx, 0, 1, 0, true);
    CHECK(f.world.mailbox.unread == 0);
}

TEST_CASE("T032 mail newsletter uses letter date and bills never debit again") {
    Fixture f; f.mail(core::MailKind::kNewGames, f.world.clock.date);
    f.mail(core::MailKind::kBill, f.world.clock.date, 75);
    f.mail(core::MailKind::kReport, f.world.clock.date, -15);
    f.mail(core::MailKind::kDelivery, f.world.clock.date);
    auto ctx = f.context(); start(ui::mail_register, ctx);
    frame(ctx, 0, 1, 0, true); frame(ctx);
    CHECK(has_text("new game")); CHECK_FALSE(has_text("old game")); CHECK_FALSE(has_text("future game"));
    f.world.clock.date += 40; frame(ctx);
    CHECK(has_text("new game")); CHECK_FALSE(has_text("future game"));
    frame(ctx, 6); frame(ctx, 0, 1, 1, true); frame(ctx);
    CHECK(has_text("paid 75")); CHECK(f.world.economy.money == 5000);
    frame(ctx, 6); frame(ctx, 0, 1, 2, true); frame(ctx); CHECK(has_text("profit -15"));
    frame(ctx, 6); frame(ctx, 0, 1, 3, true); frame(ctx); CHECK(has_text("os.mail.delivery_body"));
}

TEST_CASE("T032 mail empty expired and evicted letters are safe") {
    Fixture f; auto ctx = f.context(); start(ui::mail_register, ctx);
    CHECK(has_text("os.mail.empty")); frame(ctx, 2);
    f.mail(core::MailKind::kEvent, f.world.clock.date); f.world.mailbox.mail[0].ref = 0;
    frame(ctx, 0, 1, 0, true); frame(ctx); CHECK(has_text("os.mail.expired"));
    f.world.mailbox.mail[0].kind = core::MailKind::kBill;
    frame(ctx); CHECK_FALSE(has_text("os.mail.expired")); CHECK(rows_of(1).cells.size() == 1);
}

TEST_CASE("T032 mail wrapping preserves UTF8 and newsletter is not limited by a text buffer") {
    Fixture f;
    std::string long_text;
    for (int i = 0; i < 220; ++i) long_text += "Длинное_письмо ";
    long_text += "КОНЕЦ";
    f.replace_text(6, long_text);
    f.mail(core::MailKind::kEvent, f.world.clock.date);
    f.world.mailbox.mail[0].ref = 0;
    auto ctx = f.context(); start(ui::mail_register, ctx);
    frame(ctx, 0, 1, 0, true); frame(ctx);
    const auto& lines = rows_of(3).cells;
    REQUIRE(lines.size() > 9);
    CHECK(lines.back()[0].find("КОНЕЦ") != std::string::npos);
    std::string joined;
    for (const auto& row : lines) {
        joined += row[0];
        for (size_t i = 0; i < row[0].size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(row[0][i]);
            if (c < 0x80) continue;
            REQUIRE(c >= 0xC0);
            REQUIRE(i + 1 < row[0].size());
            CHECK((static_cast<unsigned char>(row[0][++i]) & 0xC0) == 0x80);
        }
    }
    long_text.erase(std::remove(long_text.begin(), long_text.end(), ' '), long_text.end());
    joined.erase(std::remove(joined.begin(), joined.end(), ' '), joined.end());
    CHECK(joined == long_text);
    f.replace_text(2, std::string(6000, 'x') + "NEWS_END");
    f.mail(core::MailKind::kNewGames, f.world.clock.date);
    frame(ctx, 6); frame(ctx, 0, 1, 1, true); frame(ctx);
    CHECK(has_text("NEWS_END"));
}

TEST_CASE("T032 ledger history unwraps ring and totals all categories without today") {
    Fixture f;
    f.world.stats.head = 7; f.world.stats.count = core::kStatsDays;
    for (int age = 0; age < core::kStatsDays; ++age) {
        auto& d = f.world.stats.days[(7 - 1 - age + core::kStatsDays) % core::kStatsDays];
        d.date = f.world.clock.date - age - 1;
        for (int i = 0; i < core::kRevenueCount; ++i) d.revenue[i] = 100 + age + i;
        for (int i = 0; i < core::kExpenseCount; ++i) d.expenses[i] = 10 + age + i;
    }
    f.world.stats.today.revenue[0] = 99999;
    ui::LedgerPoint points[core::kStatsDays];
    REQUIRE(ui::ledger_history(f.world, points) == core::kStatsDays);
    for (int i = 0; i < core::kStatsDays; ++i) {
        const int age = core::kStatsDays - i - 1;
        CHECK(points[i].date == f.world.clock.date - age - 1);
        CHECK(points[i].revenue == (100 + age) * core::kRevenueCount + 10);
        CHECK(points[i].expenses == (10 + age) * core::kExpenseCount + 28);
        CHECK(points[i].profit == points[i].revenue - points[i].expenses);
    }
}

TEST_CASE("T032 ledger graph renders positive negative and zero days from history") {
    Fixture f; f.world.stats.head = 3; f.world.stats.count = 3;
    f.world.stats.days[0].revenue[0] = 100;
    f.world.stats.days[1].expenses[0] = 100;
    f.world.stats.today.revenue[0] = 900;
    auto ctx = f.context(); start(ui::ledger_register, ctx);
    std::vector<Filled> bars;
    for (const Filled& fill : g_fills) if (fill.color == ui::kColGood || fill.color == ui::kColBad) bars.push_back(fill);
    REQUIRE(bars.size() == 3);
    CHECK(bars[0].color == ui::kColGood); CHECK(bars[0].rect.h == 33);
    CHECK(bars[1].color == ui::kColBad); CHECK(bars[1].rect.h == 33);
    CHECK(bars[2].rect.h == 1);
    CHECK(has_text("900"));
    frame(ctx, 0, 2, 2); CHECK(has_text("-100"));
}

TEST_CASE("T032 ledger history is empty before first day and sums avoid int32 overflow") {
    Fixture f;
    ui::LedgerPoint points[core::kStatsDays];
    CHECK(ui::ledger_history(f.world, points) == 0);
    for (const auto& point : points) CHECK(point.profit == 0);
    f.world.stats.count = 1; f.world.stats.head = 1;
    f.world.stats.days[0].revenue[0] = std::numeric_limits<int32_t>::max();
    f.world.stats.days[0].revenue[1] = std::numeric_limits<int32_t>::max();
    REQUIRE(ui::ledger_history(f.world, points) == 1);
    CHECK(points[0].profit == 2LL * std::numeric_limits<int32_t>::max());
    auto ctx = f.context(); start(ui::ledger_register, ctx);
    frame(ctx, 0, 2, 1);
    CHECK(has_text("4294967294"));
}

TEST_CASE("T032 ledger prices use commands and refuse nonpositive or overflow drafts") {
    Fixture f; auto ctx = f.context(); start(ui::ledger_register, ctx);
    CHECK(has_text("os.ledger.empty"));
    frame(ctx, 0, 1, 1); frame(ctx, 5); frame(ctx, 6);
    CHECK(f.world.economy.price_hour == 11);
    frame(ctx, 0, 3, 1); frame(ctx, 4); frame(ctx, 6);
    CHECK(f.world.economy.price_night == 49);
    frame(ctx, 0, 3, 2); frame(ctx, 5); frame(ctx, 6);
    CHECK(f.world.economy.goods_price[0] == 13);
    f.world.economy.price_hour = 1;
    frame(ctx, 0, 3, 0); frame(ctx, 4); frame(ctx, 6);
    CHECK(f.world.economy.price_hour == 1);
    f.world.economy.price_night = std::numeric_limits<int32_t>::max();
    frame(ctx, 0, 3, 1); frame(ctx, 5); frame(ctx, 6);
    CHECK(f.world.economy.price_night == std::numeric_limits<int32_t>::max());
    f.world.clock.phase = core::Phase::kGameOver;
    frame(ctx, 4); frame(ctx, 6);
    CHECK(has_text("os.ledger.failed"));
    CHECK(f.world.economy.price_night == std::numeric_limits<int32_t>::max());
}

TEST_CASE("T032 records use core ordering and names and show honest tournament state") {
    Fixture f;
    auto& b = f.world.records[0]; b.count = 4;
    b.top[0] = {500, f.world.clock.date, core::kRecordPlayer, core::kNone};
    b.top[1] = {400, f.world.clock.date, 0, core::kNone};
    b.top[2] = {300, f.world.clock.date, core::kNone, 0};
    b.top[3] = {200, f.world.clock.date, core::kNone, core::kNone};
    auto ctx = f.context(); start(ui::records_register, ctx);
    REQUIRE(rows_of(3).cells.size() == 4);
    CHECK(rows_of(3).cells[0][1] == "os.records.player");
    CHECK(rows_of(3).cells[1][1] == "regular");
    CHECK(rows_of(3).cells[2][1] == "student");
    CHECK(rows_of(3).cells[3][1] == "os.records.visitor");
    CHECK(rows_of(3).cells[0][2] == "500"); CHECK(has_text("os.records.higher"));
    frame(ctx, 0, 2, 1); CHECK(has_text("os.records.lower")); CHECK(has_text("os.records.empty"));
    frame(ctx, 0, 1, 1); CHECK(has_text("os.records.no_tournaments"));
    f.data.minigame_count = 0; frame(ctx, 0, 1, 0); CHECK(has_text("os.records.no_games"));
}

TEST_CASE("T032 inactive app frames cannot issue commands") {
    Fixture f; auto ctx = f.context(); start(ui::shop_register, ctx);
    frame(ctx, 0, 2, 0, true);
    ctx.is_active = false; frame(ctx, 6);
    CHECK(f.world.inventory.order_count == 0);
    CHECK(f.world.economy.money == 5000);
}
