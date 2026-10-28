#include "ui/apps/shop.h"

#include <cstdio>
#include <limits>

namespace ui {
namespace {

enum : WidgetId { kCategory = 1, kCatalog, kAdd, kCart, kRemove, kBuy };
struct State {
    const core::World* owner = nullptr;
    const core::GameData* data = nullptr;
    core::Order cart[core::kMaxOrders]{};
    int count = 0;
    int category = 0;
    ListState catalog, basket;
    const char* categories[4]{};
    const char* add = nullptr;
    const char* remove = nullptr;
    const char* buy = nullptr;
    const char* empty = nullptr;
    const char* unavailable = nullptr;
    const char* delivery = nullptr;
    const char* row_fmt = nullptr;
    const char* total_fmt = nullptr;
    const char* status = nullptr;
    const char* results[static_cast<int>(core::CmdResult::kLimit) + 1]{};
    const char* added = nullptr;
    const char* item = nullptr;
    const char* price = nullptr;
    const char* basket_title = nullptr;
} g_state;

core::OrderKind kind_of(int category) {
    static constexpr core::OrderKind kKinds[] = {core::OrderKind::kHardware, core::OrderKind::kLicense,
                                                core::OrderKind::kPirate, core::OrderKind::kGoods};
    return kKinds[category];
}

bool ready(const Context& ctx) { return ctx.world && ctx.world->data; }

bool available(const core::World& w, core::OrderKind kind, core::Index ref) {
    const core::GameData& d = *w.data;
    if (kind == core::OrderKind::kHardware)
        return ref < d.hardware_count && d.hardware[ref].release <= w.clock.date;
    if (kind == core::OrderKind::kGoods) return ref < d.goods_count;
    return ref < d.game_count && core::game_released(w, ref);
}

int catalog_count(const core::GameData& d, core::OrderKind kind) {
    if (kind == core::OrderKind::kHardware) return d.hardware_count;
    if (kind == core::OrderKind::kGoods) return d.goods_count;
    return d.game_count;
}

core::Index catalog_ref(const core::World& w, int category, int row) {
    if (row < 0) return core::kNone;
    const core::OrderKind kind = kind_of(category);
    for (int i = 0; i < catalog_count(*w.data, kind); ++i) {
        const auto ref = static_cast<core::Index>(i);
        if (available(w, kind, ref) && row-- == 0) return ref;
    }
    return core::kNone;
}

const char* name_of(const core::GameData& d, core::OrderKind kind, core::Index ref) {
    const core::StrId name = kind == core::OrderKind::kHardware ? d.hardware[ref].name :
                             kind == core::OrderKind::kGoods ? d.goods[ref].name : d.games[ref].title;
    return core::data_string(d, name);
}

int32_t price_of(const core::World& w, core::OrderKind kind, core::Index ref) {
    if (!available(w, kind, ref)) return 0;
    switch (kind) {
    case core::OrderKind::kHardware: return core::current_hardware_price(w, ref);
    case core::OrderKind::kGoods: return w.data->goods[ref].buy_price;
    case core::OrderKind::kLicense: return w.data->games[ref].price_license;
    case core::OrderKind::kPirate: return w.data->games[ref].price_pirate;
    }
    return 0;
}

const char* category_text(int i, char*, int, void* user) {
    return static_cast<State*>(user)->categories[i];
}

const char* catalog_cell(int row, int col, char* buf, int cap, void* user) {
    const Context& ctx = *static_cast<Context*>(user);
    const core::OrderKind kind = kind_of(g_state.category);
    const core::Index ref = catalog_ref(*ctx.world, g_state.category, row);
    if (ref == core::kNone) return "";
    if (col == 0) return name_of(*ctx.world->data, kind, ref);
    format_money(price_of(*ctx.world, kind, ref), buf, cap);
    return buf;
}

const char* cart_text(int row, char* buf, int cap, void* user) {
    const State& s = *static_cast<State*>(user);
    const core::Order& o = s.cart[row];
    std::snprintf(buf, static_cast<size_t>(cap), s.row_fmt, o.count,
                  s.categories[o.kind == core::OrderKind::kHardware ? 0 :
                               o.kind == core::OrderKind::kLicense ? 1 :
                               o.kind == core::OrderKind::kPirate ? 2 : 3],
                  name_of(*s.data, o.kind, o.ref));
    return buf;
}

void erase_row(State& s, int row) {
    for (int i = row + 1; i < s.count; ++i) s.cart[i - 1] = s.cart[i];
    --s.count;
    if (s.basket.selected >= s.count) s.basket.selected = static_cast<int16_t>(s.count - 1);
}

void add_item(State& s, core::Index ref) {
    if (ref == core::kNone) return;
    const core::OrderKind kind = kind_of(s.category);
    for (int i = 0; i < s.count; ++i) {
        core::Order& o = s.cart[i];
        if (o.kind != kind || o.ref != ref) continue;
        if (kind == core::OrderKind::kPirate || o.count == UINT16_MAX) {
            s.status = s.results[static_cast<int>(core::CmdResult::kInvalid)];
            return;
        }
        ++o.count;
        s.status = s.added;
        return;
    }
    if (s.count == core::kMaxOrders) {
        s.status = s.results[static_cast<int>(core::CmdResult::kLimit)];
        return;
    }
    core::Order& o = s.cart[s.count++];
    o = core::Order{};
    o.kind = kind;
    o.ref = ref;
    o.count = 1;
    s.status = s.added;
}

void checkout(State& s, core::World& w, int64_t total) {
    if (w.clock.phase == core::Phase::kGameOver) {
        s.status = s.results[static_cast<int>(core::CmdResult::kWrongPhase)];
        return;
    }
    if (total > w.economy.money) {
        s.status = s.results[static_cast<int>(core::CmdResult::kNoMoney)];
        return;
    }
    if (w.inventory.order_count + s.count > core::kMaxOrders) {
        s.status = s.results[static_cast<int>(core::CmdResult::kLimit)];
        return;
    }
    // Команды не образуют транзакцию: успешно заказанные строки сразу убираем из корзины.
    while (s.count > 0) {
        const core::Order o = s.cart[0];
        const core::CmdResult result = core::cmd_order(w, o.kind, o.ref, o.count);
        s.status = s.results[static_cast<int>(result)];
        if (result != core::CmdResult::kOk) return;
        erase_row(s, 0);
    }
}

void on_open(Context&, void* user) {
    State& s = *static_cast<State*>(user);
    s = State{};
    const char* keys[] = {"os.shop.hardware", "os.shop.license", "os.shop.pirate", "os.shop.goods"};
    for (int i = 0; i < 4; ++i) s.categories[i] = str(keys[i]);
    // Текст по коду CmdResult: новый код без строки здесь не соберётся.
    const char* results[] = {"os.shop.ordered", "os.shop.invalid", "os.shop.wrong_phase", "os.shop.no_money",
                             "os.shop.failed", "os.shop.failed", "os.shop.failed", "os.shop.failed",
                             "os.shop.failed", "os.shop.limit"};
    static_assert(sizeof(results) / sizeof(results[0]) == sizeof(s.results) / sizeof(s.results[0]),
                  "os.shop: строка на каждый core::CmdResult");
    for (int i = 0; i < static_cast<int>(sizeof(results) / sizeof(results[0])); ++i) s.results[i] = str(results[i]);
    s.add = str("os.shop.add");
    s.remove = str("os.shop.remove");
    s.buy = str("os.shop.buy");
    s.empty = str("os.shop.empty");
    s.unavailable = str("os.shop.unavailable");
    s.delivery = str("os.shop.delivery");
    s.row_fmt = str("os.shop.row");
    s.total_fmt = str("os.shop.total");
    s.added = str("os.shop.added");
    s.item = str("os.shop.item");
    s.price = str("os.shop.price");
    s.basket_title = str("os.shop.basket");
    s.status = s.delivery;
}

void on_frame(Context& ctx, void* user) {
    State& s = *static_cast<State*>(user);
    if (!ready(ctx)) { label(ctx, {4, 4, 468, 28}, s.unavailable); return; }
    if (s.owner != ctx.world || s.data != ctx.world->data) {
        s.count = 0;
        s.catalog = ListState{};
        s.basket = ListState{};
        s.status = s.delivery;
    }
    s.owner = ctx.world;
    s.data = ctx.world->data;
    if (choice(ctx, kCategory, {4, 4, 236, 16}, &s.category, 4, category_text, &s))
        s.catalog = ListState{};
    label(ctx, {248, 4, 224, 16}, s.basket_title);
    int count = 0;
    for (int i = 0; i < catalog_count(*s.data, kind_of(s.category)); ++i)
        if (available(*ctx.world, kind_of(s.category), static_cast<core::Index>(i))) ++count;
    const Column cols[] = {{s.item, 158, Align::kLeft}, {s.price, 0, Align::kRight}};
    const ListEvent event = table(ctx, kCatalog, {4, 24, 236, 126}, s.catalog, cols, 2, count,
                                  catalog_cell, &ctx);
    const bool add = button(ctx, kAdd, {4, 154, 236, 16}, s.add,
                            s.catalog.selected < 0 ? kWidgetDisabled : 0);
    if (event == ListEvent::kActivated || add) add_item(s, catalog_ref(*ctx.world, s.category, s.catalog.selected));
    list(ctx, kCart, {248, 24, 224, 126}, s.basket, s.count, cart_text, &s);
    if (s.count == 0) label(ctx, {252, 46, 212, 28}, s.empty, kColTextDim);
    if (button(ctx, kRemove, {248, 154, 224, 16}, s.remove,
               s.basket.selected < 0 ? kWidgetDisabled : 0) && s.basket.selected < s.count)
        erase_row(s, s.basket.selected);
    int64_t total = 0;
    for (int i = 0; i < s.count; ++i)
        total += static_cast<int64_t>(price_of(*ctx.world, s.cart[i].kind, s.cart[i].ref)) * s.cart[i].count;
    char money[32], buf[96];
    if (total <= std::numeric_limits<int32_t>::max()) {
        format_money(static_cast<int32_t>(total), money, sizeof(money));
        std::snprintf(buf, sizeof(buf), s.total_fmt, money);
        label(ctx, {4, 176, 300, 16}, buf);
    } else label(ctx, {4, 176, 300, 16}, s.results[3], kColBad);
    if (button(ctx, kBuy, {312, 176, 160, 16}, s.buy, s.count == 0 ? kWidgetDisabled : 0))
        checkout(s, *ctx.world, total);
    label(ctx, {4, 198, 468, 32}, s.status, kColTextDim);
}

}  // namespace

AppId shop_register() {
    AppDesc d{};
    d.id = "shop";
    d.title_key = "os.shop.title";
    d.icon_text = "os.shop.icon";
    d.icon_color = kColGood;
    d.flags = kAppOnDesktop | kAppMaximized;
    d.on_open = on_open;
    d.on_frame = on_frame;
    d.user = &g_state;
    return register_app(d);
}
}  // namespace ui
