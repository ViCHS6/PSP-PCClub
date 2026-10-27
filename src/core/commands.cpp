// Команды экономики: скорость, цены, заказы, апгрейды, долг, почта (контракт — core_api.h).
#include "core/core_api.h"
#include "core/formulas_api.h"
#include "core/sim_internal.h"

namespace core {

namespace {

bool game_over(const World& w) { return w.clock.phase == Phase::kGameOver; }

// Лицензий игры с учётом ещё не доставленных заказов.
int licenses_with_orders(const World& w, Index game) {
    int n = w.library.licenses[game];
    for (int i = 0; i < w.inventory.order_count; ++i) {
        const Order& o = w.inventory.orders[i];
        if (o.kind == OrderKind::kLicense && o.ref == game) n += o.count;
    }
    return n;
}

}  // namespace

CmdResult cmd_set_speed(World& w, int speed) {
    if (speed != 1 && speed != 2 && speed != 4) return CmdResult::kInvalid;
    w.clock.speed = static_cast<uint8_t>(speed);
    return CmdResult::kOk;
}

CmdResult cmd_set_price_hour(World& w, int32_t price) {
    if (game_over(w)) return CmdResult::kWrongPhase;
    if (price <= 0) return CmdResult::kInvalid;
    w.economy.price_hour = price;
    return CmdResult::kOk;
}

CmdResult cmd_set_price_night(World& w, int32_t price) {
    if (game_over(w)) return CmdResult::kWrongPhase;
    if (price <= 0) return CmdResult::kInvalid;
    w.economy.price_night = price;
    return CmdResult::kOk;
}

CmdResult cmd_set_goods_price(World& w, Index goods, int32_t price) {
    if (game_over(w)) return CmdResult::kWrongPhase;
    if (goods >= w.data->goods_count || price <= 0) return CmdResult::kInvalid;
    w.economy.goods_price[goods] = price;
    return CmdResult::kOk;
}

CmdResult cmd_order(World& w, OrderKind kind, Index ref, uint16_t count) {
    if (game_over(w)) return CmdResult::kWrongPhase;
    const GameData& d = *w.data;
    if (count == 0) return CmdResult::kInvalid;
    int64_t price = 0;
    switch (kind) {
        case OrderKind::kHardware:
            if (ref >= d.hardware_count || d.hardware[ref].release > w.clock.date) return CmdResult::kInvalid;
            price = static_cast<int64_t>(current_hardware_price(w, ref)) * count;
            break;
        case OrderKind::kGoods:
            if (ref >= d.goods_count) return CmdResult::kInvalid;
            price = static_cast<int64_t>(d.goods[ref].buy_price) * count;
            break;
        case OrderKind::kLicense:
            if (!game_released(w, ref) || licenses_with_orders(w, ref) + count > 255) return CmdResult::kInvalid;
            price = static_cast<int64_t>(d.games[ref].price_license) * count;
            break;
        case OrderKind::kPirate:
            if (!game_released(w, ref)) return CmdResult::kInvalid;
            if ((w.library.pirate[ref >> 5] >> (ref & 31)) & 1u) return CmdResult::kInvalid;
            for (int i = 0; i < w.inventory.order_count; ++i) {
                const Order& o = w.inventory.orders[i];
                if (o.kind == OrderKind::kPirate && o.ref == ref) return CmdResult::kInvalid;
            }
            count = 1;  // диск на все места
            price = d.games[ref].price_pirate;
            break;
        default: return CmdResult::kInvalid;
    }
    if (price > w.economy.money) return CmdResult::kNoMoney;
    if (w.inventory.order_count >= kMaxOrders) return CmdResult::kLimit;
    Order& o = w.inventory.orders[w.inventory.order_count++];
    o = Order{};
    o.kind = kind;
    o.ref = ref;
    o.count = count;
    o.arrives = w.clock.date + 1;  // доставка в 8:00 следующего дня
    detail::add_expense(w, kExpPurchases, static_cast<int32_t>(price));
    return CmdResult::kOk;
}

CmdResult cmd_buy_upgrade(World& w, Index upgrade) {
    if (game_over(w)) return CmdResult::kWrongPhase;
    const GameData& d = *w.data;
    if (upgrade >= d.upgrade_count) return CmdResult::kInvalid;
    const UpgradeRec& r = d.upgrades[upgrade];
    if (r.kind == kUpgAd) {
        // Временная реклама: продлевается повторной покупкой.
        if (r.price > w.economy.money) return CmdResult::kNoMoney;
        DayNum& until = w.priv.ad_until[upgrade];
        if (until < w.clock.date - 1) until = w.clock.date - 1;
        until += r.days > 0 ? r.days : 1;
    } else {
        if (upgrade_owned(w, upgrade) || !detail::upgrade_requirement_met(w, r.requires_upgrade)) {
            return CmdResult::kInvalid;
        }
        if (r.level > 0) {
            const UpgradeRec* have = detail::owned_upgrade_of_kind(w, r.kind);
            if (have != nullptr && have->level > 0 && have->level >= r.level) return CmdResult::kInvalid;
        }
        if (r.price > w.economy.money) return CmdResult::kNoMoney;
        detail::own_upgrade(w, upgrade);
    }
    detail::add_expense(w, kExpPurchases, r.price);
    detail::events_on_action(w, kActUpgradeBought, upgrade);
    return CmdResult::kOk;
}

CmdResult cmd_repay_debt(World& w, int32_t amount) {
    if (game_over(w)) return CmdResult::kWrongPhase;
    if (amount <= 0 || amount > w.economy.debt) return CmdResult::kInvalid;
    if (amount > w.economy.money) return CmdResult::kNoMoney;
    w.economy.money -= amount;
    w.economy.debt -= amount;
    return CmdResult::kOk;
}

CmdResult cmd_mail_read(World& w, uint8_t mail) {
    Mailbox& mb = w.mailbox;
    if (mail >= mb.count) return CmdResult::kInvalid;
    if (!mb.mail[mail].read) {
        mb.mail[mail].read = 1;
        if (mb.unread > 0) --mb.unread;
    }
    return CmdResult::kOk;
}

}  // namespace core
