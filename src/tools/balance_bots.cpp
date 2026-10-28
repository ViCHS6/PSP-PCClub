#include "balance_bots.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "core/formulas_api.h"

namespace balance {
namespace {

using namespace core;

// Пороги тестового игрока, не баланс игры: запас на месяц обязательных платежей,
// минимум четверть стартовой кассы; качество мини-игр постоянно, их время не моделируется.
constexpr int kReserveDays = 30;
constexpr int kOpenHours = (kMinutesPerDay - kOpenMinute) / 60;
constexpr float kInitialReserveShare = 0.25f;
constexpr float kServiceQuality = 0.8f;
constexpr float kCleanDust = 30.0f;
constexpr int kGoodsTarget = 12;
constexpr int kLibraryGames = 5;
constexpr float kLoadLow = 0.35f;
constexpr float kLoadHigh = 0.75f;
constexpr float kExpandLoad = 0.65f;
constexpr float kPriceMinShare = 0.75f;
constexpr float kPriceMaxShare = 1.35f;
constexpr float kPriceStepShare = 0.05f;
constexpr float kHardwareGain = 1.2f;
constexpr int kHardwareOrders = 4;
constexpr float kRepValue = 100.0f;  // Рублёвая оценка пункта репутации при выборе ответа.

int pending(const World& w, OrderKind kind, Index ref) {
    int count = 0;
    for (int i = 0; i < w.inventory.order_count; ++i) {
        const Order& order = w.inventory.orders[i];
        if (order.kind == kind && order.ref == ref) count += order.count;
    }
    return count;
}

int64_t reserve(const World& w) {
    int64_t monthly = 0;
    for (Index u = 0; u < w.data->upgrade_count; ++u) {
        if (!upgrade_owned(w, u)) continue;
        const UpgradeRec& rec = w.data->upgrades[u];
        monthly += rec.cost_month + int64_t{kReserveDays} *
                   (rec.cost_day + int64_t{kOpenHours} * rec.cost_hour);
    }
    return std::max(monthly, int64_t{formula::money_round(
                               w.data->economy->start_money * kInitialReserveShare)});
}

bool affordable(const World& w, int64_t cost) {
    return cost >= 0 && int64_t{w.economy.money} - cost >= reserve(w);
}

float game_value(const World& w, Index game) {
    float interest = 0.0f;
    const uint8_t genre = w.data->games[game].genre;
    for (Index a = 0; a < w.data->archetype_count; ++a) {
        const ArchetypeRec& rec = w.data->archetypes[a];
        interest += rec.base_rate * rec.affinity[genre];
    }
    // GDD §7.6: предпочтение по популярности и жанровому интересу аудитории.
    return game_popularity_today(w, game) * interest;
}

bool usable(const World& w, uint8_t pc, Index game) {
    const PcState state = w.pcs[pc].state;
    return state != PcState::kEmpty && state != PcState::kBroken &&
           formula::perf_runs(pc_perf(w, pc, game)) &&
           (pc_has_game(w, pc, game) ||
            int64_t{pc_free_hdd_mb(w, pc)} >= w.data->games[game].req_hdd_mb);
}

void library(World& w) {
    std::array<int, kMaxPcs> installed{};
    for (uint8_t pc = 0; pc < w.club.seats; ++pc) {
        for (Index g = 0; g < w.data->game_count; ++g) {
            if (pc_has_game(w, pc, g)) ++installed[pc];
        }
    }
    std::array<Index, kMaxGames> ranked{};
    int count = 0;
    for (Index g = 0; g < w.data->game_count; ++g) {
        if (game_released(w, g) && game_value(w, g) > 0.0f) ranked[count++] = g;
    }
    std::sort(ranked.begin(), ranked.begin() + count, [&w](Index a, Index b) {
        const float va = game_value(w, a), vb = game_value(w, b);
        return va == vb ? a < b : va > vb;
    });
    count = std::min(count, kLibraryGames);
    float total = 0.0f;
    for (int i = 0; i < count; ++i) total += game_value(w, ranked[i]);
    for (int i = 0; i < count; ++i) {
        const Index g = ranked[i];
        int suitable = 0;
        for (uint8_t pc = 0; pc < w.club.seats; ++pc) {
            if (usable(w, pc, g)) ++suitable;
        }
        while (free_licenses(w, g) > 0) {
            uint8_t best = kNoPc;
            for (uint8_t pc = 0; pc < w.club.seats; ++pc) {
                if (pc_has_game(w, pc, g) || !usable(w, pc, g)) continue;
                // При равенстве остаётся меньший номер ПК.
                if (best == kNoPc || installed[pc] < installed[best]) best = pc;
            }
            if (best == kNoPc || cmd_install(w, best, g, false, kServiceQuality) != CmdResult::kOk)
                break;
            ++installed[best];
        }
        const int target = std::min(suitable, static_cast<int>(std::ceil(
                                                w.club.seats * game_value(w, g) / total)));
        int need = target - w.library.licenses[g] - pending(w, OrderKind::kLicense, g);
        const int32_t price = w.data->games[g].price_license;
        if (price > 0) need = std::min<int64_t>(need, std::max<int64_t>(
                                      0, (int64_t{w.economy.money} - reserve(w)) / price));
        if (need > 0 && affordable(w, int64_t{need} * price))
            cmd_order(w, OrderKind::kLicense, g, static_cast<uint16_t>(need));
    }
}

void restock(World& w) {
    for (Index g = 0; g < w.data->goods_count; ++g) {
        const int need = kGoodsTarget - w.inventory.goods[g] - pending(w, OrderKind::kGoods, g);
        if (need > 0 && affordable(w, int64_t{need} * w.data->goods[g].buy_price))
            cmd_order(w, OrderKind::kGoods, g, static_cast<uint16_t>(need));
    }
}

void maintain(World& w) {
    for (uint8_t pc = 0; pc < w.club.seats; ++pc) {
        const Pc& p = w.pcs[pc];
        if (p.state == PcState::kHung) cmd_reboot(w, pc, kServiceQuality);
        if (p.state == PcState::kBroken && p.broken_component < kComponentCount) {
            const Component comp = static_cast<Component>(p.broken_component);
            const Index part = p.component[comp];
            if (!formula::wear_broken(p.wear[comp])) {
                cmd_repair(w, pc, comp, kServiceQuality);
            } else if (part != kNone) {
                // Обязательный ремонт может расходовать резерв, покупка без займа.
                if (cmd_replace_component(w, pc, part, kServiceQuality) != CmdResult::kOk &&
                    pending(w, OrderKind::kHardware, part) == 0)
                    cmd_order(w, OrderKind::kHardware, part, 1);
            }
        }
        if (p.state == PcState::kOff) cmd_set_pc_power(w, pc, true);
        if (p.dust >= kCleanDust && p.visitor == kNoVisitor)
            cmd_clean_dust(w, pc, kServiceQuality);
    }
}

void requests(World& w) {
    for (uint8_t v = 0; v < kMaxVisitors; ++v) {
        const Visitor& visitor = w.visitors[v];
        if (visitor.request == Request::kFood || visitor.request == Request::kHelp)
            cmd_resolve_request(w, v);
        if (visitor.request == Request::kGame && visitor.pc < w.club.seats &&
            visitor.request_arg < w.data->game_count && free_licenses(w, visitor.request_arg) > 0)
            cmd_install(w, visitor.pc, visitor.request_arg, false, kServiceQuality);
    }
}

float observed_value(const World& w, uint16_t variable, Index arg) {
    switch (variable) {
        case kVarMoney: return static_cast<float>(w.economy.money);
        case kVarRep: return w.economy.rep;
        case kVarDay: return static_cast<float>(w.clock.day_index);
        case kVarHour: return w.clock.minute / 60;
        case kVarWeekday: return w.clock.weekday;
        case kVarDate: return static_cast<float>(w.clock.date);
        case kVarPcs: return static_cast<float>(pcs_working(w));
        case kVarPirateShare: return pirate_share(w);
        case kVarPirateInstalls: return static_cast<float>(pirate_installs(w));
        case kVarFlag: return flag_set(w, arg) ? 1.0f : 0.0f;
        case kVarRelation:
            return arg < w.data->regular_count ? w.regulars[arg].relation : 0.0f;
        case kVarGameInstalled: {
            int count = 0;
            for (uint8_t pc = 0; pc < w.club.seats; ++pc)
                if (pc_has_game(w, pc, arg)) ++count;
            return static_cast<float>(count);
        }
        case kVarVisitsArchetype:
            return arg < w.data->archetype_count ?
                   static_cast<float>(w.stats.visits_archetype[arg]) : 0.0f;
        case kVarCompetitorActive: return w.competitor.active ? 1.0f : 0.0f;
        default: return 0.0f;
    }
}

float effect_score(const World& w, uint16_t first, uint8_t count, int depth = 0) {
    float score = 0.0f;
    for (int e = 0; e < count; ++e) {
        const EffectRec& effect = w.data->effects[first + e];
        if (effect.type == kEffMoney)
            score += effect.sub == 0 ? static_cast<float>(effect.ival) :
                     effect.fval * observed_value(w, effect.ref, effect.arg);
        if (effect.type == kEffRep) score += effect.fval * kRepValue;
        // Формат данных ограничивает вложенность chance четырьмя уровнями.
        if (effect.type == kEffChance && depth < 4)
            score += effect.fval * effect_score(w, effect.child_first, effect.child_count, depth + 1);
    }
    return score;
}

void answer_events(World& w) {
    for (uint8_t a = 0; a < kMaxActiveEvents; ++a) {
        const Index event = w.events.active[a].event;
        if (event >= w.data->event_count) continue;
        const EventRec& rec = w.data->events[event];
        std::array<uint8_t, 256> choices{};
        std::array<float, 256> scores{};
        for (int c = 0; c < rec.choice_count; ++c) {
            choices[c] = static_cast<uint8_t>(c);
            const ChoiceRec& choice = w.data->choices[rec.choice_first + c];
            // Только известные денежные и репутационные последствия; условия проверяет API.
            scores[c] = effect_score(w, choice.effect_first, choice.effect_count);
        }
        std::sort(choices.begin(), choices.begin() + rec.choice_count,
                  [&scores](uint8_t x, uint8_t y) {
                      return scores[x] == scores[y] ? x < y : scores[x] > scores[y];
                  });
        for (int c = 0; c < rec.choice_count; ++c) {
            if (cmd_event_choose(w, a, choices[c]) == CmdResult::kOk) break;
        }
    }
    for (uint8_t m = 0; m < w.mailbox.count; ++m) {
        if (!w.mailbox.mail[m].read) cmd_mail_read(w, m);
    }
}

float last_load(const World& w) {
    const DayStats* day = stats_days_ago(w, 0);
    return day && w.club.seats ? day->load_minutes /
        static_cast<float>(w.club.seats * (kMinutesPerDay - kOpenMinute)) : 0.0f;
}

void adjust_price(World& w) {
    if (!stats_days_ago(w, 0)) return;
    float sum = 0.0f, weight = 0.0f;
    for (Index a = 0; a < w.data->archetype_count; ++a) {
        const ArchetypeRec& rec = w.data->archetypes[a];
        sum += rec.base_rate * formula::expected_price(static_cast<float>(rec.price_expect),
                          w.competitor.active != 0, static_cast<float>(w.competitor.price));
        weight += rec.base_rate;
    }
    if (weight <= 0.0f) return;
    const float expected = sum / weight;
    const int32_t low = std::max(1, formula::money_round(expected * kPriceMinShare));
    const int32_t high = std::max(low, formula::money_round(expected * kPriceMaxShare));
    const int32_t step = std::max(1, formula::money_round(expected * kPriceStepShare));
    int64_t price = w.economy.price_hour;
    if (last_load(w) < kLoadLow) price -= step;
    if (last_load(w) > kLoadHigh) price += step;
    cmd_set_price_hour(w, static_cast<int32_t>(std::clamp<int64_t>(price, low, high)));
}

float part_value(const HardwareRec& part) {
    switch (part.type) {
        case kCompCpu:
        case kCompGpu: return part.score;
        case kCompRam: return part.ram_mb;
        case kCompHdd: return static_cast<float>(part.hdd_mb);
        default: return static_cast<float>(part.comfort);
    }
}

void improve_hardware(World& w) {
    int orders = 0;
    for (uint8_t pc = 0; pc < w.club.seats; ++pc) {
        const Pc& p = w.pcs[pc];
        if (p.visitor != kNoVisitor || p.state == PcState::kBroken) continue;
        for (int c = 0; c < kComponentCount; ++c) {
            const Index old = p.component[c];
            // На новом месте сначала собираем обязательные компоненты.
            if (old == kNone && c >= kCompHeadphones && p.state == PcState::kEmpty) continue;
            Index best = kNone;
            int32_t best_price = std::numeric_limits<int32_t>::max();
            for (Index h = 0; h < w.data->hardware_count; ++h) {
                const HardwareRec& part = w.data->hardware[h];
                if (part.type != c || part.release > w.clock.date) continue;
                if (old == kNone && c >= kCompHeadphones && part.comfort <= 0) continue;
                if (old != kNone && (h == old || part_value(part) <=
                                           part_value(w.data->hardware[old]) * kHardwareGain)) continue;
                const int32_t price = current_hardware_price(w, h);
                if (price < best_price) { best = h; best_price = price; }
            }
            if (best == kNone) continue;
            if (w.inventory.hardware[best] > 0) {
                cmd_replace_component(w, pc, best, kServiceQuality);
            } else if (orders < kHardwareOrders && pending(w, OrderKind::kHardware, best) == 0 &&
                       affordable(w, best_price) &&
                       cmd_order(w, OrderKind::kHardware, best, 1) == CmdResult::kOk) {
                ++orders;
            }
        }
    }
}

void upgrades(Bot& bot, World& w) {
    for (Index u = 0; u < w.data->upgrade_count; ++u) {
        const UpgradeRec& rec = w.data->upgrades[u];
        if (rec.requires_upgrade != kNone && !upgrade_owned(w, rec.requires_upgrade)) continue;
        if (rec.kind == kUpgAd) {
            if (bot.ad_until[u] >= w.clock.day_index || last_load(w) > kExpandLoad) continue;
        } else {
            bool superseded = false;
            for (Index v = 0; v < w.data->upgrade_count; ++v) {
                const UpgradeRec& owned = w.data->upgrades[v];
                if (upgrade_owned(w, v) && owned.kind == rec.kind && owned.level >= rec.level)
                    superseded = true;
            }
            if (superseded) continue;
        }
        switch (rec.kind) {
            case kUpgHall:
                if (last_load(w) < kExpandLoad || pcs_working(w) < w.club.seats) continue;
                break;
            case kUpgInternet:
            case kUpgNetwork:
            case kUpgAircon:
            case kUpgSoundproof:
            case kUpgRenovation:
            case kUpgSign:
            case kUpgAd: break;
            default: continue;
        }
        const int64_t cost = rec.price + int64_t{rec.cost_month} + int64_t{kReserveDays} *
                            (rec.cost_day + int64_t{kOpenHours} * rec.cost_hour);
        if (affordable(w, cost) && cmd_buy_upgrade(w, u) == CmdResult::kOk) {
            if (rec.kind == kUpgAd) bot.ad_until[u] = w.clock.day_index + rec.days;
            break;  // Не более одного апгрейда утром: даём проявиться его эффекту.
        }
    }
}

}  // namespace

void bot_act(Bot& bot, core::World& w) {
    using namespace core;
    if (w.clock.phase == Phase::kClosed || w.clock.phase == Phase::kGameOver) return;
    if (bot.strategy != Strategy::kPassive) {
        maintain(w);
        requests(w);
        answer_events(w);
        if (w.clock.phase == Phase::kMorning && bot.morning_day != w.clock.day_index) {
            bot.morning_day = w.clock.day_index;
            library(w);
            restock(w);
            if (bot.strategy == Strategy::kAggressive) {
                adjust_price(w);
                upgrades(bot, w);
                improve_hardware(w);
            }
        }
    }
    if (w.clock.phase != Phase::kOpen || w.queue_len == 0) return;
    const uint8_t visitor = w.queue[0];
    const uint8_t pc = best_pc_for(w, visitor);
    if (pc != kNoPc) cmd_serve(w, visitor, pc, Tariff::kHourly);
}

}  // namespace balance
