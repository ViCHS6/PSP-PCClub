// Переменные и условия GDD §8.6. Нужны ядру (разблокировка постоянных клиентов, §7.5)
// и системе событий (T023).
#include "core/core_api.h"
#include "core/sim_internal.h"

namespace core {
namespace detail {

float cond_var_value(const World& w, uint8_t var, Index arg) {
    const GameData& d = *w.data;
    switch (var) {
        case kVarMoney: return static_cast<float>(w.economy.money);
        case kVarRep: return w.economy.rep;
        case kVarDay: return static_cast<float>(w.clock.day_index);
        case kVarHour: return static_cast<float>(w.clock.minute / 60);
        case kVarWeekday: return static_cast<float>(w.clock.weekday);
        case kVarDate: return static_cast<float>(w.clock.date);
        case kVarPcs: return static_cast<float>(pcs_working(w));
        case kVarPirateShare: return pirate_share(w);
        case kVarPirateInstalls: return static_cast<float>(pirate_installs(w));
        case kVarFlag: return flag_set(w, arg) ? 1.0f : 0.0f;
        case kVarRelation: return arg < d.regular_count ? static_cast<float>(w.regulars[arg].relation) : 0.0f;
        case kVarGameInstalled: {
            int n = 0;
            for (int pc = 0; pc < kMaxPcs; ++pc) {
                if (pc_has_game(w, static_cast<uint8_t>(pc), arg)) ++n;
            }
            return static_cast<float>(n);
        }
        case kVarVisitsArchetype:
            return arg < kMaxArchetypes ? static_cast<float>(w.stats.visits_archetype[arg]) : 0.0f;
        case kVarCompetitorActive: return w.competitor.active ? 1.0f : 0.0f;
        default: return 0.0f;
    }
}

bool cond_holds(const World& w, const CondRec& c) {
    const float x = cond_var_value(w, c.var, c.arg);
    switch (c.op) {
        case kOpEq: return x == c.val;
        case kOpNe: return x != c.val;
        case kOpLt: return x < c.val;
        case kOpLe: return x <= c.val;
        case kOpGt: return x > c.val;
        case kOpGe: return x >= c.val;
        default: return false;
    }
}

bool conds_hold(const World& w, uint16_t first, uint8_t count) {
    if (static_cast<int>(first) + count > w.data->cond_count) return false;
    for (int i = 0; i < count; ++i) {
        if (!cond_holds(w, w.data->conds[first + i])) return false;
    }
    return true;
}

}  // namespace detail
}  // namespace core
