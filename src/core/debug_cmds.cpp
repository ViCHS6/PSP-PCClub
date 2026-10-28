// Отладочные команды ядра (DBG) — см. debug_cmds.h.
#include "core/debug_cmds.h"

#include "core/formulas_api.h"
#include "core/sim_internal.h"

namespace core {

namespace {

constexpr float kDebugDust = 90.0f;

bool valid_pc(const World& w, uint8_t pc) { return w.data != nullptr && pc < w.club.seats && pc < kMaxPcs; }

bool working(const Pc& p) { return p.state == PcState::kIdle || p.state == PcState::kBusy; }

// Один шаг к следующей смене фазы: последняя минута фазы + настоящий tick (или cmd_end_day).
void step_phase(World& w) {
    Clock& c = w.clock;
    switch (c.phase) {
        case Phase::kMorning:
            c.minute = kOpenMinute - 1;
            tick(w);
            break;
        case Phase::kOpen:
            c.minute = kMinutesPerDay - 1;
            tick(w);
            break;
        case Phase::kClosed: cmd_end_day(w); break;
        case Phase::kNight:
            c.minute = kMorningMinute - 1;
            tick(w);
            break;
        case Phase::kGameOver: break;
    }
}

}  // namespace

CmdResult debug_add_money(World& w, int32_t amount) {
    w.economy.money += amount;
    return CmdResult::kOk;
}

CmdResult debug_skip(World& w, DebugSkip to) {
    if (w.data == nullptr || w.clock.phase == Phase::kGameOver) return CmdResult::kWrongPhase;
    // Хотя бы один шаг (из утра «до утра» — следующее), не больше суток:
    // утро → открыто → закрыто → (ночь) → утро → открыто.
    const Phase target = to == DebugSkip::kToOpen ? Phase::kOpen
                         : to == DebugSkip::kToClose ? Phase::kClosed : Phase::kMorning;
    for (int guard = 0; guard < 6 && w.clock.phase != Phase::kGameOver; ++guard) {
        step_phase(w);
        if (w.clock.phase == target) break;
    }
    return CmdResult::kOk;
}

float debug_minutes_per_real_second(const World& w, int mult) {
    if (mult <= 1) return minutes_per_real_second(w);
    switch (w.clock.phase) {
        case Phase::kOpen:
        case Phase::kMorning: return 2.0f * static_cast<float>(mult);
        case Phase::kNight: return 16.0f * static_cast<float>(mult);
        default: return 0.0f;
    }
}

CmdResult debug_spawn_visitor(World& w) {
    if (w.data == nullptr || w.data->archetype_count == 0) return CmdResult::kInvalid;
    if (w.clock.phase != Phase::kOpen && w.clock.phase != Phase::kNight) return CmdResult::kWrongPhase;
    const Index a = static_cast<Index>(rng_range(w.rng, 0, w.data->archetype_count - 1));
    const uint8_t v = detail::spawn_visitor(w, a, kNone);
    if (v == kNoVisitor) return CmdResult::kLimit;
    // Хотя бы на час, иначе развернётся у входа (§7.2) и до очереди не дойдёт.
    Visitor& vis = w.visitors[v];
    if (formula::plan_hours(w.priv.vis_hours_roll[v], vis.money, w.economy.price_hour) == 0) {
        const int hours = w.priv.vis_hours_roll[v] > 0 ? w.priv.vis_hours_roll[v] : 1;
        vis.money = w.economy.price_hour * hours;
    }
    return CmdResult::kOk;
}

int debug_install_all(World& w) {
    if (w.data == nullptr) return 0;
    int installed = 0;
    for (int pc = 0; pc < w.club.seats && pc < kMaxPcs; ++pc) {
        if (w.pcs[pc].state == PcState::kEmpty) continue;
        for (int g = 0; g < w.data->game_count; ++g) {
            const Index gi = static_cast<Index>(g);
            if (!game_released(w, gi) || pc_has_game(w, static_cast<uint8_t>(pc), gi)) continue;
            uint8_t& lic = w.library.licenses[g];
            const bool added = free_licenses(w, gi) <= 0 && lic < 0xFF;
            if (added) ++lic;
            if (cmd_install(w, static_cast<uint8_t>(pc), gi, false, 1.0f) == CmdResult::kOk) ++installed;
            else if (added) --lic;  // не влезло на HDD — лицензию не оставляем
        }
    }
    return installed;
}

CmdResult debug_break_pc(World& w, uint8_t pc) {
    if (!valid_pc(w, pc) || !working(w.pcs[pc])) return CmdResult::kInvalid;
    const Pc& p = w.pcs[pc];
    int comp = -1;
    for (int k = 0; k < kComponentCount; ++k)
        if (p.component[k] != kNone && (comp < 0 || p.wear[k] > p.wear[comp])) comp = k;
    if (comp < 0) return CmdResult::kInvalid;
    detail::break_pc(w, pc, comp);
    return CmdResult::kOk;
}

CmdResult debug_hang_pc(World& w, uint8_t pc) {
    if (!valid_pc(w, pc) || !working(w.pcs[pc])) return CmdResult::kInvalid;
    detail::set_pc_hung(w, pc);
    return CmdResult::kOk;
}

CmdResult debug_dust_pc(World& w, uint8_t pc) {
    if (!valid_pc(w, pc) || w.pcs[pc].state == PcState::kEmpty) return CmdResult::kInvalid;
    w.pcs[pc].dust = kDebugDust;
    return CmdResult::kOk;
}

CmdResult debug_fix_pc(World& w, uint8_t pc) {
    if (!valid_pc(w, pc) || w.pcs[pc].state == PcState::kEmpty) return CmdResult::kInvalid;
    Pc& p = w.pcs[pc];
    for (float& wear : p.wear) wear = 0.0f;
    p.dust = 0.0f;
    p.virus = 0;
    if (p.state == PcState::kHung || p.state == PcState::kBroken) {
        detail::pc_restore(w, pc);
        if (p.visitor != kNoVisitor) {
            const Request r = w.visitors[p.visitor].request;
            if (r == Request::kHang || r == Request::kBroken) detail::resolve_request(w, p.visitor);
        }
    }
    return CmdResult::kOk;
}

CmdResult debug_fire_event(World& w, Index event) {
    if (w.data == nullptr || event >= w.data->event_count) return CmdResult::kInvalid;
    if (w.clock.phase == Phase::kGameOver) return CmdResult::kWrongPhase;
    return detail::events_force(w, event) ? CmdResult::kOk : CmdResult::kLimit;
}

}  // namespace core
