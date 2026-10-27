// T023: система событий (GDD §8.6) — каждый тип триггера и эффекта на примере.
// Данные собраны вручную в раскладке data_pak.md §5; события изолированы флагами-«воротами»,
// которые тест ставит сам.
#include <doctest/doctest.h>

#include <cstring>

#include "core/core_api.h"
#include "core/data_api.h"

using namespace core;

namespace {

enum { kGameShooter, kGameRts, kGames };
enum { kArchSchool, kArchs };
enum { kUpHall, kUpAd, kUpNight, kUpAircon, kUps };
enum { kRegDimon, kRegs };
enum {
    kEvLicense,   // daily_roll 11:00, шанс через chance.per, mail
    kEvRevenge,   // scheduled, phone
    kEvDate,      // date 2001-09-03 без часа, world
    kEvState,     // on_state money ≥ 15000
    kEvAction,    // on_action upgrade_bought, once
    kEvRegular,   // on_state, из event_pool Димона
    kEvW1,        // daily_roll 13:00, weight 1
    kEvW2,        // daily_roll 13:00, weight 3
    kEvs
};
enum { kFGateLicense, kFGateDate, kFGateState, kFGateAction, kFGateWeights, kFRich, kFlags };

constexpr int kMaxEff = 64;
constexpr int kMaxCond = 16;
constexpr int kMaxChoice = 16;

struct EvData {
    GameRec games[kGames];
    ArchetypeRec archetypes[kArchs];
    RegularRec regulars[kRegs];
    HardwareRec hardware[kComponentCount];
    GoodsRec goods[1];
    UpgradeRec upgrades[kUps];
    EventRec events[kEvs];
    ChoiceRec choices[kMaxChoice];
    EffectRec effects[kMaxEff];
    CondRec conds[kMaxCond];
    ChanceTermRec chance[1];
    uint16_t lists[2];
    EconomyRec economy;
    GameData data;
    int n_eff = 0, n_cond = 0, n_choice = 0;
};

EffectRec eff(uint8_t type, uint8_t sub = 0, Index ref = kNone, Index arg = kNone, int32_t ival = 0,
              float fval = 0.0f) {
    EffectRec e{};
    e.type = type;
    e.sub = sub;
    e.hour = 0xFF;
    e.ref = ref;
    e.arg = arg;
    e.ival = ival;
    e.fval = fval;
    return e;
}

uint16_t add_cond(EvData& t, uint8_t var, uint8_t op, Index arg, float val) {
    t.conds[t.n_cond] = CondRec{var, op, arg, val};
    return static_cast<uint16_t>(t.n_cond++);
}

// Вариант с эффектами подряд; возвращает индекс первого эффекта.
uint16_t add_choice(EvData& t, const EffectRec* fx, int n, uint16_t req_first = 0, uint8_t req_count = 0) {
    ChoiceRec& c = t.choices[t.n_choice++];
    c.text = kNone;
    c.req_first = req_first;
    c.req_count = req_count;
    c.effect_first = static_cast<uint16_t>(t.n_eff);
    c.effect_count = static_cast<uint8_t>(n);
    for (int i = 0; i < n; ++i) t.effects[t.n_eff++] = fx[i];
    return c.effect_first;
}

// Дети эффекта chance — после эффектов варианта.
void add_then(EvData& t, uint16_t parent, const EffectRec* fx, int n) {
    t.effects[parent].child_first = static_cast<uint16_t>(t.n_eff);
    t.effects[parent].child_count = static_cast<uint8_t>(n);
    for (int i = 0; i < n; ++i) t.effects[t.n_eff++] = fx[i];
}

EventRec event(uint8_t trigger, uint8_t hour, uint8_t kind) {
    EventRec e{};
    e.id = kNone;
    e.trigger = trigger;
    e.trigger_hour = hour;
    e.present_kind = kind;
    e.weight = 1.0f;
    e.chance_base = 1.0f;
    e.present_model = kNone;
    e.present_text = kNone;
    return e;
}

void begin_choices(EvData& t, EventRec& e) { e.choice_first = static_cast<uint16_t>(t.n_choice); }
void end_choices(EvData& t, EventRec& e) { e.choice_count = static_cast<uint8_t>(t.n_choice - e.choice_first); }

void gate(EvData& t, EventRec& e, int flag) {
    e.cond_first = add_cond(t, kVarFlag, kOpEq, static_cast<Index>(flag), 1.0f);
    e.cond_count = 1;
}

void make_data(EvData& t) {
    std::memset(static_cast<void*>(&t), 0, sizeof(t));
    const DayNum start = date_from_ymd(2001, 9, 1);

    for (int g = 0; g < kGames; ++g) {
        GameRec& r = t.games[g];
        r.id = kNone;
        r.title = kNone;
        r.genre = g == kGameShooter ? kGenreShooterTeam : kGenreRts;
        r.release = date_from_ymd(2000, 1, 1);
        r.price_license = 500;
        r.price_pirate = 50;
        r.req_cpu = 10;
        r.req_gpu = 10;
        r.req_ram_mb = 32;
        r.req_hdd_mb = 100;
        r.minigame = kNone;
        r.pop_peak = 1.0f;
        r.pop_floor = 0.5f;
        r.pop_rise_days = 10;
        r.pop_half_life_days = 100;
        r.box_tex = kNone;
    }

    // Случайного потока нет (base_rate = 0): посетители появляются только от событий.
    ArchetypeRec& a = t.archetypes[kArchSchool];
    a.id = kNone;
    a.name = kNone;
    for (int d = 0; d < 7; ++d) a.weekday[d] = 1.0f;
    a.season_holidays = a.season_session = 1.0f;
    a.money_min = 50;
    a.money_max = 100;
    a.price_expect = 20;
    a.elasticity = 1.0f;
    a.patience_min = 200;
    a.patience_max = 300;
    a.session_hours_min = 1;
    a.session_hours_max = 2;
    a.model_count = 1;
    a.extend = 0.5f;
    a.alt_accept = 0.6f;
    for (int g = 0; g < kGenreCount; ++g) a.affinity[g] = 1.0f;
    a.rep_weight = 0.3f;
    for (int m = 0; m < kMaxArchetypeModels; ++m) a.models[m] = kNone;

    for (int k = 0; k < kComponentCount; ++k) {
        HardwareRec& h = t.hardware[k];
        h.id = kNone;
        h.name = kNone;
        h.type = static_cast<uint8_t>(k);
        h.score = 100;
        h.ram_mb = 512;
        h.hdd_mb = 20000;
        h.release = date_from_ymd(2001, 1, 1);
        h.base_price = 1000;
    }
    t.goods[0] = GoodsRec{kNone, kNone, 10, 15, 1.0f};

    for (int u = 0; u < kUps; ++u) {
        t.upgrades[u].id = kNone;
        t.upgrades[u].name = kNone;
        t.upgrades[u].arg = kNone;
        t.upgrades[u].requires_upgrade = kNone;
    }
    t.upgrades[kUpHall].kind = kUpgHall;
    t.upgrades[kUpHall].level = 1;
    t.upgrades[kUpHall].value = 4;
    t.upgrades[kUpAd].kind = kUpgAd;
    t.upgrades[kUpAd].days = 7;
    t.upgrades[kUpAd].price = 100;
    t.upgrades[kUpAd].value = 0.3f;
    t.upgrades[kUpNight].kind = kUpgNightTariff;
    t.upgrades[kUpNight].price = 1000;
    t.upgrades[kUpAircon].kind = kUpgAircon;
    t.upgrades[kUpAircon].price = 8000;
    t.upgrades[kUpAircon].value = 0.5f;

    // Димон закрыт (условие недостижимо) — его открывает эффект spawn_visitor.
    RegularRec& dimon = t.regulars[kRegDimon];
    dimon.id = kNone;
    dimon.name = kNone;
    dimon.nick = kNone;
    dimon.archetype = kArchSchool;
    dimon.model = kNone;
    dimon.fav_game = kGameShooter;
    dimon.fav_minigame = kNone;
    dimon.unlock_first = add_cond(t, kVarVisitsArchetype, kOpGe, kArchSchool, 1000.0f);
    dimon.unlock_count = 1;
    dimon.event_pool_first = 1;
    dimon.event_pool_count = 1;
    dimon.skill = 0.85f;
    t.lists[0] = kUpHall;      // start_upgrades
    t.lists[1] = kEvRegular;   // event_pool Димона

    // --- События
    EventRec& lic = t.events[kEvLicense] = event(kTrigDailyRoll, 11, kPresentMail);
    lic.chance_base = 0.0f;
    lic.chance_first = 0;
    lic.chance_count = 1;
    t.chance[0] = ChanceTermRec{kVarPirateInstalls, 0, kNone, 0.5f};
    lic.cooldown_days = 3;
    lic.cond_first = add_cond(t, kVarDay, kOpGe, kNone, 2.0f);
    add_cond(t, kVarFlag, kOpEq, kFGateLicense, 1.0f);
    lic.cond_count = 2;
    begin_choices(t, lic);
    {
        const EffectRec pay[] = {eff(kEffMoney, 1, kVarPirateInstalls, kNone, 0, -5000.0f),
                                 eff(kEffRep, 0, kNone, kNone, 0, -2.0f)};
        add_choice(t, pay, 2);
        const uint16_t req = add_cond(t, kVarMoney, kOpGe, kNone, 3000.0f);
        const EffectRec bribe[] = {eff(kEffMoney, 0, kNone, kNone, -3000), eff(kEffChance, 0, kNone, kNone, 0, 1.0f),
                                   eff(kEffChance, 0, kNone, kNone, 0, 0.0f)};
        const uint16_t b = add_choice(t, bribe, 3, req, 1);
        const EffectRec revenge[] = {eff(kEffScheduleEvent, 0, kEvRevenge, kNone, 7)};
        add_then(t, static_cast<uint16_t>(b + 1), revenge, 1);
        const EffectRec never[] = {eff(kEffRep, 0, kNone, kNone, 0, 50.0f)};
        add_then(t, static_cast<uint16_t>(b + 2), never, 1);
        const EffectRec del[] = {eff(kEffUninstall, 0), eff(kEffRep, 0, kNone, kNone, 0, -1.0f)};
        add_choice(t, del, 2);
    }
    end_choices(t, lic);

    EventRec& rev = t.events[kEvRevenge] = event(kTrigScheduled, 0xFF, kPresentPhone);
    begin_choices(t, rev);
    {
        const EffectRec fx[] = {eff(kEffDamagePc, 0, 0, kCompPsu),
                                eff(kEffModifier, kModHwPrice, kNone, kNone, 3, 1.5f)};
        add_choice(t, fx, 2);
    }
    end_choices(t, rev);

    EventRec& dt = t.events[kEvDate] = event(kTrigDate, 0xFF, kPresentWorld);
    dt.trigger_date = start + 2;
    gate(t, dt, kFGateDate);
    begin_choices(t, dt);
    {
        const EffectRec fx[] = {eff(kEffItem, 0, kCompCpu, kNone, 2),     eff(kEffItem, 1, 0, kNone, -5),
                                eff(kEffUnlock, 0, kUpAircon),             eff(kEffUnlock, 1, kGameRts),
                                eff(kEffStartMinigame, 1, kSvcClean),      eff(kEffMoney, 0, kNone, kNone, 500)};
        add_choice(t, fx, 6);
    }
    end_choices(t, dt);

    EventRec& st = t.events[kEvState] = event(kTrigOnState, 0xFF, kPresentPhone);
    st.cond_first = add_cond(t, kVarMoney, kOpGe, kNone, 15000.0f);
    add_cond(t, kVarFlag, kOpEq, kFGateState, 1.0f);
    st.cond_count = 2;
    begin_choices(t, st);
    {
        const EffectRec fx[] = {eff(kEffFlag, 0, kFRich, kNone, 1),
                                eff(kEffModifier, kModArrivals, kNone, kArchSchool, 2, 2.0f),
                                eff(kEffModifier, kModPop, kNone, kGameShooter, 1, 0.5f),
                                eff(kEffModifier, kModDust, kNone, kNone, 1, 3.0f)};
        add_choice(t, fx, 4);
    }
    end_choices(t, st);

    EventRec& act = t.events[kEvAction] = event(kTrigOnAction, 0xFF, kPresentVisitor);
    act.trigger_action = kActUpgradeBought;
    act.once = 1;
    gate(t, act, kFGateAction);
    begin_choices(t, act);
    {
        const EffectRec fx[] = {eff(kEffSpawnVisitor, 0, kArchSchool), eff(kEffSpawnVisitor, 1, kRegDimon),
                                eff(kEffRelation, 0, kRegDimon, kNone, 10)};
        add_choice(t, fx, 3);
    }
    end_choices(t, act);

    EventRec& reg = t.events[kEvRegular] = event(kTrigOnState, 0xFF, kPresentVisitor);
    reg.category = kCatRegular;
    gate(t, reg, kFRich);
    begin_choices(t, reg);
    {
        const EffectRec fx[] = {eff(kEffRelation, 0, kRegDimon, kNone, -5), eff(kEffDamagePc)};
        add_choice(t, fx, 2);
    }
    end_choices(t, reg);

    for (int i = kEvW1; i <= kEvW2; ++i) {
        EventRec& e = t.events[i] = event(kTrigDailyRoll, 13, kPresentPhone);
        e.weight = i == kEvW1 ? 1.0f : 3.0f;
        gate(t, e, kFGateWeights);
        begin_choices(t, e);
        add_choice(t, nullptr, 0);
        end_choices(t, e);
    }

    EconomyRec& ec = t.economy;
    ec.start_date = start;
    ec.start_money = 10000;
    ec.start_rep = 30.0f;
    ec.start_price_hour = 20;
    ec.start_price_night = 100;
    ec.electricity_price = 1.0f;
    ec.loan_rate_week = 0.1f;
    ec.debt_limit = 30000;
    ec.debt_days_to_lose = 14;
    ec.start_upgrades_first = 0;
    ec.start_upgrades_count = 1;
    ec.start_pcs = 2;
    for (int k = 0; k < kComponentCount; ++k) ec.start_pc[k] = k <= kCompKeyboard ? static_cast<Index>(k) : kNone;

    GameData& d = t.data;
    d = GameData{};
    d.games = t.games;
    d.game_count = kGames;
    d.archetypes = t.archetypes;
    d.archetype_count = kArchs;
    d.regulars = t.regulars;
    d.regular_count = kRegs;
    d.hardware = t.hardware;
    d.hardware_count = kComponentCount;
    d.goods = t.goods;
    d.goods_count = 1;
    d.upgrades = t.upgrades;
    d.upgrade_count = kUps;
    d.events = t.events;
    d.event_count = kEvs;
    d.choices = t.choices;
    d.choice_count = static_cast<uint16_t>(t.n_choice);
    d.effects = t.effects;
    d.effect_count = static_cast<uint16_t>(t.n_eff);
    d.conds = t.conds;
    d.cond_count = static_cast<uint16_t>(t.n_cond);
    d.chance_terms = t.chance;
    d.chance_term_count = 1;
    d.lists = t.lists;
    d.list_count = 2;
    d.flag_count = kFlags;
    d.economy = &t.economy;
}

EvData g_data;
World g_world;

World& fresh(uint32_t seed = 7) {
    make_data(g_data);
    world_init(g_world, g_data.data, seed);
    return g_world;
}

void set_flag(World& w, int f) { w.events.flags[f >> 5] |= 1u << (f & 31); }

// Тикать (закрывая дни) до дня day_index и минуты minute включительно.
void advance(World& w, int day, int minute) {
    for (int guard = 0; guard < 400 * kMinutesPerDay; ++guard) {
        if (w.clock.day_index > day || (w.clock.day_index == day && w.clock.minute >= minute)) return;
        if (w.clock.phase == Phase::kClosed) {
            cmd_end_day(w);
        } else {
            tick(w);
        }
    }
    FAIL("advance: время не дошло");
}

int active_count(const World& w) {
    int n = 0;
    for (const ActiveEvent& a : w.events.active) n += a.event != kNone ? 1 : 0;
    return n;
}

int find_active(const World& w, Index ev) {
    for (int i = 0; i < kMaxActiveEvents; ++i) {
        if (w.events.active[i].event == ev) return i;
    }
    return -1;
}

void drain(World& w) {
    Notice buf[kMaxNotices];
    take_notices(w, buf, kMaxNotices);
}

int count_notices(World& w, NoticeKind kind, Notice* last = nullptr) {
    Notice buf[kMaxNotices];
    const int n = take_notices(w, buf, kMaxNotices);
    int c = 0;
    for (int i = 0; i < n; ++i) {
        if (buf[i].kind != kind) continue;
        ++c;
        if (last) *last = buf[i];
    }
    return c;
}

void install_pirate(World& w, uint8_t pc, Index game) {
    w.library.pirate[game >> 5] |= 1u << (game & 31);
    REQUIRE(cmd_install(w, pc, game, true, 1.0f) == CmdResult::kOk);
}

}  // namespace

TEST_CASE("daily_roll: шанс из chance.per, кулдаун, письмо; money expr, rep, uninstall") {
    World& w = fresh();
    set_flag(w, kFGateLicense);
    advance(w, 2, 12 * 60);
    CHECK(find_active(w, kEvLicense) < 0);  // пиратских установок нет → p = 0

    install_pirate(w, 0, kGameShooter);
    install_pirate(w, 1, kGameShooter);
    REQUIRE(pirate_installs(w) == 2);
    advance(w, 3, 11 * 60 - 1);
    CHECK(find_active(w, kEvLicense) < 0);  // ещё 10:59
    drain(w);
    advance(w, 3, 11 * 60);
    const int slot = find_active(w, kEvLicense);
    REQUIRE(slot >= 0);  // p = 0 + 0,5 · 2 = 1
    Notice n{};
    CHECK(count_notices(w, NoticeKind::kEventPresented, &n) == 1);
    CHECK(n.ref == kEvLicense);
    CHECK(n.a == slot);
    const ActiveEvent& ae = w.events.active[slot];
    REQUIRE(ae.mail < w.mailbox.count);
    const Mail& m = w.mailbox.mail[ae.mail];
    CHECK(m.kind == MailKind::kEvent);
    CHECK(m.ref == kEvLicense);
    CHECK(w.events.cooldown_until[kEvLicense] == 3 + 3);

    // Штраф: −5000 · pirate_installs, денег 1000 → остальное в долг (§5.4).
    w.economy.money = 1000;
    const float rep = w.economy.rep;
    const uint8_t mail = ae.mail;
    CHECK_FALSE(event_choice_available(w, static_cast<uint8_t>(slot), 1));  // requires money ≥ 3000
    CHECK(cmd_event_choose(w, static_cast<uint8_t>(slot), 1) == CmdResult::kInvalid);
    CHECK(cmd_event_choose(w, static_cast<uint8_t>(slot), 3) == CmdResult::kInvalid);
    REQUIRE(cmd_event_choose(w, static_cast<uint8_t>(slot), 0) == CmdResult::kOk);
    CHECK(w.economy.money == 0);
    CHECK(w.economy.debt == 9000);
    CHECK(w.stats.today.expenses[kExpEvents] == 10000);
    CHECK(w.economy.rep == doctest::Approx(rep - 2.0f));
    CHECK(w.mailbox.mail[mail].answered == 1);
    CHECK(w.mailbox.mail[mail].choice == 0);
    CHECK(w.mailbox.mail[mail].read == 1);
    CHECK(active_count(w) == 0);
    CHECK(cmd_event_choose(w, static_cast<uint8_t>(slot), 0) == CmdResult::kInvalid);  // слот пуст

    // Кулдаун 3 дня: дни 4–5 молчат, день 6 — снова.
    advance(w, 5, 23 * 60);
    CHECK(find_active(w, kEvLicense) < 0);
    advance(w, 6, 11 * 60 + 1);
    const int again = find_active(w, kEvLicense);
    REQUIRE(again >= 0);
    REQUIRE(cmd_event_choose(w, static_cast<uint8_t>(again), 2) == CmdResult::kOk);  // удалить всё
    CHECK(pirate_installs(w) == 0);
    CHECK_FALSE(pc_has_game(w, 0, kGameShooter));
}

TEST_CASE("chance, schedule_event → scheduled; damage_pc, modifier hw_price") {
    World& w = fresh();
    set_flag(w, kFGateLicense);
    install_pirate(w, 0, kGameShooter);
    install_pirate(w, 1, kGameRts);
    advance(w, 2, 11 * 60 + 1);
    const int slot = find_active(w, kEvLicense);
    REQUIRE(slot >= 0);
    w.economy.money = 5000;
    const float rep = w.economy.rep;
    REQUIRE(event_choice_available(w, static_cast<uint8_t>(slot), 1));
    REQUIRE(cmd_event_choose(w, static_cast<uint8_t>(slot), 1) == CmdResult::kOk);  // взятка
    CHECK(w.economy.money == 2000);
    CHECK(w.economy.rep == doctest::Approx(rep));  // chance p = 0 не сработал
    const ScheduledEvent* s = nullptr;
    for (const ScheduledEvent& x : w.events.scheduled) {
        if (x.event == kEvRevenge) s = &x;
    }
    REQUIRE(s != nullptr);  // chance p = 1 → schedule_event
    CHECK(s->date == w.clock.date + 7);
    CHECK(s->hour == kEventDefaultHour);

    w.events.flags[0] = 0;  // проверка лицензий больше не мешает
    advance(w, 9, kEventDefaultHour * 60 - 1);
    CHECK(find_active(w, kEvRevenge) < 0);
    advance(w, 9, kEventDefaultHour * 60);
    const int rv = find_active(w, kEvRevenge);
    REQUIRE(rv >= 0);
    for (const ScheduledEvent& x : w.events.scheduled) CHECK(x.event == kNone);

    const int32_t price = current_hardware_price(w, kCompCpu);
    REQUIRE(cmd_event_choose(w, static_cast<uint8_t>(rv), 0) == CmdResult::kOk);
    CHECK(w.pcs[0].state == PcState::kBroken);
    CHECK(w.pcs[0].broken_component == kCompPsu);
    CHECK(w.pcs[1].state == PcState::kIdle);
    CHECK(current_hardware_price(w, kCompCpu) > price);  // ×1,5
    CHECK(cmd_repair(w, 0, kCompPsu, 1.0f) == CmdResult::kOk);  // ремонт возможен
    advance(w, 11, 10 * 60);
    CHECK(current_hardware_price(w, kCompCpu) > price);  // 3 дня, считая день 9
    advance(w, 12, 10 * 60);
    CHECK(current_hardware_price(w, kCompCpu) == price);
}

TEST_CASE("date: в свой день и час; item, unlock, start_minigame, money > 0") {
    World& w = fresh();
    set_flag(w, kFGateDate);
    w.inventory.goods[0] = 3;
    advance(w, 3, kEventDefaultHour * 60 - 1);
    CHECK(find_active(w, kEvDate) < 0);
    advance(w, 3, kEventDefaultHour * 60);
    const int slot = find_active(w, kEvDate);
    REQUIRE(slot >= 0);
    drain(w);
    const int32_t money = w.economy.money;
    REQUIRE(cmd_event_choose(w, static_cast<uint8_t>(slot), 0) == CmdResult::kOk);
    CHECK(w.inventory.hardware[kCompCpu] == 2);
    CHECK(w.inventory.goods[0] == 0);  // −5 при трёх → 0
    CHECK(upgrade_owned(w, kUpAircon));
    CHECK(w.library.licenses[kGameRts] == 1);
    CHECK(free_licenses(w, kGameRts) == 1);
    Notice n{};
    CHECK(count_notices(w, NoticeKind::kEventMinigame, &n) == 1);
    CHECK(n.a == 1);
    CHECK(n.ref == kSvcClean);
    CHECK(w.economy.money == money + 500);
    CHECK(w.stats.today.revenue[kRevEvents] == 500);

    advance(w, 30, 12 * 60);
    CHECK(active_count(w) == 0);  // другая дата — не повторяется
}

TEST_CASE("on_state: фронт условий, ожидание слота; flag и модификаторы arrivals/pop/dust") {
    World& w = fresh();
    set_flag(w, kFGateState);
    advance(w, 1, 10 * 60 + 30);
    CHECK(active_count(w) == 0);  // денег 10000 < 15000

    // Все слоты заняты — событие ждёт, а не теряется.
    for (ActiveEvent& a : w.events.active) a.event = kEvW1;
    w.economy.money = 20000;
    advance(w, 1, 12 * 60 + 30);
    CHECK(find_active(w, kEvState) < 0);
    w.events.active[5] = ActiveEvent{};
    advance(w, 1, 13 * 60 + 1);
    CHECK(find_active(w, kEvState) == 5);
    for (ActiveEvent& a : w.events.active) a = ActiveEvent{};

    // Повторно не срабатывает, пока условия не станут ложными.
    advance(w, 1, 16 * 60 + 1);
    CHECK(active_count(w) == 0);
    w.economy.money = 0;
    advance(w, 1, 17 * 60 + 1);
    w.economy.money = 20000;
    advance(w, 1, 18 * 60 + 1);
    const int slot = find_active(w, kEvState);
    REQUIRE(slot >= 0);

    const float pop = game_popularity_today(w, kGameShooter);
    REQUIRE(cmd_event_choose(w, static_cast<uint8_t>(slot), 0) == CmdResult::kOk);
    CHECK(flag_set(w, kFRich));
    CHECK(game_popularity_today(w, kGameShooter) == doctest::Approx(pop * 0.5f));
    int arrivals = 0, dust = 0;
    for (const Modifier& m : w.modifiers) {
        if (m.target == kModArrivals) {
            ++arrivals;
            CHECK(m.arg == kArchSchool);
            CHECK(m.mult == doctest::Approx(2.0f));
            CHECK(m.until == w.clock.date + 1);
        }
        if (m.target == kModDust) {
            ++dust;
            CHECK(m.mult == doctest::Approx(3.0f));
            CHECK(m.until == w.clock.date);
        }
    }
    CHECK(arrivals == 1);
    CHECK(dust == 1);
    advance(w, 2, 9 * 60);
    CHECK(game_popularity_today(w, kGameShooter) > pop * 0.9f);  // pop-модификатор на 1 день снят
}

TEST_CASE("on_action + once; spawn_visitor (архетип и постоянный), relation; event_pool ждёт клиента") {
    World& w = fresh();
    set_flag(w, kFGateAction);
    set_flag(w, kFRich);  // ворота события Димона: ждёт только его прихода
    REQUIRE(cmd_buy_upgrade(w, kUpNight) == CmdResult::kOk);
    const int slot = find_active(w, kEvAction);
    REQUIRE(slot >= 0);  // сразу, из команды
    CHECK(w.events.active[slot].visitor == kNoVisitor);
    REQUIRE(cmd_buy_upgrade(w, kUpAd) == CmdResult::kOk);
    CHECK(active_count(w) == 1);  // once

    advance(w, 1, 11 * 60);
    CHECK(find_active(w, kEvRegular) < 0);  // Димона нет в клубе
    drain(w);
    REQUIRE(cmd_event_choose(w, static_cast<uint8_t>(slot), 0) == CmdResult::kOk);
    int school = 0;
    uint8_t dimon_slot = kNoVisitor;
    for (int i = 0; i < kMaxVisitors; ++i) {
        const Visitor& v = w.visitors[i];
        if (v.state == VisitorState::kNone) continue;
        if (v.regular == kRegDimon) {
            dimon_slot = static_cast<uint8_t>(i);
        } else {
            CHECK(v.archetype == kArchSchool);
            ++school;
        }
    }
    CHECK(school == 1);
    REQUIRE(dimon_slot != kNoVisitor);
    CHECK(w.regulars[kRegDimon].unlocked == 1);
    CHECK(w.regulars[kRegDimon].in_club == 1);
    CHECK(w.regulars[kRegDimon].relation == 10);
    CHECK(count_notices(w, NoticeKind::kRegularUnlocked) == 1);

    advance(w, 1, 12 * 60 + 1);
    const int reg = find_active(w, kEvRegular);
    REQUIRE(reg >= 0);
    CHECK(w.events.active[reg].visitor == dimon_slot);
    REQUIRE(cmd_event_choose(w, static_cast<uint8_t>(reg), 0) == CmdResult::kOk);
    CHECK(w.regulars[kRegDimon].relation == 5);
    int broken = 0;  // damage_pc random/random — ровно один ПК
    for (int pc = 0; pc < w.club.seats; ++pc) broken += w.pcs[pc].state == PcState::kBroken ? 1 : 0;
    CHECK(broken == 1);
}

TEST_CASE("spawn_visitor вне часов работы не действует") {
    World& w = fresh();
    set_flag(w, kFGateAction);
    REQUIRE(cmd_buy_upgrade(w, kUpAd) == CmdResult::kOk);
    const int slot = find_active(w, kEvAction);
    REQUIRE(slot >= 0);
    REQUIRE(w.clock.phase == Phase::kMorning);
    REQUIRE(cmd_event_choose(w, static_cast<uint8_t>(slot), 0) == CmdResult::kOk);
    for (const Visitor& v : w.visitors) CHECK(v.state == VisitorState::kNone);
    CHECK(w.regulars[kRegDimon].unlocked == 0);
    CHECK(w.regulars[kRegDimon].relation == 10);  // остальные эффекты применились
}

TEST_CASE("daily_roll: не больше одного события в час, выбор по weight; детерминизм") {
    int counts[2][2] = {};
    for (int run = 0; run < 2; ++run) {
        World& w = fresh(12345);
        set_flag(w, kFGateWeights);
        for (int day = 1; day <= 40; ++day) {
            advance(w, day, 13 * 60 + 1);
            REQUIRE(active_count(w) == 1);
            const int s1 = find_active(w, kEvW1), s2 = find_active(w, kEvW2);
            ++counts[run][s1 >= 0 ? 0 : 1];
            REQUIRE(cmd_event_choose(w, static_cast<uint8_t>(s1 >= 0 ? s1 : s2), 0) == CmdResult::kOk);
        }
    }
    CHECK(counts[0][0] + counts[0][1] == 40);
    CHECK(counts[0][0] > 0);
    CHECK(counts[0][1] > counts[0][0]);  // weight 3 против 1
    CHECK(counts[1][0] == counts[0][0]);  // тот же сид — те же события
}

TEST_CASE("cmd_event_choose: границы и kGameOver") {
    World& w = fresh();
    CHECK(cmd_event_choose(w, 0, 0) == CmdResult::kInvalid);
    CHECK(cmd_event_choose(w, kMaxActiveEvents, 0) == CmdResult::kInvalid);
    CHECK_FALSE(event_choice_available(w, 0, 0));
    w.clock.phase = Phase::kGameOver;
    CHECK(cmd_event_choose(w, 0, 0) == CmdResult::kWrongPhase);
}
