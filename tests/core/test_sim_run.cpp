// T022: прогон симуляции (90 дней с простым ботом), инварианты мира, детерминизм,
// загрузчик data.pak. Тесты формул GDD §7 — отдельно (T024).
#include <doctest/doctest.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

#include "core/core_api.h"
#include "core/data_api.h"
#include "core/formulas_api.h"

using namespace core;

namespace {

// ---------------------------------------------------------------------------
// Тестовые данные: числа из примеров GDD §7–§8.

enum { kGameKu, kGameRts, kGameArpg, kGameMmo, kGameRace, kGameFuture, kGames };
enum { kArchSchool, kArchStudent, kArchAdult, kArchs };
enum { kHwCpu, kHwCpu2, kHwGpu, kHwRam, kHwHdd, kHwPsu, kHwMon, kHwMouse, kHwKbd, kHwChair, kHws };
enum { kUpHall6, kUpHall10, kUpDialup, kUpInet64, kUpNight, kUpAircon, kUpFlyers, kUpCleaner, kUps };

struct TestData {
    GameRec games[kGames];
    ArchetypeRec archetypes[kArchs];
    RegularRec regulars[2];
    HardwareRec hardware[kHws];
    GoodsRec goods[2];
    UpgradeRec upgrades[kUps];
    CondRec conds[1];
    uint16_t lists[2];
    PeriodRec periods[3];
    MonthDay holidays[3];
    EconomyRec economy;
    GameData data;
};

GameRec game(uint8_t genre, uint8_t net, DayNum release, uint16_t cpu, uint16_t gpu, uint16_t ram,
             uint32_t hdd, float peak, uint16_t rise, uint16_t half, float floor) {
    GameRec g{};
    g.id = kNone;
    g.title = kNone;
    g.genre = genre;
    g.net = net;
    g.release = release;
    g.price_license = 900;
    g.price_pirate = 70;
    g.req_cpu = cpu;
    g.req_gpu = gpu;
    g.req_ram_mb = ram;
    g.req_hdd_mb = hdd;
    g.minigame = kNone;
    g.pop_peak = peak;
    g.pop_floor = floor;
    g.pop_rise_days = rise;
    g.pop_half_life_days = half;
    g.box_tex = kNone;
    return g;
}

ArchetypeRec archetype(float base, const float* hourly, float sat_hol, float sat_ses, int32_t mmin,
                       int32_t mmax, int32_t pexp, float e, uint16_t pmin, uint16_t pmax, float ext,
                       float hunger, float repw, uint32_t flags) {
    ArchetypeRec a{};
    a.id = kNone;
    a.name = kNone;
    a.base_rate = base;
    for (int h = 0; h < 24; ++h) a.hourly[h] = hourly[h];
    const float wd[7] = {1.0f, 1.0f, 1.0f, 1.0f, 1.2f, 1.4f, 1.2f};
    for (int d = 0; d < 7; ++d) a.weekday[d] = wd[d];
    a.season_holidays = sat_hol;
    a.season_session = sat_ses;
    a.money_min = mmin;
    a.money_max = mmax;
    a.price_expect = pexp;
    a.elasticity = e;
    a.patience_min = pmin;
    a.patience_max = pmax;
    a.skill_min = 0.2f;
    a.skill_max = 0.8f;
    a.session_hours_min = 1;
    a.session_hours_max = 3;
    a.model_count = 2;
    a.extend = ext;
    a.alt_accept = 0.6f;
    for (int g = 0; g < kGenreCount; ++g) a.affinity[g] = 1.0f;
    a.virus = 0.02f;
    a.hunger = hunger;
    a.rep_weight = repw;
    a.flags = flags;
    for (int m = 0; m < kMaxArchetypeModels; ++m) a.models[m] = m < 2 ? static_cast<NameId>(10 + m) : kNone;
    return a;
}

HardwareRec hw(uint8_t type, uint16_t score, uint16_t ram, uint32_t hdd, int32_t price, float rate,
               float f0, int8_t comfort) {
    HardwareRec h{};
    h.id = kNone;
    h.name = kNone;
    h.type = type;
    h.comfort = comfort;
    h.score = score;
    h.release = date_from_ymd(2000, 1, 1);
    h.base_price = price;
    h.ram_mb = ram;
    h.power_w = 10;
    h.hdd_mb = hdd;
    h.wear_rate = rate;
    h.fail_base = f0;
    return h;
}

UpgradeRec upg(uint8_t kind, uint8_t level, int32_t price, int32_t month, int32_t day, float minute,
               float value) {
    UpgradeRec u{};
    u.id = kNone;
    u.name = kNone;
    u.kind = kind;
    u.level = level;
    u.arg = kNone;
    u.requires_upgrade = kNone;
    u.price = price;
    u.cost_month = month;
    u.cost_day = day;
    u.cost_minute = minute;
    u.value = value;
    return u;
}

void make_data(TestData& t) {
    std::memset(static_cast<void*>(&t), 0, sizeof(t));
    const DayNum start = date_from_ymd(2001, 9, 1);
    t.games[kGameKu] = game(kGenreShooterTeam, kNetLan, date_from_ymd(2001, 6, 1), 40, 30, 64, 400, 1.0f, 30, 400, 0.6f);
    t.games[kGameRts] = game(kGenreRts, kNetNone, date_from_ymd(1999, 3, 1), 30, 20, 64, 600, 0.6f, 14, 300, 0.3f);
    t.games[kGameArpg] = game(kGenreArpg, kNetNone, date_from_ymd(2000, 6, 1), 35, 25, 64, 800, 0.8f, 14, 200, 0.2f);
    t.games[kGameMmo] = game(kGenreMmo, kNetOnlineHigh, date_from_ymd(2001, 3, 1), 30, 20, 128, 1500, 0.4f, 30, 300, 0.2f);
    t.games[kGameRace] = game(kGenreRace, kNetNone, date_from_ymd(2001, 1, 15), 45, 35, 128, 700, 0.5f, 7, 45, 0.05f);
    t.games[kGameFuture] = game(kGenreShooter3p, kNetNone, start + 40, 60, 50, 128, 1200, 0.9f, 7, 60, 0.1f);

    const float school_h[24] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0.15f, 0.15f, 0.15f, 0.2f, 0.5f,
                                1.0f, 1.0f, 1.0f, 1.0f, 0.8f, 0.5f, 0.3f, 0.1f, 0, 0};
    const float student_h[24] = {0.2f, 0.1f, 0, 0, 0, 0, 0, 0, 0, 0.4f, 0.5f, 0.6f, 0.6f, 0.6f,
                                 0.5f, 0.5f, 0.6f, 0.8f, 1.0f, 1.0f, 1.0f, 0.9f, 0.7f, 0.4f};
    const float adult_h[24] = {0.3f, 0.2f, 0, 0, 0, 0, 0, 0, 0, 0.2f, 0.2f, 0.2f, 0.3f, 0.3f,
                               0.3f, 0.3f, 0.4f, 0.6f, 0.9f, 1.0f, 1.0f, 1.0f, 0.8f, 0.5f};
    t.archetypes[kArchSchool] = archetype(1.6f, school_h, 1.5f, 1.0f, 30, 120, 15, 1.2f, 8, 15, 0.7f, 0.20f, 0.3f,
                                          kArchNoisy | kArchMinigameLover);
    t.archetypes[kArchSchool].affinity[kGenreShooterTeam] = 1.5f;
    t.archetypes[kArchSchool].affinity[kGenreMmo] = 0.3f;
    t.archetypes[kArchStudent] = archetype(1.0f, student_h, 1.0f, 0.7f, 60, 200, 20, 1.0f, 8, 15, 0.8f, 0.15f, 0.3f,
                                           kArchLikesGraphics | kArchNight);
    t.archetypes[kArchAdult] = archetype(0.4f, adult_h, 1.0f, 1.0f, 100, 400, 30, 0.6f, 5, 10, 0.5f, 0.10f, 0.5f,
                                         kArchSmoker);

    RegularRec& dimon = t.regulars[0];
    dimon.id = kNone;
    dimon.name = kNone;
    dimon.nick = kNone;
    dimon.archetype = kArchSchool;
    dimon.model = 11;
    dimon.fav_game = kGameKu;
    dimon.fav_minigame = kNone;
    dimon.unlock_first = 0;
    dimon.unlock_count = 1;
    dimon.skill = 0.85f;
    t.conds[0] = CondRec{kVarVisitsArchetype, kOpGe, kArchSchool, 20.0f};
    RegularRec& vovan = t.regulars[1];
    vovan = dimon;
    vovan.archetype = kArchAdult;
    vovan.fav_game = kGameRts;
    vovan.unlock_count = 0;
    vovan.skill = 0.5f;

    t.hardware[kHwCpu] = hw(kCompCpu, 40, 0, 0, 3000, 0.05f, 0.02f, 0);
    t.hardware[kHwCpu2] = hw(kCompCpu, 70, 0, 0, 6000, 0.05f, 0.02f, 0);
    t.hardware[kHwGpu] = hw(kCompGpu, 30, 0, 0, 2500, 0.05f, 0.02f, 0);
    t.hardware[kHwRam] = hw(kCompRam, 0, 128, 0, 1200, 0.02f, 0.01f, 0);
    t.hardware[kHwHdd] = hw(kCompHdd, 0, 0, 20000, 2500, 0.15f, 0.04f, 0);
    t.hardware[kHwPsu] = hw(kCompPsu, 0, 0, 0, 800, 0.10f, 0.03f, 0);
    t.hardware[kHwMon] = hw(kCompMonitor, 0, 0, 0, 6000, 0.05f, 0.02f, 3);
    t.hardware[kHwMouse] = hw(kCompMouse, 0, 0, 0, 150, 0.40f, 0.05f, 0);
    t.hardware[kHwKbd] = hw(kCompKeyboard, 0, 0, 0, 200, 0.30f, 0.03f, 0);
    t.hardware[kHwChair] = hw(kCompChair, 0, 0, 0, 900, 0.02f, 0.01f, 4);

    t.goods[0] = GoodsRec{kNone, kNone, 10, 15, 1.0f};
    t.goods[1] = GoodsRec{kNone, kNone, 8, 12, 1.2f};

    t.upgrades[kUpHall6] = upg(kUpgHall, 1, 0, 12000, 0, 0, 6);
    t.upgrades[kUpHall10] = upg(kUpgHall, 2, 20000, 18000, 0, 0, 10);
    t.upgrades[kUpHall10].requires_upgrade = kUpHall6;
    t.upgrades[kUpDialup] = upg(kUpgInternet, 1, 0, 0, 0, 0.5f, 56);
    t.upgrades[kUpInet64] = upg(kUpgInternet, 2, 3000, 4000, 0, 0, 64);
    t.upgrades[kUpNight] = upg(kUpgNightTariff, 0, 5000, 0, 0, 0, 0);
    t.upgrades[kUpAircon] = upg(kUpgAircon, 0, 8000, 0, 0, 0, 0.5f);
    t.upgrades[kUpFlyers] = upg(kUpgAd, 0, 500, 0, 0, 0, 0.3f);
    t.upgrades[kUpFlyers].arg = kArchSchool;
    t.upgrades[kUpFlyers].days = 14;
    t.upgrades[kUpCleaner] = upg(kUpgStaffCleaner, 0, 0, 0, 300, 0, 0.6f);

    t.lists[0] = kUpHall6;
    t.lists[1] = kUpInet64;
    t.periods[0] = PeriodRec{kNone, kPeriodHolidays, 0, 1103, 1111};
    t.periods[1] = PeriodRec{kNone, kPeriodHolidays, 0, 1229, 111};
    t.periods[2] = PeriodRec{kNone, kPeriodSession, 0, 108, 131};
    t.holidays[0] = 101;
    t.holidays[1] = 223;
    t.holidays[2] = 308;

    EconomyRec& e = t.economy;
    e.start_date = start;
    e.start_money = 20000;
    e.start_rep = 30.0f;
    e.start_price_hour = 20;
    e.start_price_night = 100;
    e.tax_rate = 0.06f;
    e.electricity_price = 1.2f;
    e.loan_rate_week = 0.10f;
    e.debt_limit = 30000;
    e.debt_days_to_lose = 14;
    e.start_upgrades_first = 0;
    e.start_upgrades_count = 2;
    e.start_pcs = 6;
    const Index pc[kComponentCount] = {kHwCpu, kHwGpu, kHwRam, kHwHdd, kHwPsu, kHwMon, kHwMouse, kHwKbd, kNone, kHwChair};
    for (int k = 0; k < kComponentCount; ++k) e.start_pc[k] = pc[k];

    GameData& d = t.data;
    d = GameData{};
    d.games = t.games;
    d.game_count = kGames;
    d.archetypes = t.archetypes;
    d.archetype_count = kArchs;
    d.regulars = t.regulars;
    d.regular_count = 2;
    d.hardware = t.hardware;
    d.hardware_count = kHws;
    d.goods = t.goods;
    d.goods_count = 2;
    d.upgrades = t.upgrades;
    d.upgrade_count = kUps;
    d.conds = t.conds;
    d.cond_count = 1;
    d.lists = t.lists;
    d.list_count = 2;
    d.periods = t.periods;
    d.period_count = 3;
    d.holidays = t.holidays;
    d.holiday_count = 3;
    d.economy = &t.economy;
}

// ---------------------------------------------------------------------------
// Бот: обслуживает очередь, чинит, закупает — как будет делать balance-runner (T025).

struct BotStats {
    int serve_calls = 0;
    int seated = 0;
    int seated_alt = 0;
    int left_at_desk = 0;
    int reboots = 0;
    int repairs = 0;
    int replaces = 0;
    int sales = 0;
};

void bot_morning(World& w) {
    const GameData& d = *w.data;
    // Игры: лицензии на КУ, пиратские диски на остальное вышедшее.
    if (w.library.licenses[kGameKu] == 0) cmd_order(w, OrderKind::kLicense, kGameKu, 6);
    for (Index g = 1; g < d.game_count; ++g) {
        if (game_released(w, g) && !((w.library.pirate[g >> 5] >> (g & 31)) & 1u)) {
            cmd_order(w, OrderKind::kPirate, g, 1);
        }
    }
    for (uint8_t pc = 0; pc < w.club.seats; ++pc) {
        if (w.pcs[pc].state == PcState::kEmpty) continue;
        for (Index g = 0; g < d.game_count; ++g) {
            if (pc_has_game(w, pc, g)) continue;
            if (cmd_install(w, pc, g, false, 1.0f) != CmdResult::kOk) cmd_install(w, pc, g, true, 1.0f);
        }
        if (w.pcs[pc].dust > 40.0f) cmd_clean_dust(w, pc, 0.9f);
        for (int k = 0; k < kComponentCount; ++k) {
            if (w.pcs[pc].component[k] != kNone && w.pcs[pc].wear[k] > 60.0f &&
                w.pcs[pc].wear[k] < 100.0f) {
                cmd_repair(w, pc, static_cast<Component>(k), 0.8f);
            }
        }
    }
    for (Index g = 0; g < d.goods_count; ++g) {
        if (w.inventory.goods[g] < 15) cmd_order(w, OrderKind::kGoods, g, 30);
    }
    if (w.clock.day_index == 5) cmd_buy_upgrade(w, kUpFlyers);
    if (w.clock.day_index >= 10 && !upgrade_owned(w, kUpNight) && w.economy.money > 12000) {
        cmd_buy_upgrade(w, kUpNight);
    }
}

void fix_pc(World& w, uint8_t pc, BotStats& bs) {
    Pc& p = w.pcs[pc];
    if (p.state == PcState::kHung && cmd_reboot(w, pc, 1.0f) == CmdResult::kOk) ++bs.reboots;
    if (p.state != PcState::kBroken) return;
    const Component k = static_cast<Component>(p.broken_component);
    if (p.wear[k] < 100.0f) {
        if (cmd_repair(w, pc, k, 0.7f) == CmdResult::kOk) ++bs.repairs;
        return;
    }
    const Index part = p.component[k];
    if (cmd_replace_component(w, pc, part, 1.0f) == CmdResult::kOk) {
        ++bs.replaces;
    } else {
        bool ordered = false;
        for (int i = 0; i < w.inventory.order_count; ++i) {
            ordered |= w.inventory.orders[i].kind == OrderKind::kHardware && w.inventory.orders[i].ref == part;
        }
        if (!ordered) cmd_order(w, OrderKind::kHardware, part, 1);
    }
}

void bot_tick(World& w, BotStats& bs) {
    if (w.clock.phase == Phase::kMorning && w.clock.minute == kMorningMinute) bot_morning(w);
    for (uint8_t pc = 0; pc < w.club.seats; ++pc) fix_pc(w, pc, bs);
    for (uint8_t i = 0; i < kMaxVisitors; ++i) {
        const Visitor& v = w.visitors[i];
        if (v.request == Request::kFood || v.request == Request::kHelp) {
            if (cmd_resolve_request(w, i) == CmdResult::kOk) ++bs.sales;
        }
    }
    if (w.clock.phase != Phase::kOpen || w.queue_len == 0) return;
    const uint8_t v = w.queue[0];
    if (w.visitors[v].state != VisitorState::kWaiting) return;
    const uint8_t pc = best_pc_for(w, v);
    if (pc == kNoPc) return;
    const bool night = upgrade_owned(w, kUpNight) && w.clock.minute >= 21 * 60 &&
                       (w.data->archetypes[w.visitors[v].archetype].flags & kArchNight) &&
                       w.visitors[v].money >= w.economy.price_night;
    ++bs.serve_calls;
    switch (cmd_serve(w, v, pc, night ? Tariff::kNight : Tariff::kHourly)) {
        case ServeResult::kSeated: ++bs.seated; break;
        case ServeResult::kSeatedAlt: ++bs.seated_alt; break;
        case ServeResult::kLeftNoGame:
        case ServeResult::kLeftPrice: ++bs.left_at_desk; break;
        default: break;
    }
}

// Инварианты мира; при нарушении — текст причины.
const char* check_world(const World& w) {
    const Economy& e = w.economy;
    if (!(e.rep >= 0.0f && e.rep <= 100.0f)) return "rep вне [0, 100]";
    if (e.debt < 0) return "debt < 0";
    int waiting = 0;
    for (int i = 0; i < kMaxVisitors; ++i) {
        const Visitor& v = w.visitors[i];
        if (v.state == VisitorState::kNone) continue;
        if (!(v.sat >= 0.0f && v.sat <= 100.0f)) return "sat вне [0, 100]";
        if (v.money < 0) return "у посетителя отрицательные деньги";
        if (v.state == VisitorState::kWaiting) ++waiting;
        if (v.state == VisitorState::kToSeat || v.state == VisitorState::kPlaying) {
            if (v.pc >= w.club.seats || w.pcs[v.pc].visitor != i) return "посетитель за ПК, а ПК его не знает";
        }
    }
    if (waiting != w.queue_len) return "очередь не совпадает с kWaiting";
    for (int q = 0; q < w.queue_len; ++q) {
        if (w.visitors[w.queue[q]].state != VisitorState::kWaiting) return "в очереди не ждущий";
    }
    for (int i = 0; i < kMaxPcs; ++i) {
        const Pc& p = w.pcs[i];
        if (!(p.dust >= 0.0f && p.dust <= 100.0f)) return "dust вне [0, 100]";
        for (float wear : p.wear) {
            if (!(wear >= 0.0f && wear <= 100.0f)) return "wear вне [0, 100]";
        }
        if (p.state == PcState::kBusy && p.visitor == kNoVisitor) return "kBusy без посетителя";
        if (p.visitor != kNoVisitor) {
            const Visitor& v = w.visitors[p.visitor];
            if (v.pc != i || (v.state != VisitorState::kToSeat && v.state != VisitorState::kPlaying)) {
                return "ПК держит посетителя, который не за ним";
            }
            if (p.state != PcState::kBusy && p.state != PcState::kHung && p.state != PcState::kBroken) {
                return "посетитель за ПК в неподходящем состоянии";
            }
        }
        if (p.state == PcState::kIdle && i >= w.club.seats) return "ПК за пределами зала";
    }
    if (free_licenses(w, kGameKu) < 0) return "лицензий меньше, чем установок";
    return nullptr;
}

struct RunResult {
    int days = 0;
    const char* error = nullptr;
    int error_day = 0;
    BotStats bot;
    int64_t night_revenue = 0;
    int64_t revenue = 0, expenses = 0, arrived = 0, served = 0, extended = 0, hangs = 0, failures = 0;
    int64_t left[10] = {};
};

RunResult run(World& w, const GameData& d, uint32_t seed, int days, bool log) {
    RunResult r;
    world_init(w, d, seed);
    while (w.clock.day_index <= days && w.clock.phase != Phase::kGameOver) {
        if (w.clock.phase == Phase::kClosed) {
            const DayStats& t = w.stats.today;
            for (int k = 0; k < kRevenueCount; ++k) r.revenue += t.revenue[k];
            for (int k = 0; k < kExpenseCount; ++k) r.expenses += t.expenses[k];
            r.night_revenue += t.revenue[kRevNight];
            r.arrived += t.arrived;
            r.served += t.served;
            r.extended += t.extended;
            r.hangs += t.pc_hangs;
            r.failures += t.pc_failures;
            for (int k = 0; k < 10; ++k) r.left[k] += t.left[k];
            if (log) {
                int32_t rev = 0, exp = 0;
                for (int k = 0; k < kRevenueCount; ++k) rev += t.revenue[k];
                for (int k = 0; k < kExpenseCount; ++k) exp += t.expenses[k];
                std::printf("day %3d  money %6d debt %5d rep %5.1f  arrived %3d served %3d ext %2d  rev %5d exp %5d  sat %.0f\n",
                            w.clock.day_index, t.money_end, t.debt_end, static_cast<double>(t.rep_end), t.arrived,
                            t.served, t.extended, rev, exp,
                            t.served > 0 ? static_cast<double>(t.sat_sum / t.served) : 0.0);
            }
            ++r.days;
            cmd_end_day(w);
            continue;
        }
        bot_tick(w, r.bot);
        tick(w);
        Notice n[16];
        while (take_notices(w, n, 16) > 0) {
        }
        if (const char* err = check_world(w)) {
            r.error = err;
            r.error_day = w.clock.day_index;
            break;
        }
    }
    return r;
}

}  // namespace

TEST_CASE("T022: 90 игровых дней без ошибок, инварианты мира соблюдены") {
    static TestData t;
    make_data(t);
    static World w;
    const bool log = std::getenv("PCCLUB_SIM_LOG") != nullptr;
    const RunResult r = run(w, t.data, 1, 90, log);
    INFO("ошибка: " << (r.error ? r.error : "нет") << ", день " << r.error_day);
    REQUIRE(r.error == nullptr);
    CHECK(w.clock.phase != Phase::kGameOver);
    CHECK(r.days == 90);
    CHECK(w.clock.day_index == 91);
    CHECK(w.clock.date == date_from_ymd(2001, 9, 1) + 90);
    CHECK(r.arrived > 1000);
    CHECK(r.served > 500);
    CHECK(r.extended > 0);
    CHECK(r.hangs > 0);
    CHECK(r.bot.reboots > 0);
    CHECK(r.bot.sales > 0);
    CHECK(r.revenue > 0);
    CHECK(w.stats.count == kStatsDays);
    CHECK(upgrade_owned(w, kUpNight));
    CHECK(r.night_revenue > 0);  // ночные пакеты продавались, фаза kNight пройдена
    CHECK(w.mailbox.count > 0);
    CHECK(w.regulars[1].visits > 0);  // постоянный без условий приходит
    if (log) {
        std::printf("revenue %lld expenses %lld arrived %lld served %lld ext %lld hangs %lld fails %lld\n",
                    static_cast<long long>(r.revenue), static_cast<long long>(r.expenses),
                    static_cast<long long>(r.arrived), static_cast<long long>(r.served),
                    static_cast<long long>(r.extended), static_cast<long long>(r.hangs),
                    static_cast<long long>(r.failures));
        std::printf("left:");
        for (int k = 0; k < 10; ++k) std::printf(" %lld", static_cast<long long>(r.left[k]));
        std::printf("\nbot: serve %d seated %d alt %d desk_left %d reboots %d repairs %d replaces %d sales %d\n",
                    r.bot.serve_calls, r.bot.seated, r.bot.seated_alt, r.bot.left_at_desk, r.bot.reboots,
                    r.bot.repairs, r.bot.replaces, r.bot.sales);
        std::printf("dimon unlocked %d visits %d relation %d\n", w.regulars[0].unlocked, w.regulars[0].visits,
                    w.regulars[0].relation);
    }
}

TEST_CASE("T022: один сид — один прогон; снимок World копируется") {
    static TestData t;
    make_data(t);
    static World a, b, c;
    run(a, t.data, 77, 10, false);
    run(b, t.data, 77, 10, false);
    CHECK(a.rng.state == b.rng.state);
    CHECK(a.economy.money == b.economy.money);
    CHECK(a.economy.rep == b.economy.rep);
    CHECK(std::memcmp(&a.stats, &b.stats, sizeof(Stats)) == 0);
    run(c, t.data, 78, 10, false);
    CHECK(a.rng.state != c.rng.state);

    // Снимок через memcpy продолжает тот же прогон.
    static World snap;
    std::memcpy(static_cast<void*>(&snap), &a, sizeof(World));
    for (int i = 0; i < 3000; ++i) {
        if (a.clock.phase == Phase::kClosed) cmd_end_day(a);
        if (snap.clock.phase == Phase::kClosed) cmd_end_day(snap);
        tick(a);
        tick(snap);
    }
    CHECK(a.rng.state == snap.rng.state);
    CHECK(a.economy.money == snap.economy.money);
}

TEST_CASE("T022: старт новой игры по EconomyRec") {
    static TestData t;
    make_data(t);
    static World w;
    world_init(w, t.data, 5);
    CHECK(w.clock.date == date_from_ymd(2001, 9, 1));
    CHECK(w.clock.weekday == 5);
    CHECK(w.clock.minute == kMorningMinute);
    CHECK(w.clock.phase == Phase::kMorning);
    CHECK(w.economy.money == 20000);
    CHECK(w.economy.price_hour == 20);
    CHECK(w.club.seats == 6);
    CHECK(w.club.internet_level == 2);
    CHECK(pcs_working(w) == 6);
    CHECK(w.pcs[6].state == PcState::kEmpty);
    CHECK(w.regulars[0].unlocked == 0);
    CHECK(w.regulars[1].unlocked == 1);
    CHECK(w.modifiers[0].target == kModTargetCount);
    CHECK(can_save(w));
    // Ни одной игры: на ПК не запускается ничего.
    CHECK_FALSE(pc_has_game(w, 0, kGameKu));
    CHECK(pc_perf(w, 0, kGameKu) == doctest::Approx(1.0f));  // CPU 40/40, GPU 30/30
    CHECK(pc_free_hdd_mb(w, 0) == 20000);
    // 60 тиков утра → 9:00, клуб открыт.
    for (int i = 0; i < 60; ++i) tick(w);
    CHECK(w.clock.phase == Phase::kOpen);
    CHECK(minutes_per_real_second(w) == doctest::Approx(2.0f));
}

TEST_CASE("T022: команды отказывают, не меняя мир") {
    static TestData t;
    make_data(t);
    static World w;
    world_init(w, t.data, 9);
    CHECK(cmd_end_day(w) == CmdResult::kWrongPhase);
    CHECK(cmd_set_speed(w, 3) == CmdResult::kInvalid);
    CHECK(cmd_set_price_hour(w, 0) == CmdResult::kInvalid);
    CHECK(cmd_install(w, 0, kGameKu, false, 1.0f) == CmdResult::kNoLicense);
    CHECK(cmd_install(w, 0, kGameFuture, true, 1.0f) == CmdResult::kInvalid);  // ещё не вышла
    CHECK(cmd_order(w, OrderKind::kLicense, kGameFuture, 1) == CmdResult::kInvalid);
    CHECK(cmd_order(w, OrderKind::kHardware, kHwCpu2, 100) == CmdResult::kNoMoney);
    CHECK(cmd_buy_upgrade(w, kUpHall6) == CmdResult::kInvalid);  // уже куплен
    CHECK(cmd_buy_upgrade(w, kUpDialup) == CmdResult::kInvalid);  // младше текущего
    CHECK(cmd_reboot(w, 0, 1.0f) == CmdResult::kInvalid);
    CHECK(cmd_repay_debt(w, 100) == CmdResult::kInvalid);
    CHECK(w.economy.money == 20000);

    // Заказ → доставка утром следующего дня → установка с лицензией.
    CHECK(cmd_order(w, OrderKind::kLicense, kGameKu, 2) == CmdResult::kOk);
    CHECK(w.economy.money == 20000 - 1800);
    while (w.clock.phase != Phase::kClosed) tick(w);
    CHECK(cmd_end_day(w) == CmdResult::kOk);
    CHECK(w.library.licenses[kGameKu] == 2);
    CHECK(cmd_install(w, 0, kGameKu, false, 0.0f) == CmdResult::kFailed);
    CHECK(cmd_install(w, 0, kGameKu, false, 1.0f) == CmdResult::kOk);
    CHECK(cmd_install(w, 1, kGameKu, false, 1.0f) == CmdResult::kOk);
    CHECK(cmd_install(w, 2, kGameKu, false, 1.0f) == CmdResult::kNoLicense);
    CHECK(free_licenses(w, kGameKu) == 0);
    CHECK(cmd_uninstall(w, 1, kGameKu) == CmdResult::kOk);
    CHECK(free_licenses(w, kGameKu) == 1);
    CHECK(pc_free_hdd_mb(w, 0) == 20000 - 400);

    // Зал 10 мест: требование hall_6 выполнено, новые места пустые.
    w.economy.money = 50000;
    CHECK(cmd_buy_upgrade(w, kUpHall10) == CmdResult::kOk);
    CHECK(w.club.seats == 10);
    CHECK_FALSE(upgrade_owned(w, kUpHall6));
    CHECK(w.pcs[8].state == PcState::kEmpty);
}

TEST_CASE("T022: 24:00 — расходы дня, 1-е число — аренда и налог") {
    static TestData t;
    make_data(t);
    static World w;
    world_init(w, t.data, 3);
    int32_t month_paid = 0;
    for (int day = 0; day < 30; ++day) {
        while (w.clock.phase != Phase::kClosed) tick(w);
        CHECK(w.stats.today.expenses[kExpElectricity] == formula::electricity_cost(
                  static_cast<float>(w.stats.today.load_minutes) / 60.0f, 15.0f, 1.2f));
        cmd_end_day(w);
        Notice n[64];
        const int k = take_notices(w, n, 64);
        for (int i = 0; i < k; ++i) {
            if (n[i].kind == NoticeKind::kMonthPaid) month_paid = n[i].value;
        }
    }
    // 30-й end_day — переход на 1 октября: аренда 12 000 + интернет 4 000 + налог 0 (выручки нет).
    CHECK(ymd_from_date(w.clock.date).day == 1);
    CHECK(month_paid == 16000);
    CHECK(w.stats.today.expenses[kExpRent] == 12000);
    CHECK(w.stats.today.expenses[kExpInternet] == 4000);
    CHECK(w.stats.month_revenue == 0);
}

// ---------------------------------------------------------------------------
// data.pak: минимальный файл собирается здесь же (по data_pak.md) и читается data_load.

namespace {

uint32_t crc32_ref(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int b = 0; b < 8; ++b) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

struct PakBuilder {
    struct Sec {
        uint32_t tag;
        std::vector<uint8_t> bytes;
        uint32_t count;
        uint16_t rec_size;
    };
    std::vector<Sec> secs;

    template <typename T>
    void table(uint32_t tag, const T* recs, uint32_t count) {
        Sec s{tag, {}, count, static_cast<uint16_t>(sizeof(T))};
        const uint8_t* p = reinterpret_cast<const uint8_t*>(recs);
        s.bytes.assign(p, p + sizeof(T) * count);
        secs.push_back(s);
    }

    void strings(uint32_t tag, const std::vector<const char*>& list) {
        Sec s{tag, {}, static_cast<uint32_t>(list.size()), 1};
        uint32_t off = static_cast<uint32_t>(list.size() * 4);
        std::vector<uint8_t> body;
        for (const char* str : list) {
            for (int b = 0; b < 4; ++b) s.bytes.push_back(static_cast<uint8_t>(off >> (8 * b)));
            const size_t len = std::strlen(str) + 1;
            body.insert(body.end(), str, str + len);
            off += static_cast<uint32_t>(len);
        }
        s.bytes.insert(s.bytes.end(), body.begin(), body.end());
        secs.push_back(s);
    }

    std::vector<uint8_t> build() const {
        size_t pos = sizeof(PakHeader) + secs.size() * sizeof(PakSection);
        std::vector<PakSection> dir;
        for (const Sec& s : secs) {
            pos = (pos + 15) & ~size_t{15};
            dir.push_back(PakSection{s.tag, static_cast<uint32_t>(pos), static_cast<uint32_t>(s.bytes.size()), s.count,
                                     s.rec_size, 0});
            pos += s.bytes.size();
        }
        std::vector<uint8_t> out(pos, 0);
        PakHeader h{kPakMagic, kPakVersion, static_cast<uint16_t>(secs.size()), static_cast<uint32_t>(pos), 0};
        std::memcpy(out.data() + sizeof(PakHeader), dir.data(), dir.size() * sizeof(PakSection));
        for (size_t i = 0; i < secs.size(); ++i) {
            std::memcpy(out.data() + dir[i].offset, secs[i].bytes.data(), secs[i].bytes.size());
        }
        h.crc32 = crc32_ref(out.data() + 16, out.size() - 16);
        std::memcpy(out.data(), &h, sizeof(h));
        return out;
    }
};

}  // namespace

TEST_CASE("T022: data_load читает pak по data_pak.md и ловит порчу") {
    TestData t;
    make_data(t);
    GameRec g = t.games[kGameKu];
    g.id = 1;     // NAME[1] = "kontr_udar_13"
    g.title = 0;  // STRS[0]
    StringKeyRec keys[2] = {{fnv1a32("games.kontr_udar_13.title"), 0, 0}, {fnv1a32("ui.ok"), 1, 0}};
    if (keys[0].hash > keys[1].hash) std::swap(keys[0], keys[1]);

    PakBuilder b;
    b.table(kSecGames, &g, 1);
    b.table(kSecEconomy, &t.economy, 1);
    b.table(kSecUpgrades, t.upgrades, kUps);
    b.table(kSecHardware, t.hardware, kHws);
    b.table(kSecLists, t.lists, 2);
    b.strings(kSecStrings, {"Контр-Удар 1.3", "Ок"});
    b.table(kSecStringKeys, keys, 2);
    b.strings(kSecNames, {"school", "kontr_udar_13"});
    std::vector<uint8_t> file = b.build();

    alignas(16) static uint8_t buf[4096];
    REQUIRE(file.size() <= sizeof(buf));
    std::memcpy(buf, file.data(), file.size());
    GameData d;
    REQUIRE(data_load(buf, file.size(), &d) == DataError::kOk);
    CHECK(d.game_count == 1);
    CHECK(d.upgrade_count == kUps);
    CHECK(d.archetype_count == 0);
    CHECK(d.archetypes == nullptr);
    CHECK(d.economy->start_money == 20000);
    CHECK(d.games[0].req_cpu == 40);
    CHECK(std::strcmp(data_string(d, d.games[0].title), "Контр-Удар 1.3") == 0);
    CHECK(std::strcmp(data_name(d, d.games[0].id), "kontr_udar_13") == 0);
    CHECK(std::strcmp(data_string(d, kNone), "") == 0);
    CHECK(std::strcmp(data_string(d, 99), "") == 0);
    CHECK(data_find_game(d, "kontr_udar_13") == 0);
    CHECK(data_find_game(d, "nope") == kNone);
    CHECK(std::strcmp(data_string(d, data_find_string(d, "ui.ok")), "Ок") == 0);
    CHECK(data_find_string(d, "ui.nope") == kNone);

    // Мир поднимается из загруженного pak (без архетипов — посетителей нет).
    static World w;
    world_init(w, d, 1);
    CHECK(w.club.seats == 6);
    for (int i = 0; i < 2000 && w.clock.phase != Phase::kClosed; ++i) tick(w);
    CHECK(w.clock.phase == Phase::kClosed);

    // Порча.
    GameData bad;
    CHECK(data_load(buf + 1, file.size(), &bad) == DataError::kBadAlignment);
    CHECK(data_load(buf, 8, &bad) == DataError::kTooSmall);
    CHECK(data_load(buf, file.size() - 16, &bad) == DataError::kTooSmall);
    buf[file.size() - 3] ^= 0x55;
    CHECK(data_load(buf, file.size(), &bad) == DataError::kBadCrc);
    CHECK(bad.game_count == 0);
    buf[file.size() - 3] ^= 0x55;
    buf[4] = 99;
    CHECK(data_load(buf, file.size(), &bad) == DataError::kBadVersion);
    buf[4] = static_cast<uint8_t>(kPakVersion);
    buf[0] = 'X';
    CHECK(data_load(buf, file.size(), &bad) == DataError::kBadMagic);

    // Нет обязательной секции.
    PakBuilder b2;
    b2.table(kSecGames, &g, 1);
    std::vector<uint8_t> f2 = b2.build();
    std::memcpy(buf, f2.data(), f2.size());
    CHECK(data_load(buf, f2.size(), &bad) == DataError::kMissingSection);
}
