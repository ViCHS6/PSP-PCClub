#include "game/club_scene_rules.h"

#include "core/data_api.h"

namespace game {
namespace {

bool has_visitor(const core::Pc& p) { return p.visitor != core::kNoVisitor; }

bool installable(const core::World& w, uint8_t pc, core::Index g, bool* pirate) {
    if (g >= w.data->game_count || !core::game_released(w, g) || core::pc_has_game(w, pc, g)) return false;
    if (static_cast<int64_t>(core::pc_free_hdd_mb(w, pc)) < w.data->games[g].req_hdd_mb) return false;
    if (core::free_licenses(w, g) > 0) {
        *pirate = false;
        return true;
    }
    *pirate = true;
    return ((w.library.pirate[g >> 5] >> (g & 31)) & 1u) != 0;
}

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

}  // namespace

core::Index install_candidate(const core::World& w, uint8_t pc, bool* pirate) {
    bool p = false;
    if (!w.data || pc >= core::kMaxPcs) return core::kNone;
    const core::Pc& m = w.pcs[pc];
    if (has_visitor(m)) {
        const core::Visitor& v = w.visitors[m.visitor];
        if (v.request == core::Request::kGame && installable(w, pc, v.request_arg, &p)) {
            *pirate = p;
            return v.request_arg;
        }
    }
    for (core::Index g = 0; g < w.data->game_count; ++g) {
        if (installable(w, pc, g, &p)) {
            *pirate = p;
            return g;
        }
    }
    return core::kNone;
}

SeatActions seat_actions(const core::World& w, uint8_t pc, bool can_repair) {
    SeatActions a;
    if (pc >= core::kMaxPcs || pc >= w.club.seats) return a;
    const core::Pc& p = w.pcs[pc];
    switch (p.state) {
        case core::PcState::kEmpty:
            return a;
        case core::PcState::kOff:
            a.case_primary = PcAction::kPowerOn;
            break;
        case core::PcState::kIdle:
        case core::PcState::kHung:
            a.case_primary = has_visitor(p) ? PcAction::kInfoBusy : PcAction::kPowerOff;
            break;
        case core::PcState::kBusy:
            a.case_primary = PcAction::kInfoBusy;
            break;
        case core::PcState::kBroken: {
            const bool fixable = p.broken_component < core::kComponentCount &&
                                 p.component[p.broken_component] != core::kNone &&
                                 p.wear[p.broken_component] < 100.0f;
            a.case_primary = can_repair && fixable && !has_visitor(p) ? PcAction::kRepair : PcAction::kInfoBroken;
            break;
        }
    }
    if (p.state == core::PcState::kHung) {
        a.monitor = PcAction::kReboot;
    } else if (p.state == core::PcState::kIdle || p.state == core::PcState::kBusy) {
        a.install_game = install_candidate(w, pc, &a.install_pirate);
        if (a.install_game != core::kNone) a.monitor = PcAction::kInstall;
    }
    if (p.dust >= kDustAction && p.state != core::PcState::kBusy) a.case_alt = PcAction::kClean;
    return a;
}

const char* pc_action_hint(PcAction action) {
    switch (action) {
        case PcAction::kNone: return nullptr;
        case PcAction::kInfoBusy: return "world.pc.busy";
        case PcAction::kInfoBroken: return "world.pc.broken";
        case PcAction::kPowerOn: return "world.pc.power_on";
        case PcAction::kPowerOff: return "world.pc.power_off";
        case PcAction::kReboot: return "world.pc.reboot";
        case PcAction::kClean: return "world.pc.dust";
        case PcAction::kInstall: return "world.pc.install";
        case PcAction::kRepair: return "world.pc.repair";
    }
    return nullptr;
}

bool pc_action_acts(PcAction action) {
    return action != PcAction::kNone && action != PcAction::kInfoBusy && action != PcAction::kInfoBroken;
}

const char* pc_action_minigame(PcAction action) {
    switch (action) {
        case PcAction::kReboot: return "svc_reboot";
        case PcAction::kClean: return "svc_clean";
        case PcAction::kInstall: return "svc_install";
        case PcAction::kRepair: return "svc_replace";
        default: return nullptr;
    }
}

float pc_action_difficulty(const core::World& w, uint8_t pc, PcAction action, core::Index game) {
    if (pc >= core::kMaxPcs) return 0.5f;
    const core::Pc& p = w.pcs[pc];
    switch (action) {
        case PcAction::kClean:
            return clamp01(p.dust / 100.0f);  // sdk.h: «dust/100 для Чистки»
        case PcAction::kReboot: {
            // Чем изношеннее железо, тем капризнее загрузка.
            float worst = 0.0f;
            for (int c = 0; c < core::kComponentCount; ++c)
                if (p.component[c] != core::kNone && p.wear[c] > worst) worst = p.wear[c];
            return clamp01(worst / 100.0f);
        }
        case PcAction::kInstall:
            // Больше игра — больше дисков (svc_install: 3…5); 3 ГБ и выше — максимум.
            return w.data && game < w.data->game_count ? clamp01(w.data->games[game].req_hdd_mb / 3000.0f) : 0.5f;
        case PcAction::kRepair:
            return p.broken_component < core::kComponentCount ? clamp01(p.wear[p.broken_component] / 100.0f) : 0.5f;
        default:
            return 0.5f;
    }
}

uint8_t pc_action_variant(const core::World& w, uint8_t pc, PcAction action) {
    if (action == PcAction::kRepair && pc < core::kMaxPcs) return w.pcs[pc].broken_component;
    return 0;
}

core::CmdResult pc_action_apply(core::World& w, uint8_t pc, PcAction action, core::Index game, bool pirate,
                                float quality) {
    switch (action) {
        case PcAction::kPowerOn: return core::cmd_set_pc_power(w, pc, true);
        case PcAction::kPowerOff: return core::cmd_set_pc_power(w, pc, false);
        case PcAction::kReboot: return core::cmd_reboot(w, pc, quality);
        case PcAction::kClean: return core::cmd_clean_dust(w, pc, quality);
        case PcAction::kInstall: return core::cmd_install(w, pc, game, pirate, quality);
        case PcAction::kRepair:
            if (pc >= core::kMaxPcs || w.pcs[pc].broken_component >= core::kComponentCount)
                return core::CmdResult::kInvalid;
            return core::cmd_repair(w, pc, static_cast<core::Component>(w.pcs[pc].broken_component), quality);
        default: return core::CmdResult::kInvalid;
    }
}

}  // namespace game
