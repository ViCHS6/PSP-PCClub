// Посетители: появление, конечный автомат, час игры, уход, решение у стойки
// (GDD §6.1, §7.1–7.3, §7.5, §7.13; контракт — core_api.h).
#include "core/core_api.h"
#include "core/formulas_api.h"
#include "core/sim_internal.h"

namespace core {

namespace {

// Чисел ниже нет в GDD — предложены в отчёте T022 для переноса в данные.
constexpr float kRegularVisitShare = 0.15f;  // доля визитов архетипа, которую занимают открытые постоянные
constexpr float kHelpPerHour = 0.05f;        // p просьбы «как тут…» за час при skill = 0
constexpr float kNoisyEnvironment = -1.0f;   // environment за каждого шумного соседа
constexpr float kSmokerEnvironment = -1.0f;  // environment за каждого курящего в зале
constexpr int kNightPackageHours = 8;        // 0:00–8:00: ночной пакет в пересчёте на час для §7.3

const ArchetypeRec& arch_of(const World& w, const Visitor& v) { return w.data->archetypes[v.archetype]; }

float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

int queue_index(const World& w, uint8_t v) {
    for (int i = 0; i < w.queue_len; ++i) {
        if (w.queue[i] == v) return i;
    }
    return -1;
}

void queue_remove(World& w, uint8_t v) {
    const int idx = queue_index(w, v);
    if (idx < 0) return;
    for (int i = idx; i + 1 < w.queue_len; ++i) w.queue[i] = w.queue[i + 1];
    --w.queue_len;
    if (idx == 0 && w.queue_len > 0) {
        detail::push_notice(w, NoticeKind::kVisitorAtDesk, 0, w.queue[0]);
    }
}

// Идущие к стойке: под них место в очереди уже занято.
int heading_to_desk(const World& w) {
    int n = 0;
    for (const Visitor& v : w.visitors) {
        if (v.state == VisitorState::kEntering || v.state == VisitorState::kToDesk) ++n;
    }
    return n;
}

// g* — из всех вышедших игр с весом pop · aff (§7.2).
Index pick_market_game(World& w, Index archetype) {
    const GameData& d = *w.data;
    float sum = 0.0f;
    for (int g = 0; g < d.game_count; ++g) {
        if (game_released(w, static_cast<Index>(g))) sum += detail::value_for(w, archetype, static_cast<Index>(g));
    }
    if (sum <= 0.0f) return kNone;
    float r = rng_float01(w.rng) * sum;
    Index last = kNone;
    for (int g = 0; g < d.game_count; ++g) {
        const Index gi = static_cast<Index>(g);
        if (!game_released(w, gi)) continue;
        const float v = detail::value_for(w, archetype, gi);
        if (v <= 0.0f) continue;
        last = gi;
        if (r < v) return gi;
        r -= v;
    }
    return last;
}

// Открытый постоянный клиент, сегодня ещё не приходивший.
bool regular_can_come(const World& w, int r, Index archetype) {
    const RegularState& s = w.regulars[r];
    return s.unlocked && !s.in_club && w.data->regulars[r].archetype == archetype &&
           w.priv.reg_last_visit[r] != w.clock.date;
}

// Открытый постоянный клиент этого архетипа вместо анонимного посетителя.
Index pick_regular(World& w, Index archetype) {
    const GameData& d = *w.data;
    int n = 0;
    for (int r = 0; r < d.regular_count; ++r) {
        if (regular_can_come(w, r, archetype)) ++n;
    }
    if (n == 0 || !rng_chance(w.rng, kRegularVisitShare)) return kNone;
    int k = rng_range(w.rng, 0, n - 1);
    for (int r = 0; r < d.regular_count; ++r) {
        if (regular_can_come(w, r, archetype) && k-- == 0) return static_cast<Index>(r);
    }
    return kNone;
}

void release_slot(World& w, uint8_t i) {
    Visitor& v = w.visitors[i];
    if (v.regular != kNone) w.regulars[v.regular].in_club = 0;
    v = Visitor{};
}

// Обслуженный анонимный посетитель — «примеривается» к закрытому постоянному клиенту своего
// архетипа, у которого выполнены условия unlock (§7.5).
void regular_candidate(World& w, const Visitor& v) {
    const GameData& d = *w.data;
    for (int r = 0; r < d.regular_count; ++r) {
        const RegularRec& rec = d.regulars[r];
        RegularState& s = w.regulars[r];
        if (s.unlocked || rec.archetype != v.archetype) continue;
        if (!detail::conds_hold(w, rec.unlock_first, rec.unlock_count)) continue;
        if (s.visits < 0xFFFF) ++s.visits;
        s.sat_sum += v.sat;
        const float avg = s.sat_sum / static_cast<float>(s.visits);
        if (formula::regular_eligible(s.visits, avg) &&
            rng_chance(w.rng, formula::regular_convert_prob())) {
            s.unlocked = 1;
            detail::push_notice(w, NoticeKind::kRegularUnlocked, 0, kNoVisitor, kNoPc,
                                static_cast<Index>(r));
            detail::events_on_action(w, kActRegularUnlocked, static_cast<Index>(r));
        }
        return;
    }
}

void add_relation(RegularState& s, float delta) {
    const int32_t r = static_cast<int32_t>(s.relation) + formula::money_round(delta);
    s.relation = static_cast<int8_t>(r < -100 ? -100 : (r > 100 ? 100 : r));
}

float comfort_sum(const World& w, uint8_t pc) {
    float sum = 0.0f;
    for (int k = 0; k < kComponentCount; ++k) {
        const Index hw = w.pcs[pc].component[k];
        if (hw != kNone) sum += w.data->hardware[hw].comfort;
    }
    if (const UpgradeRec* r = detail::owned_upgrade_of_kind(w, kUpgRenovation)) sum += r->value;
    return sum;
}

float environment_for(const World& w, uint8_t self) {
    float env = 0.0f;
    if (const UpgradeRec* r = detail::owned_upgrade_of_kind(w, kUpgSoundproof)) env += r->value;
    for (int i = 0; i < kMaxVisitors; ++i) {
        const Visitor& o = w.visitors[i];
        if (i == self || o.state != VisitorState::kPlaying) continue;
        const uint32_t flags = arch_of(w, o).flags;
        if (flags & kArchNoisy) env += kNoisyEnvironment;
        if (flags & kArchSmoker) env += kSmokerEnvironment;
    }
    return env;
}

void roll_requests(World& w, uint8_t i) {
    const GameData& d = *w.data;
    Visitor& v = w.visitors[i];
    const ArchetypeRec& a = arch_of(w, v);
    // Буфет (§7.13): товар по весу, затем бросок p_buy с его наценкой.
    float sum = 0.0f;
    for (int g = 0; g < d.goods_count; ++g) sum += d.goods[g].weight;
    if (sum > 0.0f && a.hunger > 0.0f) {
        float r = rng_float01(w.rng) * sum;
        int pick = d.goods_count - 1;
        for (int g = 0; g < d.goods_count; ++g) {
            if (r < d.goods[g].weight) {
                pick = g;
                break;
            }
            r -= d.goods[g].weight;
        }
        const int32_t price = w.economy.goods_price[pick];
        const float markup = static_cast<float>(price) / static_cast<float>(d.goods[pick].buy_price);
        const float p = formula::buy_prob(a.hunger, markup);
        if (p > 0.0f && rng_chance(w.rng, p) && v.money >= price) {
            detail::set_request(w, i, Request::kFood, static_cast<Index>(pick));
            return;
        }
    }
    const float p_help = kHelpPerHour * (1.0f - v.skill);
    if (p_help > 0.0f && rng_chance(w.rng, p_help)) detail::set_request(w, i, Request::kHelp, kNone);
}

// Час игры посетителя (§7.5).
void visitor_hour(World& w, uint8_t i) {
    Visitor& v = w.visitors[i];
    const ArchetypeRec& a = arch_of(w, v);
    const Pc& p = w.pcs[v.pc];
    float perf_term = 0.0f;
    if (p.state == PcState::kBusy && v.game != kNone) {
        perf_term = formula::perf_sat_per_hour(pc_perf(w, v.pc, v.game), (a.flags & kArchLikesGraphics) != 0);
    }
    int unresolved = 0;
    float extra = 0.0f;
    if (v.request == Request::kFood && w.inventory.goods[v.request_arg] == 0) {
        extra = formula::goods_missing_sat_penalty();  // «а есть...?» — товара нет (§7.13)
    } else if (v.request != Request::kNone) {
        unresolved = 1;
    }
    const float delta = formula::sat_delta_hour(perf_term, formula::comfort_clamp(comfort_sum(w, v.pc)),
                                                formula::environment_clamp(environment_for(w, i)),
                                                unresolved, w.priv.vis_resolved[i],
                                                w.priv.vis_event_sat[i]) +
                        extra;
    v.sat = clampf(v.sat + delta, 0.0f, 100.0f);
    w.priv.vis_resolved[i] = 0;

    // Еда, помощь и игра ждут не дольше часа: штраф взят, просьба забыта.
    if ((v.request == Request::kFood || v.request == Request::kHelp || v.request == Request::kGame) &&
        v.request_minutes >= 60) {
        v.request = Request::kNone;
        v.request_arg = kNone;
    }
    if (v.request == Request::kNone) roll_requests(w, i);
    if (formula::leaves_early(v.sat)) detail::start_leave(w, i, LeaveReason::kUnhappy);
}

// Оплаченное время вышло: продление или уход (§7.5).
void paid_time_over(World& w, uint8_t i) {
    Visitor& v = w.visitors[i];
    const int32_t price = w.economy.price_hour;
    const float p = formula::extend_prob(v.sat, arch_of(w, v).extend, v.money, price);
    if (p > 0.0f && rng_chance(w.rng, p)) {
        v.money -= price;
        v.paid_total += price;
        v.paid_minutes_left = 60;
        if (v.plan_hours < 255) ++v.plan_hours;
        detail::add_revenue(w, kRevHours, price);
        ++w.stats.today.extended;
        detail::push_notice(w, NoticeKind::kVisitorExtended, 0, i, v.pc, kNone, price);
        return;
    }
    detail::start_leave(w, i, LeaveReason::kDone);
}

void playing_minute(World& w, uint8_t i) {
    Visitor& v = w.visitors[i];
    const Pc& p = w.pcs[v.pc];
    if (p.state == PcState::kBusy && v.game != kNone &&
        w.data->games[v.game].net == kNetOnlineLow && w.club.internet_level == 1) {
        w.economy.online_minutes_today += 1.0f;  // диал-ап: поминутно (§7.14)
    }
    if (v.request != Request::kNone && v.request_minutes < 0xFFFF) ++v.request_minutes;
    if (v.paid_minutes_left > 0) --v.paid_minutes_left;
    if (++w.priv.vis_hour_min[i] >= 60) {
        w.priv.vis_hour_min[i] = 0;
        visitor_hour(w, i);
        if (v.state != VisitorState::kPlaying) return;
    }
    // Ночной пакет — до 8:00, уход в start_morning.
    if (v.paid_minutes_left == 0 && !w.priv.vis_night[i]) paid_time_over(w, i);
}

void update_visitor(World& w, uint8_t i) {
    Visitor& v = w.visitors[i];
    if (v.state_minutes < 0xFFFF) ++v.state_minutes;
    switch (v.state) {
        case VisitorState::kNone: break;
        case VisitorState::kEntering:
            if (v.state_minutes < kTransitMinutes) break;
            v.plan_hours = static_cast<uint8_t>(
                formula::plan_hours(w.priv.vis_hours_roll[i], v.money, w.economy.price_hour));
            if (v.plan_hours == 0) {
                detail::start_leave(w, i, LeaveReason::kNoMoney);  // не дошёл до стойки (§7.2)
                break;
            }
            v.state = VisitorState::kToDesk;
            v.state_minutes = 0;
            break;
        case VisitorState::kToDesk:
            if (v.state_minutes < kTransitMinutes) break;
            if (w.queue_len >= kMaxQueue) {
                detail::start_leave(w, i, LeaveReason::kQueueFull);
                break;
            }
            v.state = VisitorState::kWaiting;
            v.state_minutes = 0;
            w.queue[w.queue_len++] = i;
            if (w.queue_len == 1) detail::push_notice(w, NoticeKind::kVisitorAtDesk, 0, i);
            break;
        case VisitorState::kWaiting:
            v.sat = clampf(v.sat + formula::queue_sat_per_minute(), 0.0f, 100.0f);  // GDD §7.3
            if (v.patience > 0) --v.patience;
            if (v.patience == 0) detail::start_leave(w, i, LeaveReason::kPatience);
            break;
        case VisitorState::kToSeat:
            if (v.state_minutes < kTransitMinutes) break;
            v.state = VisitorState::kPlaying;
            v.state_minutes = 0;
            w.priv.vis_hour_min[i] = 0;
            detail::push_notice(w, NoticeKind::kVisitorSeated, 0, i, v.pc);
            break;
        case VisitorState::kPlaying: playing_minute(w, i); break;
        case VisitorState::kLeaving:
            if (v.state_minutes >= kTransitMinutes) release_slot(w, i);
            break;
    }
}

}  // namespace

namespace detail {

uint8_t spawn_visitor(World& w, Index archetype, Index regular) {
    const GameData& d = *w.data;
    if (archetype >= d.archetype_count) return kNoVisitor;
    int slot = -1;
    if (w.queue_len + heading_to_desk(w) < kMaxQueue) {
        for (int i = 0; i < kMaxVisitors; ++i) {
            if (w.visitors[i].state == VisitorState::kNone) {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0) {
        ++w.stats.today.left[static_cast<int>(LeaveReason::kQueueFull)];  // развернулся у входа
        return kNoVisitor;
    }
    const uint8_t i = static_cast<uint8_t>(slot);
    const ArchetypeRec& a = d.archetypes[archetype];
    Visitor& v = w.visitors[i];
    v = Visitor{};
    v.state = VisitorState::kEntering;
    v.archetype = archetype;
    v.regular = regular;
    v.serial = w.next_serial++;
    if (w.next_serial == 0) w.next_serial = 1;

    // §7.2 — порядок бросков фиксирован.
    v.money = rng_range(w.rng, a.money_min, a.money_max);
    v.patience = static_cast<uint16_t>(rng_range(w.rng, a.patience_min, a.patience_max));
    v.skill = rng_range_f(w.rng, a.skill_min, a.skill_max);
    const int hours_roll = rng_range(w.rng, a.session_hours_min, a.session_hours_max);
    v.model = static_cast<uint8_t>(a.model_count > 1 ? rng_range(w.rng, 0, a.model_count - 1) : 0);
    v.want_game = kNone;
    if (regular != kNone) {
        const RegularRec& rec = d.regulars[regular];
        v.skill = rec.skill;
        for (int m = 0; m < a.model_count; ++m) {
            if (a.models[m] == rec.model) v.model = static_cast<uint8_t>(m);
        }
        if (rec.fav_game != kNone && game_released(w, rec.fav_game)) v.want_game = rec.fav_game;
        w.regulars[regular].in_club = 1;
        w.priv.reg_last_visit[regular] = w.clock.date;
    }
    if (v.want_game == kNone) v.want_game = pick_market_game(w, archetype);
    v.plan_hours = static_cast<uint8_t>(formula::plan_hours(hours_roll, v.money, w.economy.price_hour));
    v.sat = formula::sat_start(0.0f);

    w.priv.vis_hours_roll[i] = static_cast<uint8_t>(hours_roll);
    w.priv.vis_night[i] = 0;
    w.priv.vis_resolved[i] = 0;
    w.priv.vis_hour_min[i] = 0;
    w.priv.vis_event_sat[i] = 0.0f;
    ++w.stats.today.arrived;
    ++w.stats.visits_archetype[archetype];
    push_notice(w, NoticeKind::kVisitorEntered, 0, i);
    return i;
}

void start_leave(World& w, uint8_t i, LeaveReason reason) {
    Visitor& v = w.visitors[i];
    if (v.state == VisitorState::kNone || v.state == VisitorState::kLeaving) return;
    const ArchetypeRec& a = arch_of(w, v);
    const bool served = v.pc != kNoPc;
    queue_remove(w, i);
    if (served && w.pcs[v.pc].visitor == i) free_pc(w, v.pc);

    // Репутация (§7.3, §7.5). Необслуженный ушедший по терпению репутацию только теряет;
    // отказ по цене и закрытие очереди её не меняют.
    float rep = 0.0f;
    if (served) {
        rep = formula::leave_rep_delta(a.rep_weight, v.sat);
        w.stats.today.sat_sum += v.sat;
    } else if (reason == LeaveReason::kNoGame) {
        rep = formula::no_game_rep_penalty();
    } else if (reason == LeaveReason::kPatience) {
        const float d = formula::leave_rep_delta(a.rep_weight, v.sat);
        rep = d < 0.0f ? d : 0.0f;
    }
    w.economy.rep = formula::rep_clamp(w.economy.rep + rep);

    if (v.regular != kNone) {
        RegularState& s = w.regulars[v.regular];
        const float d = formula::relation_delta(v.sat);
        if (served) {
            add_relation(s, d);
            if (s.visits < 0xFFFF) ++s.visits;
            s.sat_sum += v.sat;
        } else if (reason == LeaveReason::kPatience && d < 0.0f) {
            add_relation(s, d);
        }
    } else if (served) {
        regular_candidate(w, v);
    }

    ++w.stats.today.left[static_cast<int>(reason)];
    if (reason == LeaveReason::kPatience || reason == LeaveReason::kUnhappy ||
        reason == LeaveReason::kNoGame) {
        events_on_action(w, kActVisitorLeftAngry, v.archetype);
    }
    v.leave_reason = reason;
    v.state = VisitorState::kLeaving;
    v.state_minutes = 0;
    v.request = Request::kNone;
    v.request_arg = kNone;
    push_notice(w, NoticeKind::kVisitorLeft, static_cast<uint8_t>(reason), i, v.pc);
}

void visitors_tick(World& w) {
    uint32_t fresh = 0;  // появившиеся в этом тике не обновляются
    if (w.clock.phase == Phase::kOpen) {
        for (int a = 0; a < w.data->archetype_count; ++a) {
            const Index ai = static_cast<Index>(a);
            const float p = formula::arrival_prob_per_minute(arrival_rate_now(w, ai));  // GDD §7.1
            if (p <= 0.0f || !rng_chance(w.rng, p)) continue;
            const uint8_t i = spawn_visitor(w, ai, pick_regular(w, ai));
            if (i != kNoVisitor) fresh |= 1u << i;
        }
    }
    for (int i = 0; i < kMaxVisitors; ++i) {
        if (w.visitors[i].state == VisitorState::kNone || (fresh >> i) & 1u) continue;
        update_visitor(w, static_cast<uint8_t>(i));
    }
}

void visitors_close(World& w, bool night) {
    for (int i = 0; i < kMaxVisitors; ++i) {
        const Visitor& v = w.visitors[i];
        if (v.state == VisitorState::kNone) continue;
        const bool seated = v.state == VisitorState::kToSeat || v.state == VisitorState::kPlaying;
        if (night && seated && w.priv.vis_night[i]) continue;  // ночной пакет — остаётся
        start_leave(w, static_cast<uint8_t>(i), LeaveReason::kClosing);
        release_slot(w, static_cast<uint8_t>(i));
    }
    w.queue_len = 0;
}

void visitors_morning(World& w) {
    for (int i = 0; i < kMaxVisitors; ++i) {
        if (w.visitors[i].state == VisitorState::kNone) continue;
        start_leave(w, static_cast<uint8_t>(i), LeaveReason::kDone);
        release_slot(w, static_cast<uint8_t>(i));
    }
    w.queue_len = 0;
}

void set_request(World& w, uint8_t i, Request r, Index arg) {
    Visitor& v = w.visitors[i];
    v.request = r;
    v.request_arg = arg;
    v.request_minutes = 0;
    push_notice(w, NoticeKind::kRequest, static_cast<uint8_t>(r), i, v.pc, arg);
}

void resolve_request(World& w, uint8_t i) {
    Visitor& v = w.visitors[i];
    const Request old = v.request;
    v.request = Request::kNone;
    v.request_arg = kNone;
    v.request_minutes = 0;
    if (w.priv.vis_resolved[i] < 255) ++w.priv.vis_resolved[i];  // +5 в этом часе (§7.5)
    push_notice(w, NoticeKind::kRequestResolved, static_cast<uint8_t>(old), i, v.pc);
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Команды посетителей

ServeResult cmd_serve(World& w, uint8_t vi, uint8_t pc, Tariff tariff) {
    if (w.clock.phase != Phase::kOpen) return ServeResult::kInvalid;
    if (vi >= kMaxVisitors || w.queue_len == 0 || w.queue[0] != vi) return ServeResult::kInvalid;
    Visitor& v = w.visitors[vi];
    if (v.state != VisitorState::kWaiting) return ServeResult::kInvalid;
    const bool night = tariff == Tariff::kNight;
    const Economy& e = w.economy;
    if (night && (detail::owned_upgrade_of_kind(w, kUpgNightTariff) == nullptr || v.money < e.price_night)) {
        return ServeResult::kInvalid;
    }
    if (pc >= w.club.seats) return ServeResult::kInvalid;
    if (w.pcs[pc].state != PcState::kIdle) return ServeResult::kPcUnavailable;
    if (!w.priv.cache_valid) detail::rebuild_caches(w);
    const ArchetypeRec& a = w.data->archetypes[v.archetype];

    // 1. Игра (§7.3.1).
    Index game = kNone;
    bool alt = false;
    if (v.want_game != kNone && detail::game_playable(w, pc, v.want_game)) {
        game = v.want_game;
    } else {
        if (v.want_game != kNone) {
            for (int o = 0; o < w.club.seats; ++o) {
                if (o == pc || w.pcs[o].state != PcState::kIdle) continue;
                if (detail::game_playable(w, static_cast<uint8_t>(o), v.want_game)) return ServeResult::kWrongPc;
            }
        }
        float best = -1.0f;
        for (int g = 0; g < w.data->game_count; ++g) {
            const Index gi = static_cast<Index>(g);
            if (gi == v.want_game || !detail::game_playable(w, pc, gi)) continue;
            const float val = detail::value_for(w, v.archetype, gi);
            if (val > best) {
                best = val;
                game = gi;
            }
        }
        if (game == kNone) return ServeResult::kPcUnavailable;  // на этом ПК не запускается ничего
        alt = true;
        if (!rng_chance(w.rng, a.alt_accept)) {
            detail::start_leave(w, vi, LeaveReason::kNoGame);
            return ServeResult::kLeftNoGame;
        }
    }

    // 2. Цена (§7.3.2).
    int hours = 0;
    if (!night) {
        hours = formula::plan_hours(w.priv.vis_hours_roll[vi], v.money, e.price_hour);
        if (hours == 0) {
            detail::start_leave(w, vi, LeaveReason::kNoMoney);
            return ServeResult::kLeftPrice;
        }
    }
    const float price = night ? static_cast<float>(e.price_night) / static_cast<float>(kNightPackageHours)
                              : static_cast<float>(e.price_hour);
    const float expected = formula::expected_price(static_cast<float>(a.price_expect), w.competitor.active != 0,
                                                   static_cast<float>(w.competitor.price));
    if (!rng_chance(w.rng, formula::price_accept_prob(a.elasticity, price, expected))) {
        detail::start_leave(w, vi, LeaveReason::kPrice);
        return ServeResult::kLeftPrice;
    }

    // 3. Оплата вперёд и место (§7.3.4). Потери в очереди уже в sat.
    v.sat = formula::sat_start(v.sat - 70.0f + formula::price_sat_bonus(price, expected) +
                               (alt ? formula::alt_game_sat_penalty() : 0.0f));
    int32_t pay;
    if (night) {
        pay = e.price_night;
        v.paid_minutes_left = static_cast<uint16_t>(kMinutesPerDay - w.clock.minute + kMorningMinute);
        v.plan_hours = static_cast<uint8_t>(v.paid_minutes_left / 60);
        detail::add_revenue(w, kRevNight, pay);
    } else {
        pay = formula::session_payment(e.price_hour, hours);
        v.paid_minutes_left = static_cast<uint16_t>(hours * 60);
        v.plan_hours = static_cast<uint8_t>(hours);
        detail::add_revenue(w, kRevHours, pay);
    }
    v.money -= pay;
    v.paid_total += pay;
    w.priv.vis_night[vi] = night ? 1 : 0;

    queue_remove(w, vi);
    v.state = VisitorState::kToSeat;
    v.state_minutes = 0;
    v.pc = pc;
    v.game = game;
    Pc& p = w.pcs[pc];
    p.state = PcState::kBusy;
    p.visitor = vi;
    p.running_game = game;
    ++w.stats.today.served;
    if (alt && v.want_game != kNone) detail::set_request(w, vi, Request::kGame, v.want_game);
    return alt ? ServeResult::kSeatedAlt : ServeResult::kSeated;
}

CmdResult cmd_resolve_request(World& w, uint8_t vi) {
    if (w.clock.phase == Phase::kGameOver) return CmdResult::kWrongPhase;
    if (vi >= kMaxVisitors) return CmdResult::kInvalid;
    Visitor& v = w.visitors[vi];
    if (v.state == VisitorState::kNone || v.state == VisitorState::kLeaving) return CmdResult::kInvalid;
    if (v.request == Request::kHelp) {
        detail::resolve_request(w, vi);
        return CmdResult::kOk;
    }
    if (v.request != Request::kFood) return CmdResult::kInvalid;
    const Index g = v.request_arg;
    uint16_t& stock = w.inventory.goods[g];
    if (stock == 0) return CmdResult::kNoStock;
    const int32_t price = w.economy.goods_price[g];
    --stock;
    v.money -= price;
    if (v.money < 0) v.money = 0;
    detail::add_revenue(w, kRevGoods, price);
    detail::resolve_request(w, vi);
    if (stock == 0) {
        detail::push_notice(w, NoticeKind::kGoodsOut, 0, kNoVisitor, kNoPc, g);
        detail::events_on_action(w, kActGoodsOut, g);
    }
    return CmdResult::kOk;
}

}  // namespace core
