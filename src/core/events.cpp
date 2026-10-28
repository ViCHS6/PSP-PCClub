// Система событий: допуск, триггеры, планировщик, эффекты, ответ игрока
// (GDD §8.6; правила — блок «СИСТЕМА СОБЫТИЙ» в core_api.h, раскладка — data_pak.md §5).
#include "core/core_api.h"
#include "core/formulas_api.h"
#include "core/sim_internal.h"

namespace core {

static_assert(detail::kPrivActiveEvents == kMaxActiveEvents, "WorldPrivate: слоты событий");
static_assert(kMaxActiveEvents < 0xFF, "индекс ActiveEvent в uint8_t");

namespace {

// build_data.py пускает chance в chance до глубины 4; запас — на случай битых данных.
constexpr int kMaxEffectDepth = 8;

bool bit_get(const uint32_t* bits, int i) { return (bits[i >> 5] >> (i & 31)) & 1u; }

void bit_put(uint32_t* bits, int i, bool on) {
    if (on) {
        bits[i >> 5] |= 1u << (i & 31);
    } else {
        bits[i >> 5] &= ~(1u << (i & 31));
    }
}

int32_t clampi(int32_t x, int32_t lo, int32_t hi) { return x < lo ? lo : (x > hi ? hi : x); }

enum class Attempt : uint8_t {
    kFired,     // показано игроку
    kRejected,  // once, conditions или бросок шанса
    kWait,      // кулдаун, нет постоянного клиента в клубе, нет слота active
};

// Постоянный клиент, в event_pool которого есть событие; kNone — событие ничьё.
Index event_owner(const GameData& d, Index ev) {
    for (int r = 0; r < d.regular_count; ++r) {
        const RegularRec& rec = d.regulars[r];
        for (int i = 0; i < rec.event_pool_count; ++i) {
            const int at = rec.event_pool_first + i;
            if (at < d.list_count && d.lists[at] == ev) return static_cast<Index>(r);
        }
    }
    return kNone;
}

// Слот постоянного клиента, если он в клубе и ещё не уходит.
uint8_t regular_slot(const World& w, Index regular) {
    for (int i = 0; i < kMaxVisitors; ++i) {
        const Visitor& v = w.visitors[i];
        if (v.regular == regular && v.state != VisitorState::kNone && v.state != VisitorState::kLeaving) {
            return static_cast<uint8_t>(i);
        }
    }
    return kNoVisitor;
}

int free_active_slot(const World& w) {
    for (int i = 0; i < kMaxActiveEvents; ++i) {
        if (w.events.active[i].event == kNone) return i;
    }
    return -1;
}

// GDD §8.6: chance = base + Σ coef · var.
float event_chance(const World& w, const EventRec& e) {
    const GameData& d = *w.data;
    float p = e.chance_base;
    for (int i = 0; i < e.chance_count; ++i) {
        const int at = e.chance_first + i;
        if (at >= d.chance_term_count) break;
        const ChanceTermRec& t = d.chance_terms[at];
        p += t.coef * detail::cond_var_value(w, t.var, t.arg);
    }
    return p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p);
}

bool roll(World& w, float p) {
    if (p >= 1.0f) return true;
    if (p <= 0.0f) return false;
    return rng_chance(w.rng, p);
}

// Допуск без броска (core_api.h). visitor — кому показать (постоянный клиент) или kNoVisitor.
Attempt admit(const World& w, Index ev, uint8_t* visitor) {
    const EventRec& e = w.data->events[ev];
    if (e.once && bit_get(w.events.fired_once, ev)) return Attempt::kRejected;
    if (!detail::conds_hold(w, e.cond_first, e.cond_count)) return Attempt::kRejected;
    if (w.clock.day_index < w.events.cooldown_until[ev]) return Attempt::kWait;
    *visitor = kNoVisitor;
    const Index owner = event_owner(*w.data, ev);
    if (owner != kNone) {
        *visitor = regular_slot(w, owner);
        if (*visitor == kNoVisitor) return Attempt::kWait;
    }
    if (free_active_slot(w) < 0) return Attempt::kWait;
    return Attempt::kFired;
}

// Показать событие игроку: слот active, письмо, кулдаун, уведомление. as_mail — письмом при
// любом present.kind (отладка: другие способы показа UI пока не умеет).
void present(World& w, Index ev, uint8_t visitor, bool as_mail = false) {
    const EventRec& e = w.data->events[ev];
    const int slot = free_active_slot(w);
    ActiveEvent& a = w.events.active[slot];
    a = ActiveEvent{};
    a.event = ev;
    a.visitor = visitor;
    a.date = w.clock.date;
    a.minute = w.clock.minute;
    w.priv.ev_visitor_serial[slot] = visitor != kNoVisitor ? w.visitors[visitor].serial : 0;
    if (as_mail || e.present_kind == kPresentMail) {
        // add_mail сдвигает ActiveEvent::mail остальных слотов; наш ещё 0xFF.
        a.mail = static_cast<uint8_t>(detail::add_mail(w, MailKind::kEvent, ev, 0));
    }
    w.events.cooldown_until[ev] =
        static_cast<uint16_t>(clampi(w.clock.day_index + e.cooldown_days, 0, 0xFFFF));
    if (e.once) bit_put(w.events.fired_once, ev, true);
    detail::push_notice(w, NoticeKind::kEventPresented, static_cast<uint8_t>(slot), visitor, kNoPc, ev);
}

Attempt try_fire(World& w, Index ev) {
    uint8_t visitor = kNoVisitor;
    const Attempt adm = admit(w, ev, &visitor);
    if (adm != Attempt::kFired) return adm;
    if (!roll(w, event_chance(w, w.data->events[ev]))) return Attempt::kRejected;
    present(w, ev, visitor);
    return Attempt::kFired;
}

// Час события date/scheduled без явного часа.
int event_hour(uint8_t hour) { return hour == 0xFF ? kEventDefaultHour : hour; }

// daily_roll: броски всех допущенных, срабатывает одно по weight (core_api.h).
void daily_roll(World& w, int hour) {
    const GameData& d = *w.data;
    uint32_t won[kMaxEvents / 32] = {};
    int won_count = 0;
    float weight_sum = 0.0f;
    Index last = kNone;
    for (int i = 0; i < d.event_count; ++i) {
        const Index ev = static_cast<Index>(i);
        const EventRec& e = d.events[i];
        if (e.trigger != kTrigDailyRoll || e.trigger_hour != hour) continue;
        uint8_t visitor = kNoVisitor;
        if (admit(w, ev, &visitor) != Attempt::kFired) continue;
        if (!roll(w, event_chance(w, e))) continue;
        bit_put(won, i, true);
        ++won_count;
        weight_sum += e.weight > 0.0f ? e.weight : 0.0f;
        last = ev;
    }
    if (won_count == 0) return;
    Index pick = last;
    if (won_count > 1 && weight_sum > 0.0f) {
        float r = rng_float01(w.rng) * weight_sum;
        for (int i = 0; i < d.event_count; ++i) {
            if (!bit_get(won, i)) continue;
            const float wt = d.events[i].weight > 0.0f ? d.events[i].weight : 0.0f;
            if (r < wt) {
                pick = static_cast<Index>(i);
                break;
            }
            r -= wt;
        }
    }
    uint8_t visitor = kNoVisitor;
    admit(w, pick, &visitor);  // допуск уже проверен; нужен только посетитель
    present(w, pick, visitor);
}

// Обработка наступившего часа (порядок — core_api.h).
void process_hour(World& w, int hour) {
    const GameData& d = *w.data;
    const DayNum today = w.clock.date;

    for (ScheduledEvent& s : w.events.scheduled) {
        if (s.event == kNone || s.event >= d.event_count) continue;
        if (s.date > today || (s.date == today && s.hour > hour)) continue;
        if (try_fire(w, s.event) != Attempt::kWait) s.event = kNone;
    }

    for (int i = 0; i < d.event_count; ++i) {
        const EventRec& e = d.events[i];
        if (e.trigger == kTrigDate && e.trigger_date == today && event_hour(e.trigger_hour) == hour) {
            try_fire(w, static_cast<Index>(i));
        }
    }

    for (int i = 0; i < d.event_count; ++i) {
        const EventRec& e = d.events[i];
        if (e.trigger != kTrigOnState) continue;
        if (!detail::conds_hold(w, e.cond_first, e.cond_count)) {
            bit_put(w.priv.ev_state_done, i, false);
            continue;
        }
        if (bit_get(w.priv.ev_state_done, i)) continue;
        if (try_fire(w, static_cast<Index>(i)) != Attempt::kWait) bit_put(w.priv.ev_state_done, i, true);
    }

    daily_roll(w, hour);
}

// --- Эффекты

void change_money(World& w, int32_t amount) {
    if (amount > 0) {
        detail::add_revenue(w, kRevEvents, amount);
    } else if (amount < 0) {
        detail::add_expense(w, kExpEvents, -amount);
        detail::cover_debt(w);  // штраф — обязательный расход (§5.4)
    }
}

void add_modifier(World& w, uint8_t target, Index arg, float mult, int32_t days) {
    if (target >= kModTargetCount || mult <= 0.0f || days <= 0) return;
    Modifier* slot = nullptr;
    for (Modifier& m : w.modifiers) {
        if (m.target == kModTargetCount) {
            slot = &m;
            break;
        }
        if (slot == nullptr || m.until < slot->until) slot = &m;  // вытесняем истекающий раньше всех
    }
    *slot = Modifier{};
    slot->target = target;
    slot->arg = arg;
    slot->mult = mult;
    slot->until = w.clock.date + days - 1;  // считая сегодняшний день
    detail::invalidate_caches(w);
}

void spawn_from_event(World& w, const EffectRec& f) {
    const GameData& d = *w.data;
    if (w.clock.phase != Phase::kOpen && w.clock.phase != Phase::kNight) return;
    if (f.sub == 0) {
        detail::spawn_visitor(w, f.ref, kNone);
        return;
    }
    if (f.ref >= d.regular_count) return;
    RegularState& s = w.regulars[f.ref];
    if (s.in_club) return;
    if (!s.unlocked) {
        s.unlocked = 1;
        detail::push_notice(w, NoticeKind::kRegularUnlocked, 0, kNoVisitor, kNoPc, f.ref);
        detail::events_on_action(w, kActRegularUnlocked, f.ref);
    }
    detail::spawn_visitor(w, d.regulars[f.ref].archetype, f.ref);
}

void schedule(World& w, Index ev, int32_t in_days, uint8_t hour) {
    if (ev >= w.data->event_count) return;
    if (hour == 0xFF) hour = w.data->events[ev].trigger_hour;
    for (ScheduledEvent& s : w.events.scheduled) {
        if (s.event != kNone) continue;
        s = ScheduledEvent{};
        s.event = ev;
        s.hour = static_cast<uint8_t>(event_hour(hour));
        s.date = w.clock.date + (in_days > 0 ? in_days : 0);
        return;
    }
}

// uninstall: 0 — пиратские, 1 — все, 2 — игра ref. Запущенную сейчас игру не трогаем.
void uninstall(World& w, const EffectRec& f) {
    const GameData& d = *w.data;
    for (int pc = 0; pc < kMaxPcs; ++pc) {
        Pc& p = w.pcs[pc];
        const Index playing = p.visitor != kNoVisitor ? w.visitors[p.visitor].game : kNone;
        for (int g = 0; g < d.game_count; ++g) {
            if (!bit_get(p.installed, g) || g == playing) continue;
            const bool hit = f.sub == 0 ? bit_get(p.installed_pirate, g) : (f.sub == 1 || g == f.ref);
            if (!hit) continue;
            bit_put(p.installed, g, false);
            bit_put(p.installed_pirate, g, false);
        }
    }
    detail::invalidate_caches(w);
}

bool damageable(const World& w, int pc) {
    if (pc >= w.club.seats) return false;
    const PcState s = w.pcs[pc].state;
    return s != PcState::kEmpty && s != PcState::kBroken;
}

void damage_pc(World& w, const EffectRec& f) {
    int pc = f.ref;
    if (f.ref == kNone) {
        int n = 0;
        for (int i = 0; i < w.club.seats; ++i) n += damageable(w, i) ? 1 : 0;
        if (n == 0) return;
        int k = rng_range(w.rng, 0, n - 1);
        for (int i = 0; i < w.club.seats; ++i) {
            if (damageable(w, i) && k-- == 0) {
                pc = i;
                break;
            }
        }
    }
    if (pc >= kMaxPcs || !damageable(w, pc)) return;
    const Pc& p = w.pcs[pc];
    int comp = f.arg;
    if (f.arg == kNone) {
        int n = 0;
        for (int k = 0; k < kComponentCount; ++k) n += p.component[k] != kNone ? 1 : 0;
        int k = rng_range(w.rng, 0, n - 1);  // n ≥ 1: собранный ПК
        for (int c = 0; c < kComponentCount; ++c) {
            if (p.component[c] != kNone && k-- == 0) {
                comp = c;
                break;
            }
        }
    }
    if (comp >= kComponentCount || p.component[comp] == kNone) return;
    detail::break_pc(w, static_cast<uint8_t>(pc), comp);
}

void add_item(World& w, const EffectRec& f) {
    const GameData& d = *w.data;
    uint16_t* stock = nullptr;
    if (f.sub == 0 && f.ref < d.hardware_count) stock = &w.inventory.hardware[f.ref];
    if (f.sub == 1 && f.ref < d.goods_count) stock = &w.inventory.goods[f.ref];
    if (stock != nullptr) *stock = static_cast<uint16_t>(clampi(*stock + f.ival, 0, 0xFFFF));
}

void unlock(World& w, const EffectRec& f) {
    const GameData& d = *w.data;
    if (f.sub == 1) {
        if (f.ref >= d.game_count) return;
        w.library.licenses[f.ref] = static_cast<uint8_t>(clampi(w.library.licenses[f.ref] + 1, 0, 255));
        detail::invalidate_caches(w);
        return;
    }
    if (f.ref >= d.upgrade_count) return;
    const UpgradeRec& r = d.upgrades[f.ref];
    if (r.kind == kUpgAd) {
        // Как покупка рекламы (commands.cpp): продлевает срок.
        DayNum& until = w.priv.ad_until[f.ref];
        if (until < w.clock.date - 1) until = w.clock.date - 1;
        until += r.days > 0 ? r.days : 1;
    } else if (!upgrade_owned(w, f.ref)) {
        detail::own_upgrade(w, f.ref);
    }
}

void apply_effects(World& w, uint16_t first, uint8_t count, int depth);

void apply_effect(World& w, const EffectRec& f, int depth) {
    const GameData& d = *w.data;
    switch (f.type) {
        case kEffMoney:
            change_money(w, f.sub == 0 ? f.ival
                                       : formula::money_round(f.fval * detail::cond_var_value(
                                                                            w, static_cast<uint8_t>(f.ref), f.arg)));
            break;
        case kEffRep: w.economy.rep = formula::rep_clamp(w.economy.rep + f.fval); break;
        case kEffFlag:
            if (f.ref < kMaxFlags) bit_put(w.events.flags, f.ref, f.ival != 0);
            break;
        case kEffRelation:
            if (f.ref < d.regular_count) {
                RegularState& s = w.regulars[f.ref];
                s.relation = static_cast<int8_t>(clampi(s.relation + f.ival, -100, 100));
            }
            break;
        case kEffModifier: add_modifier(w, f.sub, f.arg, f.fval, f.ival); break;
        case kEffSpawnVisitor: spawn_from_event(w, f); break;
        case kEffScheduleEvent: schedule(w, f.ref, f.ival, f.hour); break;
        case kEffUninstall: uninstall(w, f); break;
        case kEffDamagePc: damage_pc(w, f); break;
        case kEffItem: add_item(w, f); break;
        case kEffUnlock: unlock(w, f); break;
        case kEffChance:
            if (depth < kMaxEffectDepth && roll(w, f.fval)) apply_effects(w, f.child_first, f.child_count, depth + 1);
            break;
        case kEffStartMinigame:
            detail::push_notice(w, NoticeKind::kEventMinigame, f.sub, kNoVisitor, kNoPc, f.ref);
            break;
        default: break;
    }
}

void apply_effects(World& w, uint16_t first, uint8_t count, int depth) {
    if (static_cast<int>(first) + count > w.data->effect_count) return;
    for (int i = 0; i < count; ++i) apply_effect(w, w.data->effects[first + i], depth);
}

const ChoiceRec* choice_of(const World& w, uint8_t active, uint8_t choice) {
    if (active >= kMaxActiveEvents) return nullptr;
    const Index ev = w.events.active[active].event;
    if (ev == kNone || ev >= w.data->event_count) return nullptr;
    const EventRec& e = w.data->events[ev];
    if (choice >= e.choice_count || e.choice_first + choice >= w.data->choice_count) return nullptr;
    return &w.data->choices[e.choice_first + choice];
}

}  // namespace

namespace detail {

void events_on_action(World& w, ActionId action, Index) {
    const GameData& d = *w.data;
    for (int i = 0; i < d.event_count; ++i) {
        const EventRec& e = d.events[i];
        if (e.trigger == kTrigOnAction && e.trigger_action == action) try_fire(w, static_cast<Index>(i));
    }
}

bool events_force(World& w, Index ev) {
    if (ev >= w.data->event_count || free_active_slot(w) < 0) return false;
    const Index owner = event_owner(*w.data, ev);
    present(w, ev, owner != kNone ? regular_slot(w, owner) : kNoVisitor, true);
    return true;
}

void events_tick(World& w) {
    // Посетитель события ушёл, слот занял другой — событие больше ни к кому не привязано.
    for (int i = 0; i < kMaxActiveEvents; ++i) {
        ActiveEvent& a = w.events.active[i];
        if (a.event == kNone || a.visitor == kNoVisitor) continue;
        const Visitor& v = w.visitors[a.visitor];
        if (v.state == VisitorState::kNone || v.serial != w.priv.ev_visitor_serial[i]) a.visitor = kNoVisitor;
    }

    const int hour = w.clock.minute / 60;
    if (w.priv.ev_hour_date == w.clock.date && w.priv.ev_hour == hour) return;
    w.priv.ev_hour_date = w.clock.date;
    w.priv.ev_hour = static_cast<uint8_t>(hour);
    process_hour(w, hour);
}

}  // namespace detail

CmdResult cmd_event_choose(World& w, uint8_t active, uint8_t choice) {
    if (w.clock.phase == Phase::kGameOver) return CmdResult::kWrongPhase;
    const ChoiceRec* ch = choice_of(w, active, choice);
    if (ch == nullptr) return CmdResult::kInvalid;
    if (!detail::conds_hold(w, ch->req_first, ch->req_count)) return CmdResult::kInvalid;

    const uint8_t mail = w.events.active[active].mail;
    w.events.active[active] = ActiveEvent{};  // слот свободен до эффектов: они могут показать новое
    if (mail < w.mailbox.count) {
        Mail& m = w.mailbox.mail[mail];
        m.answered = 1;
        m.choice = choice;
        if (!m.read) {
            m.read = 1;
            if (w.mailbox.unread > 0) --w.mailbox.unread;
        }
    }
    apply_effects(w, ch->effect_first, ch->effect_count, 0);
    return CmdResult::kOk;
}

bool event_choice_available(const World& w, uint8_t active, uint8_t choice) {
    const ChoiceRec* ch = choice_of(w, active, choice);
    return ch != nullptr && detail::conds_hold(w, ch->req_first, ch->req_count);
}

}  // namespace core
