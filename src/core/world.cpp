// Мир: инициализация, тик и фазы дня, запросы, уведомления, почта (контракт — core_api.h).
#include <cstring>
#include <new>

#include "core/core_api.h"
#include "core/formulas_api.h"
#include "core/sim_internal.h"

namespace core {

static_assert(detail::kPrivVisitors == kMaxVisitors, "WorldPrivate: слоты посетителей");
static_assert(detail::kPrivPcs == kMaxPcs, "WorldPrivate: слоты ПК");
static_assert(kMaxVisitors < kNoVisitor, "индекс посетителя в uint8_t");

namespace {

bool bit_test(const uint32_t* bits, int i) { return (bits[i >> 5] >> (i & 31)) & 1u; }

int popcount32(uint32_t x) {
    int n = 0;
    for (; x != 0; x &= x - 1) ++n;
    return n;
}

bool in_period(MonthDay md, MonthDay from, MonthDay to) {
    return from <= to ? (md >= from && md <= to) : (md >= from || md <= to);  // через Новый год
}

void update_calendar(World& w) {
    const GameData& d = *w.data;
    Clock& c = w.clock;
    c.weekday = static_cast<uint8_t>(weekday_of(c.date));
    const MonthDay md = month_day_of(c.date);
    c.holidays = 0;
    c.session = 0;
    c.holiday = 0;
    for (int i = 0; i < d.period_count; ++i) {
        const PeriodRec& p = d.periods[i];
        if (!in_period(md, p.from_md, p.to_md)) continue;
        if (p.tag == kPeriodHolidays) c.holidays = 1;
        if (p.tag == kPeriodSession) c.session = 1;
    }
    for (int i = 0; i < d.holiday_count; ++i) {
        if (d.holidays[i] == md) c.holiday = 1;
    }
}

// Популярность без кэша (для const-запросов).
float popularity_fresh(const World& w, Index g) { return game_popularity_today(w, g); }

// L_a по заданной популярности игр (§7.6).
float appeal_from(const World& w, Index a, const float* pop) {
    const GameData& d = *w.data;
    const ArchetypeRec& ar = d.archetypes[a];
    float installed[kMaxGames];
    float released[kMaxGames];
    int ni = 0, nr = 0;
    for (int g = 0; g < d.game_count; ++g) {
        if (!game_released(w, static_cast<Index>(g))) continue;
        const float v = pop[g] * ar.affinity[d.games[g].genre];  // value_a(g)
        released[nr++] = v;
        for (int pc = 0; pc < w.club.seats; ++pc) {
            if (w.pcs[pc].state != PcState::kEmpty && bit_test(w.pcs[pc].installed, g)) {
                installed[ni++] = v;
                break;
            }
        }
    }
    return formula::library_appeal(installed, ni, released, nr);
}

int32_t day_total(const int32_t* v, int n) {
    int32_t s = 0;
    for (int i = 0; i < n; ++i) s += v[i];
    return s;
}

// Регулярные расходы апгрейда — в статью по его виду (§7.14).
Expense expense_of_kind(uint8_t kind) {
    if (kind == kUpgInternet) return kExpInternet;
    if (kind == kUpgStaffAdmin || kind == kUpgStaffGuard || kind == kUpgStaffCleaner) return kExpWages;
    return kExpRent;
}

// 24:00 — итоги дня (core_api.h, tick).
void close_day(World& w) {
    const EconomyRec& e = *w.data->economy;
    Clock& c = w.clock;
    c.minute = 0;
    c.phase = Phase::kClosed;
    const bool night = detail::owned_upgrade_of_kind(w, kUpgNightTariff) != nullptr;
    detail::visitors_close(w, night);

    uint32_t load = 0;
    for (int i = 0; i < kMaxPcs; ++i) load += w.pcs[i].load_minutes_today;
    w.stats.today.load_minutes = static_cast<uint16_t>(load > 0xFFFFu ? 0xFFFFu : load);
    const float open_h = static_cast<float>(w.priv.open_minutes_today) / 60.0f;
    detail::add_expense(w, kExpElectricity,
                        formula::electricity_cost(static_cast<float>(load) / 60.0f, open_h,
                                                  e.electricity_price));  // GDD §7.14
    for (int u = 0; u < w.data->upgrade_count; ++u) {
        if (!upgrade_owned(w, static_cast<Index>(u))) continue;
        const UpgradeRec& r = w.data->upgrades[u];
        int32_t cost = r.cost_day + formula::money_round(static_cast<float>(r.cost_hour) * open_h);
        if (r.kind == kUpgInternet) {
            cost += formula::money_round(r.cost_minute * w.economy.online_minutes_today);  // диал-ап
        }
        detail::add_expense(w, expense_of_kind(r.kind), cost);
    }

    w.economy.rep = formula::rep_after_daily_drift(w.economy.rep);  // GDD §7.15
    DayStats& t = w.stats.today;
    t.money_end = w.economy.money;
    t.debt_end = w.economy.debt;
    t.rep_end = w.economy.rep;
    detail::push_notice(w, NoticeKind::kDayClosed);
}

// 8:00 — утро (core_api.h, tick).
void start_morning(World& w) {
    const GameData& d = *w.data;
    Clock& c = w.clock;
    c.phase = Phase::kMorning;
    c.minute = kMorningMinute;
    detail::visitors_morning(w);

    // Доставка заказов.
    Inventory& inv = w.inventory;
    int kept = 0;
    bool delivered = false;
    for (int i = 0; i < inv.order_count; ++i) {
        const Order& o = inv.orders[i];
        if (o.arrives > c.date) {
            inv.orders[kept++] = o;
            continue;
        }
        delivered = true;
        switch (o.kind) {
            case OrderKind::kHardware: inv.hardware[o.ref] += o.count; break;
            case OrderKind::kGoods: inv.goods[o.ref] += o.count; break;
            case OrderKind::kLicense: {
                const int n = w.library.licenses[o.ref] + o.count;
                w.library.licenses[o.ref] = static_cast<uint8_t>(n > 255 ? 255 : n);
                break;
            }
            case OrderKind::kPirate: w.library.pirate[o.ref >> 5] |= 1u << (o.ref & 31); break;
        }
    }
    inv.order_count = static_cast<uint8_t>(kept);
    if (delivered) {
        detail::push_notice(w, NoticeKind::kDelivery);
        detail::add_mail(w, MailKind::kDelivery, kNone, 0);
    }

    // Итоги вчера.
    if (const DayStats* y = stats_days_ago(w, 0)) {
        detail::add_mail(w, MailKind::kReport, kNone,
                         day_total(y->revenue, kRevenueCount) - day_total(y->expenses, kExpenseCount));
    }

    // Истёкшие модификаторы.
    for (Modifier& m : w.modifiers) {
        if (m.target != kModTargetCount && m.until < c.date) m.target = kModTargetCount;
    }

    // Понедельник: новинки недели, проценты займа (§5.4).
    if (c.weekday == 0) {
        int fresh = 0;
        for (int g = 0; g < d.game_count; ++g) {
            const DayNum r = d.games[g].release;
            if (r > c.date - 7 && r <= c.date) ++fresh;
        }
        if (fresh > 0) detail::add_mail(w, MailKind::kNewGames, kNone, fresh);
        const int32_t interest = formula::loan_interest(w.economy.debt, d.economy->loan_rate_week);
        if (interest > 0) {
            detail::add_expense(w, kExpLoan, interest);
            detail::cover_debt(w);
        }
    }
    detail::invalidate_caches(w);
}

}  // namespace

// ---------------------------------------------------------------------------
// Внутреннее

namespace detail {

void push_notice(World& w, NoticeKind kind, uint8_t a, uint8_t visitor, uint8_t pc, Index ref,
                 int32_t value) {
    Notices& n = w.notices;
    if (n.count == kMaxNotices) {
        n.head = static_cast<uint8_t>((n.head + 1) % kMaxNotices);
        --n.count;
        if (n.dropped < 0xFFFF) ++n.dropped;
    }
    Notice& x = n.ring[(n.head + n.count) % kMaxNotices];
    x = Notice{};
    x.kind = kind;
    x.a = a;
    x.visitor = visitor;
    x.pc = pc;
    x.ref = ref;
    x.value = value;
    ++n.count;
}

void add_revenue(World& w, Revenue kind, int32_t amount) {
    w.stats.today.revenue[kind] += amount;
    w.stats.month_revenue += amount;
    w.economy.money += amount;
}

void add_expense(World& w, Expense kind, int32_t amount) {
    w.stats.today.expenses[kind] += amount;
    w.economy.money -= amount;
}

int add_mail(World& w, MailKind kind, Index ref, int32_t amount) {
    Mailbox& mb = w.mailbox;
    if (mb.count == kMaxMail) {
        // Вытесняем самое старое прочитанное, а если таких нет — самое старое.
        int victim = 0;
        for (int i = 0; i < mb.count; ++i) {
            if (mb.mail[i].read) {
                victim = i;
                break;
            }
        }
        if (!mb.mail[victim].read && mb.unread > 0) --mb.unread;
        for (int i = victim; i + 1 < mb.count; ++i) mb.mail[i] = mb.mail[i + 1];
        --mb.count;
        for (ActiveEvent& ae : w.events.active) {
            if (ae.mail == 0xFF) continue;
            if (ae.mail == victim) {
                ae.mail = 0xFF;
            } else if (ae.mail > victim) {
                --ae.mail;
            }
        }
    }
    const int idx = mb.count;
    Mail& m = mb.mail[idx];
    m = Mail{};
    m.kind = kind;
    m.ref = ref;
    m.date = w.clock.date;
    m.amount = amount;
    ++mb.count;
    ++mb.unread;
    push_notice(w, NoticeKind::kMailArrived, static_cast<uint8_t>(idx));
    return idx;
}

void cover_debt(World& w) {
    Economy& e = w.economy;
    if (e.money >= 0) return;
    e.debt += -e.money;  // GDD §5.4: займ у знакомого
    e.money = 0;
    push_notice(w, NoticeKind::kDebtTaken, 0, kNoVisitor, kNoPc, kNone, e.debt);
}

void invalidate_caches(World& w) { w.priv.cache_valid = 0; }

void rebuild_caches(World& w) {
    const GameData& d = *w.data;
    for (int g = 0; g < d.game_count; ++g) {
        w.priv.pop_today[g] = popularity_fresh(w, static_cast<Index>(g));
    }
    for (int a = 0; a < d.archetype_count; ++a) {
        w.priv.lib_appeal[a] = appeal_from(w, static_cast<Index>(a), w.priv.pop_today);
    }
    w.priv.cache_valid = 1;
}

bool pc_complete(const World& w, uint8_t pc) {
    for (int k = kCompCpu; k <= kCompKeyboard; ++k) {
        if (w.pcs[pc].component[k] == kNone) return false;
    }
    return true;
}

bool game_net_ok(const World& w, Index game) {
    const uint8_t net = w.data->games[game].net;
    if (net == kNetOnlineLow) return w.club.internet_level >= 1;
    if (net == kNetOnlineHigh) return w.club.internet_level >= 2;  // нужна выделенка
    return true;
}

bool game_playable(const World& w, uint8_t pc, Index game) {
    return pc_has_game(w, pc, game) && game_net_ok(w, game) &&
           formula::perf_runs(pc_perf(w, pc, game));
}

float value_for(const World& w, Index archetype, Index game) {
    const float pop = w.priv.cache_valid ? w.priv.pop_today[game] : popularity_fresh(w, game);
    return pop * w.data->archetypes[archetype].affinity[w.data->games[game].genre];  // GDD §7.6
}

const UpgradeRec* owned_upgrade_of_kind(const World& w, uint8_t kind) {
    for (int u = 0; u < w.data->upgrade_count; ++u) {
        if (w.data->upgrades[u].kind == kind && upgrade_owned(w, static_cast<Index>(u))) {
            return &w.data->upgrades[u];
        }
    }
    return nullptr;
}

void own_upgrade(World& w, Index upgrade) {
    const GameData& d = *w.data;
    const UpgradeRec& r = d.upgrades[upgrade];
    if (r.level > 0) {  // ступенчатый вид: новый уровень заменяет прежний
        for (int u = 0; u < d.upgrade_count; ++u) {
            if (d.upgrades[u].kind == r.kind && d.upgrades[u].level > 0) {
                w.club.upgrades[u >> 5] &= ~(1u << (u & 31));
            }
        }
    }
    w.club.upgrades[upgrade >> 5] |= 1u << (upgrade & 31);
    switch (r.kind) {
        case kUpgHall: {
            const int seats = static_cast<int>(r.value);
            w.club.seats = static_cast<uint8_t>(seats > kMaxPcs ? kMaxPcs : (seats < 0 ? 0 : seats));
            break;
        }
        case kUpgInternet: w.club.internet_level = r.level; break;
        case kUpgNetwork: w.club.network_level = r.level; break;
        default: break;
    }
    invalidate_caches(w);
}

bool upgrade_requirement_met(const World& w, Index upgrade) {
    if (upgrade == kNone) return true;
    if (upgrade_owned(w, upgrade)) return true;
    const UpgradeRec& req = w.data->upgrades[upgrade];
    if (req.level == 0) return false;
    const UpgradeRec* have = owned_upgrade_of_kind(w, req.kind);
    return have != nullptr && have->level >= req.level;
}

int months_between(DayNum from, DayNum to) {
    const Ymd a = ymd_from_date(from);
    const Ymd b = ymd_from_date(to);
    return (b.year - a.year) * 12 + (b.month - a.month) - (b.day < a.day ? 1 : 0);
}

float ad_factor(const World& w, Index archetype) {
    float f = 1.0f;  // GDD §7.1: F_ad_a = 1 + Σ бонусов
    for (int u = 0; u < w.data->upgrade_count; ++u) {
        const UpgradeRec& r = w.data->upgrades[u];
        if (r.arg != kNone && r.arg != archetype) continue;
        if (r.kind == kUpgSign && upgrade_owned(w, static_cast<Index>(u))) f += r.value;
        if (r.kind == kUpgAd && w.priv.ad_until[u] >= w.clock.date) f += r.value;
    }
    return f;
}

float modifier_product(const World& w, uint8_t target, Index arg) {
    float f = 1.0f;
    for (const Modifier& m : w.modifiers) {
        if (m.target != target || m.until < w.clock.date) continue;
        if (m.arg != kNone && m.arg != arg) continue;
        f *= m.mult;
    }
    return f;
}

void free_pc(World& w, uint8_t pc) {
    Pc& p = w.pcs[pc];
    p.visitor = kNoVisitor;
    p.running_game = kNone;
    if (p.state == PcState::kBusy) p.state = PcState::kIdle;
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Жизненный цикл

void world_init(World& world, const GameData& data, uint32_t seed) {
    // Нули и в паддинге (снимки сравниваются memcmp), затем значения по умолчанию — на месте,
    // без 30-килобайтного временного объекта на стеке.
    std::memset(static_cast<void*>(&world), 0, sizeof(World));
    new (&world) World();
    World& w = world;
    const EconomyRec& e = *data.economy;
    w.data = &data;
    rng_seed(w.rng, seed);

    w.clock.date = e.start_date;
    w.clock.day_index = 1;
    w.clock.minute = kMorningMinute;
    w.clock.phase = Phase::kMorning;
    w.clock.speed = 1;
    update_calendar(w);

    w.economy.money = e.start_money;
    w.economy.rep = formula::rep_clamp(e.start_rep);
    w.economy.price_hour = e.start_price_hour;
    w.economy.price_night = e.start_price_night;
    for (int g = 0; g < data.goods_count; ++g) w.economy.goods_price[g] = data.goods[g].sell_price;

    for (Modifier& m : w.modifiers) m.target = kModTargetCount;
    for (int u = 0; u < kMaxUpgrades; ++u) w.priv.ad_until[u] = detail::kNoDate;
    for (int r = 0; r < kMaxRegulars; ++r) w.priv.reg_last_visit[r] = detail::kNoDate;
    for (int i = 0; i < e.start_upgrades_count; ++i) {
        detail::own_upgrade(w, data.lists[e.start_upgrades_first + i]);
    }

    for (int i = 0; i < kMaxPcs; ++i) {
        Pc& p = w.pcs[i];
        const bool assembled = i < e.start_pcs && i < w.club.seats;
        for (int k = 0; k < kComponentCount; ++k) {
            p.component[k] = assembled ? e.start_pc[k] : kNone;
            p.wear[k] = 0.0f;
        }
        p.state = assembled && detail::pc_complete(w, static_cast<uint8_t>(i)) ? PcState::kIdle
                                                                               : PcState::kEmpty;
    }

    for (int r = 0; r < data.regular_count; ++r) {
        if (data.regulars[r].unlock_count == 0) w.regulars[r].unlocked = 1;
    }
    w.stats.today.date = w.clock.date;
    detail::rebuild_caches(w);
}

void tick(World& w) {
    Clock& c = w.clock;
    if (w.data == nullptr || c.phase == Phase::kClosed || c.phase == Phase::kGameOver) return;

    ++c.minute;  // 1. время
    if (c.phase == Phase::kMorning && c.minute >= kOpenMinute) c.phase = Phase::kOpen;
    if (c.phase == Phase::kOpen && c.minute >= kMinutesPerDay) {
        close_day(w);
        return;  // дальше тики стоят до cmd_end_day
    }
    if (c.phase == Phase::kNight && c.minute >= kMorningMinute) start_morning(w);
    if (!w.priv.cache_valid || c.minute % 60 == 0) detail::rebuild_caches(w);

    if (c.phase == Phase::kOpen || c.phase == Phase::kNight) {
        ++w.priv.open_minutes_today;
        detail::visitors_tick(w);  // 2. посетители
        detail::pcs_tick(w);       // 3. ПК
    }
    detail::events_tick(w);  // 4. события (T023)
}

float minutes_per_real_second(const World& world) {
    switch (world.clock.phase) {
        case Phase::kOpen:
        case Phase::kMorning: return 2.0f * static_cast<float>(effective_speed(world));  // GDD §4
        case Phase::kNight: return 16.0f;
        default: return 0.0f;
    }
}

int effective_speed(const World& world) { return world.queue_len > 0 ? 1 : world.clock.speed; }

int take_notices(World& world, Notice* out, int max) {
    Notices& n = world.notices;
    int taken = 0;
    while (taken < max && n.count > 0) {
        out[taken++] = n.ring[n.head];
        n.head = static_cast<uint8_t>((n.head + 1) % kMaxNotices);
        --n.count;
    }
    return taken;
}

CmdResult cmd_end_day(World& w) {
    if (w.clock.phase != Phase::kClosed) return CmdResult::kWrongPhase;
    const EconomyRec& e = *w.data->economy;
    Stats& s = w.stats;
    s.days[s.head] = s.today;
    s.head = static_cast<uint8_t>((s.head + 1) % kStatsDays);
    if (s.count < kStatsDays) ++s.count;
    detail::events_on_action(w, kActDayClosed, kNone);

    // Новый день.
    Clock& c = w.clock;
    ++c.date;
    ++c.day_index;
    update_calendar(w);
    s.today = DayStats{};
    s.today.date = c.date;
    for (Pc& p : w.pcs) p.load_minutes_today = 0;
    w.economy.online_minutes_today = 0.0f;
    w.priv.open_minutes_today = 0;

    // 1-е число: аренда, интернет и прочее cost_month, налог за прошедший месяц (§7.14).
    if (ymd_from_date(c.date).day == 1) {
        int32_t total = 0;
        for (int u = 0; u < w.data->upgrade_count; ++u) {
            if (!upgrade_owned(w, static_cast<Index>(u))) continue;
            const UpgradeRec& r = w.data->upgrades[u];
            if (r.cost_month == 0) continue;
            detail::add_expense(w, expense_of_kind(r.kind), r.cost_month);
            total += r.cost_month;
        }
        const int32_t t = formula::tax(s.month_revenue, e.tax_rate);
        detail::add_expense(w, kExpTax, t);
        total += t;
        s.month_revenue = 0;
        detail::add_mail(w, MailKind::kBill, kNone, total);
        detail::push_notice(w, NoticeKind::kMonthPaid, 0, kNoVisitor, kNoPc, kNone, total);
        detail::events_on_action(w, kActMonthStarted, kNone);
    }

    // Долг (§5.4).
    detail::cover_debt(w);
    if (w.economy.debt > e.debt_limit) {
        ++w.economy.debt_over_limit_days;
        if (w.economy.debt_over_limit_days >= e.debt_days_to_lose) {
            c.phase = Phase::kGameOver;
            detail::push_notice(w, NoticeKind::kGameOver);
            return CmdResult::kOk;
        }
    } else {
        w.economy.debt_over_limit_days = 0;
    }

    detail::invalidate_caches(w);
    if (detail::owned_upgrade_of_kind(w, kUpgNightTariff) != nullptr) {
        c.minute = 0;
        c.phase = Phase::kNight;
    } else {
        start_morning(w);
    }
    return CmdResult::kOk;
}

// ---------------------------------------------------------------------------
// Запросы

float pc_overheat(const World& w, uint8_t pc) {
    if (pc >= kMaxPcs) return 0.0f;
    const UpgradeRec* ac = detail::owned_upgrade_of_kind(w, kUpgAircon);
    return formula::overheat(w.pcs[pc].dust, ac != nullptr, ac != nullptr ? ac->value : 1.0f);
}

float pc_perf(const World& w, uint8_t pc, Index game) {
    if (pc >= kMaxPcs || game >= w.data->game_count) return 0.0f;
    const Pc& p = w.pcs[pc];
    if (p.state == PcState::kEmpty) return 0.0f;
    const Index cpu = p.component[kCompCpu], gpu = p.component[kCompGpu], ram = p.component[kCompRam];
    if (cpu == kNone || gpu == kNone || ram == kNone) return 0.0f;
    const HardwareRec* hw = w.data->hardware;
    const float oh = pc_overheat(w, pc);
    const bool virus = p.virus != 0;
    const GameRec& g = w.data->games[game];
    return formula::pc_perf(formula::effective_score(hw[cpu].score, oh, virus),
                            formula::effective_score(hw[gpu].score, oh, virus),
                            formula::effective_score(hw[ram].ram_mb, oh, virus), g.req_cpu,
                            g.req_gpu, g.req_ram_mb);  // GDD §7.4
}

int32_t pc_free_hdd_mb(const World& w, uint8_t pc) {
    if (pc >= kMaxPcs) return 0;
    const Pc& p = w.pcs[pc];
    const Index hdd = p.component[kCompHdd];
    if (hdd == kNone) return 0;
    int64_t free_mb = w.data->hardware[hdd].hdd_mb;
    for (int g = 0; g < w.data->game_count; ++g) {
        if (bit_test(p.installed, g)) free_mb -= w.data->games[g].req_hdd_mb;
    }
    return static_cast<int32_t>(free_mb);
}

bool pc_has_game(const World& w, uint8_t pc, Index game) {
    if (pc >= kMaxPcs || game >= w.data->game_count) return false;
    return bit_test(w.pcs[pc].installed, game);
}

int pcs_working(const World& w) {
    int n = 0;
    for (int i = 0; i < w.club.seats; ++i) {
        const PcState s = w.pcs[i].state;
        if (s == PcState::kIdle || s == PcState::kBusy || s == PcState::kHung) ++n;
    }
    return n;
}

uint8_t best_pc_for(const World& w, uint8_t visitor) {
    if (visitor >= kMaxVisitors || w.visitors[visitor].state == VisitorState::kNone) return kNoPc;
    const Visitor& v = w.visitors[visitor];
    uint8_t best_want = kNoPc, best_alt = kNoPc;
    float want_perf = 0.0f, alt_value = -1.0f, alt_perf = 0.0f;
    for (int i = 0; i < w.club.seats; ++i) {
        const uint8_t pc = static_cast<uint8_t>(i);
        if (w.pcs[pc].state != PcState::kIdle) continue;
        if (v.want_game != kNone && detail::game_playable(w, pc, v.want_game)) {
            const float perf = pc_perf(w, pc, v.want_game);
            if (best_want == kNoPc || perf > want_perf) {
                best_want = pc;
                want_perf = perf;
            }
            continue;
        }
        for (int g = 0; g < w.data->game_count; ++g) {
            const Index gi = static_cast<Index>(g);
            if (gi == v.want_game || !detail::game_playable(w, pc, gi)) continue;
            const float val = detail::value_for(w, v.archetype, gi);
            const float perf = pc_perf(w, pc, gi);
            if (val > alt_value || (val == alt_value && perf > alt_perf)) {
                best_alt = pc;
                alt_value = val;
                alt_perf = perf;
            }
        }
    }
    return best_want != kNoPc ? best_want : best_alt;
}

float game_popularity_today(const World& w, Index game) {
    if (game >= w.data->game_count) return 0.0f;
    const GameRec& g = w.data->games[game];
    const float base = formula::game_popularity(w.clock.date - g.release, g.pop_peak, g.pop_rise_days,
                                                g.pop_half_life_days, g.pop_floor);
    return base * detail::modifier_product(w, kModPop, game);  // GDD §7.6
}

bool game_released(const World& w, Index game) {
    return game < w.data->game_count && w.data->games[game].release <= w.clock.date;
}

float library_appeal_for(const World& w, Index archetype) {
    if (archetype >= w.data->archetype_count) return 0.0f;
    float pop[kMaxGames];
    for (int g = 0; g < w.data->game_count; ++g) pop[g] = popularity_fresh(w, static_cast<Index>(g));
    return appeal_from(w, archetype, pop);
}

float arrival_rate_now(const World& w, Index archetype) {
    if (archetype >= w.data->archetype_count) return 0.0f;
    const ArchetypeRec& a = w.data->archetypes[archetype];
    const Clock& c = w.clock;
    // Праздничный день идёт по коэффициенту воскресенья (GDD не задаёт отдельный).
    const float wd = a.weekday[c.holiday ? 6 : c.weekday];
    float season = 1.0f;
    if (c.holidays) season *= a.season_holidays;
    if (c.session) season *= a.season_session;
    const float lib = w.priv.cache_valid ? w.priv.lib_appeal[archetype] : library_appeal_for(w, archetype);
    // GDD §7.1; F_comp = 1, пока конкурент вне среза (formulas_api.h).
    return formula::arrival_rate(a.base_rate, a.hourly[c.minute / 60 % 24], wd, season,
                                 formula::rep_factor(w.economy.rep), detail::ad_factor(w, archetype),
                                 formula::library_factor(lib), 1.0f,
                                 detail::modifier_product(w, kModArrivals, archetype));
}

int32_t current_hardware_price(const World& w, Index hardware) {
    if (hardware >= w.data->hardware_count) return 0;
    const HardwareRec& h = w.data->hardware[hardware];
    const int months = detail::months_between(h.release, w.clock.date);
    return formula::item_price(h.base_price, months, detail::modifier_product(w, kModHwPrice, kNone),
                               formula::supply_factor(months));  // GDD §7.9
}

int32_t component_sell_price(const World& w, uint8_t pc, Component comp) {
    if (pc >= kMaxPcs || comp >= kComponentCount) return 0;
    const Index hw = w.pcs[pc].component[comp];
    if (hw == kNone) return 0;
    return formula::used_price(current_hardware_price(w, hw), w.pcs[pc].wear[comp]);
}

int free_licenses(const World& w, Index game) {
    if (game >= w.data->game_count) return 0;
    int used = 0;
    for (const Pc& p : w.pcs) {
        if (bit_test(p.installed, game) && !bit_test(p.installed_pirate, game)) ++used;
    }
    return w.library.licenses[game] - used;
}

int pirate_installs(const World& w) {
    int n = 0;
    for (const Pc& p : w.pcs) {
        for (int i = 0; i < kGameBitsetWords; ++i) n += popcount32(p.installed_pirate[i]);
    }
    return n;
}

float pirate_share(const World& w) {
    int total = 0;
    for (const Pc& p : w.pcs) {
        for (int i = 0; i < kGameBitsetWords; ++i) total += popcount32(p.installed[i]);
    }
    return total > 0 ? static_cast<float>(pirate_installs(w)) / static_cast<float>(total) : 0.0f;
}

bool upgrade_owned(const World& w, Index upgrade) {
    return upgrade < kMaxUpgrades && bit_test(w.club.upgrades, upgrade);
}

bool flag_set(const World& w, Index flag) { return flag < kMaxFlags && bit_test(w.events.flags, flag); }

const DayStats* stats_days_ago(const World& w, int days_ago) {
    if (days_ago < 0 || days_ago >= w.stats.count) return nullptr;
    return &w.stats.days[(w.stats.head - 1 - days_ago + 2 * kStatsDays) % kStatsDays];
}

bool can_save(const World& w) {
    return w.clock.phase == Phase::kMorning || w.clock.phase == Phase::kClosed;
}

}  // namespace core
