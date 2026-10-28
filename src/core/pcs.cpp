// Компьютеры: час нагрузки (пыль, износ, поломки, зависания) и команды обслуживания
// (GDD §6.2, §7.4, §7.7; контракт — core_api.h).
#include "core/core_api.h"
#include "core/formulas_api.h"
#include "core/sim_internal.h"

namespace core {

namespace {

constexpr float kSmokerDust = 1.5f;  // GDD §7.7: курят в зале ×1,5

bool any_smoker_playing(const World& w) {
    for (const Visitor& v : w.visitors) {
        if (v.state == VisitorState::kPlaying && (w.data->archetypes[v.archetype].flags & kArchSmoker)) {
            return true;
        }
    }
    return false;
}

void break_component(World& w, uint8_t pc, int comp) {
    Pc& p = w.pcs[pc];
    p.state = PcState::kBroken;
    p.broken_component = static_cast<uint8_t>(comp);
    p.running_game = kNone;
    ++w.stats.today.pc_failures;
    detail::push_notice(w, NoticeKind::kPcBroken, static_cast<uint8_t>(comp), p.visitor, pc);
    if (p.visitor != kNoVisitor) detail::set_request(w, p.visitor, Request::kBroken, kNone);
    detail::events_on_action(w, kActPcBroken, pc);
}

void hang_pc(World& w, uint8_t pc) {
    Pc& p = w.pcs[pc];
    p.state = PcState::kHung;
    p.running_game = kNone;
    ++w.stats.today.pc_hangs;
    detail::push_notice(w, NoticeKind::kPcHung, 0, p.visitor, pc);
    if (p.visitor != kNoVisitor) detail::set_request(w, p.visitor, Request::kHang, kNone);
    detail::events_on_action(w, kActPcHung, pc);
}

// Час нагрузки ПК (§7.7).
void pc_hour(World& w, uint8_t pc) {
    const GameData& d = *w.data;
    Pc& p = w.pcs[pc];
    float env_dust = any_smoker_playing(w) ? kSmokerDust : 1.0f;
    if (const UpgradeRec* r = detail::owned_upgrade_of_kind(w, kUpgStaffCleaner)) env_dust *= r->value;
    env_dust *= detail::modifier_product(w, kModDust, kNone);
    p.dust += formula::dust_per_hour(env_dust);
    if (p.dust > 100.0f) p.dust = 100.0f;

    const float oh = pc_overheat(w, pc);
    for (int k = 0; k < kComponentCount; ++k) {
        if (p.component[k] == kNone) continue;
        p.wear[k] += formula::wear_per_hour(d.hardware[p.component[k]].wear_rate, oh);
        if (p.wear[k] > 100.0f) p.wear[k] = 100.0f;
    }
    for (int k = 0; k < kComponentCount; ++k) {
        if (p.component[k] == kNone) continue;
        const float pf = formula::fail_prob(d.hardware[p.component[k]].fail_base, p.wear[k],
                                            k == kCompCpu || k == kCompGpu, oh);
        if (pf > 0.0f && rng_chance(w.rng, pf)) {
            break_component(w, pc, k);
            return;
        }
    }
    if (rng_chance(w.rng, formula::hang_prob(oh, p.virus != 0))) hang_pc(w, pc);
}

bool valid_pc(const World& w, uint8_t pc) { return pc < w.club.seats; }

void clear_installed(World& w, uint8_t pc) {
    Pc& p = w.pcs[pc];
    for (int i = 0; i < kGameBitsetWords; ++i) {
        p.installed[i] = 0;
        p.installed_pirate[i] = 0;
    }
    detail::invalidate_caches(w);
}

// Починили компонент, из-за которого ПК стоял: снять kBroken и просьбу посетителя.
void fix_broken(World& w, uint8_t pc, int comp) {
    Pc& p = w.pcs[pc];
    if (p.state != PcState::kBroken || p.broken_component != comp) return;
    detail::pc_restore(w, pc);
    if (p.visitor != kNoVisitor && w.visitors[p.visitor].request == Request::kBroken) {
        detail::resolve_request(w, p.visitor);
    }
}

}  // namespace

namespace detail {

void break_pc(World& w, uint8_t pc, int comp) { break_component(w, pc, comp); }
void set_pc_hung(World& w, uint8_t pc) { hang_pc(w, pc); }

void pcs_tick(World& w) {
    for (int i = 0; i < w.club.seats; ++i) {
        Pc& p = w.pcs[i];
        if (p.state != PcState::kBusy || p.visitor == kNoVisitor ||
            w.visitors[p.visitor].state != VisitorState::kPlaying) {
            continue;
        }
        if (p.load_minutes_today < 0xFFFF) ++p.load_minutes_today;
        if (++w.priv.pc_load_min[i] >= 60) {
            w.priv.pc_load_min[i] = 0;
            pc_hour(w, static_cast<uint8_t>(i));
        }
    }
}

void pc_restore(World& w, uint8_t pc) {
    Pc& p = w.pcs[pc];
    p.broken_component = 0xFF;
    if (p.visitor != kNoVisitor) {
        p.state = PcState::kBusy;
        p.running_game = w.visitors[p.visitor].game;
    } else {
        p.state = PcState::kIdle;
        p.running_game = kNone;
    }
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Команды обслуживания ПК

CmdResult cmd_reboot(World& w, uint8_t pc, float quality) {
    if (w.clock.phase == Phase::kGameOver) return CmdResult::kWrongPhase;
    if (!valid_pc(w, pc) || w.pcs[pc].state != PcState::kHung) return CmdResult::kInvalid;
    if (quality <= 0.0f) return CmdResult::kFailed;
    Pc& p = w.pcs[pc];
    detail::pc_restore(w, pc);
    if (p.visitor != kNoVisitor && w.visitors[p.visitor].request == Request::kHang) {
        detail::resolve_request(w, p.visitor);
    }
    return CmdResult::kOk;
}

CmdResult cmd_clean_dust(World& w, uint8_t pc, float quality) {
    if (w.clock.phase == Phase::kGameOver) return CmdResult::kWrongPhase;
    if (!valid_pc(w, pc) || w.pcs[pc].state == PcState::kEmpty) return CmdResult::kInvalid;
    if (w.pcs[pc].state == PcState::kBusy) return CmdResult::kBusy;
    if (quality <= 0.0f) return CmdResult::kFailed;
    const float q = quality > 1.0f ? 1.0f : quality;
    w.pcs[pc].dust *= 1.0f - q;  // core_api.h: dust · (1 − quality)
    return CmdResult::kOk;
}

CmdResult cmd_repair(World& w, uint8_t pc, Component comp, float quality) {
    if (w.clock.phase == Phase::kGameOver) return CmdResult::kWrongPhase;
    if (!valid_pc(w, pc) || comp >= kComponentCount) return CmdResult::kInvalid;
    Pc& p = w.pcs[pc];
    if (p.component[comp] == kNone || formula::wear_broken(p.wear[comp])) return CmdResult::kInvalid;
    if (p.state == PcState::kBusy) return CmdResult::kBusy;
    if (quality <= 0.0f) return CmdResult::kFailed;
    p.wear[comp] = formula::wear_after_repair(p.wear[comp], quality);  // GDD §7.7
    fix_broken(w, pc, comp);
    return CmdResult::kOk;
}

CmdResult cmd_replace_component(World& w, uint8_t pc, Index hardware, float quality) {
    if (w.clock.phase == Phase::kGameOver) return CmdResult::kWrongPhase;
    if (!valid_pc(w, pc) || hardware >= w.data->hardware_count) return CmdResult::kInvalid;
    Pc& p = w.pcs[pc];
    const int comp = w.data->hardware[hardware].type;
    if (comp >= kComponentCount) return CmdResult::kInvalid;
    if (w.inventory.hardware[hardware] == 0) return CmdResult::kNoStock;
    if (p.state == PcState::kBusy) return CmdResult::kBusy;
    if (quality <= 0.0f) return CmdResult::kFailed;
    --w.inventory.hardware[hardware];
    p.component[comp] = hardware;
    p.wear[comp] = 0.0f;
    if (comp == kCompHdd) clear_installed(w, pc);  // новый диск — без игр
    if (p.state == PcState::kEmpty) {
        if (detail::pc_complete(w, pc)) {
            p.state = PcState::kIdle;
            p.broken_component = 0xFF;
            detail::events_on_action(w, kActPcBought, pc);
        }
    } else {
        fix_broken(w, pc, comp);
    }
    return CmdResult::kOk;
}

CmdResult cmd_sell_component(World& w, uint8_t pc, Component comp) {
    if (w.clock.phase == Phase::kGameOver) return CmdResult::kWrongPhase;
    if (!valid_pc(w, pc) || comp >= kComponentCount) return CmdResult::kInvalid;
    Pc& p = w.pcs[pc];
    if (p.component[comp] == kNone) return CmdResult::kInvalid;
    if (p.state == PcState::kBusy || p.visitor != kNoVisitor) return CmdResult::kBusy;
    detail::add_revenue(w, kRevSales, component_sell_price(w, pc, comp));  // GDD §7.9
    p.component[comp] = kNone;
    p.wear[comp] = 0.0f;
    if (comp == kCompHdd) clear_installed(w, pc);
    if (comp <= kCompKeyboard) {
        p.state = PcState::kEmpty;
        p.broken_component = 0xFF;
        p.running_game = kNone;
    } else {
        fix_broken(w, pc, comp);
    }
    return CmdResult::kOk;
}

CmdResult cmd_remove_virus(World& w, uint8_t pc, float quality) {
    if (w.clock.phase == Phase::kGameOver) return CmdResult::kWrongPhase;
    if (!valid_pc(w, pc) || w.pcs[pc].virus == 0) return CmdResult::kInvalid;
    if (quality <= 0.0f) return CmdResult::kFailed;
    w.pcs[pc].virus = 0;
    return CmdResult::kOk;
}

CmdResult cmd_set_pc_power(World& w, uint8_t pc, bool on) {
    if (w.clock.phase == Phase::kGameOver) return CmdResult::kWrongPhase;
    if (!valid_pc(w, pc)) return CmdResult::kInvalid;
    Pc& p = w.pcs[pc];
    if (on) {
        if (p.state != PcState::kOff) return CmdResult::kInvalid;
        p.state = detail::pc_complete(w, pc) ? PcState::kIdle : PcState::kEmpty;
        return CmdResult::kOk;
    }
    if (p.state == PcState::kBusy || p.visitor != kNoVisitor) return CmdResult::kBusy;
    if (p.state != PcState::kIdle && p.state != PcState::kHung) return CmdResult::kInvalid;
    p.state = PcState::kOff;  // выключение снимает и зависание
    p.running_game = kNone;
    return CmdResult::kOk;
}

CmdResult cmd_install(World& w, uint8_t pc, Index game, bool pirate, float quality) {
    if (w.clock.phase == Phase::kGameOver) return CmdResult::kWrongPhase;
    if (!valid_pc(w, pc) || w.pcs[pc].state == PcState::kEmpty) return CmdResult::kInvalid;
    if (!game_released(w, game) || pc_has_game(w, pc, game)) return CmdResult::kInvalid;
    if (static_cast<int64_t>(pc_free_hdd_mb(w, pc)) < w.data->games[game].req_hdd_mb) {
        return CmdResult::kNoSpace;  // GDD §7.4
    }
    if (pirate ? ((w.library.pirate[game >> 5] >> (game & 31)) & 1u) == 0 : free_licenses(w, game) <= 0) {
        return CmdResult::kNoLicense;
    }
    if (quality <= 0.0f) return CmdResult::kFailed;
    Pc& p = w.pcs[pc];
    p.installed[game >> 5] |= 1u << (game & 31);
    if (pirate) p.installed_pirate[game >> 5] |= 1u << (game & 31);
    detail::invalidate_caches(w);
    if (p.visitor != kNoVisitor) {
        const Visitor& v = w.visitors[p.visitor];
        if (v.request == Request::kGame && v.request_arg == game) detail::resolve_request(w, p.visitor);
    }
    detail::events_on_action(w, kActGameInstalled, game);
    if (pirate) detail::events_on_action(w, kActPirateInstalled, game);
    return CmdResult::kOk;
}

CmdResult cmd_uninstall(World& w, uint8_t pc, Index game) {
    if (w.clock.phase == Phase::kGameOver) return CmdResult::kWrongPhase;
    if (!valid_pc(w, pc) || !pc_has_game(w, pc, game)) return CmdResult::kInvalid;
    Pc& p = w.pcs[pc];
    if (p.visitor != kNoVisitor && w.visitors[p.visitor].game == game) return CmdResult::kBusy;
    p.installed[game >> 5] &= ~(1u << (game & 31));
    p.installed_pirate[game >> 5] &= ~(1u << (game & 31));
    detail::invalidate_caches(w);
    return CmdResult::kOk;
}

}  // namespace core
