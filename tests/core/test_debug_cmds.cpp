// DBG: отладочные команды ядра на настоящем data.pak — каждая меняет мир как обещано.
#include <doctest/doctest.h>

#include <cstdio>
#include <vector>

#include "core/core_api.h"
#include "core/data_api.h"
#include "core/debug_cmds.h"
#include "core/save_api.h"

using namespace core;

namespace {

struct Pak {
    std::vector<uint8_t> bytes;
    GameData data{};
    Pak() {
        std::FILE* f = std::fopen(PCCLUB_DATA_PAK, "rb");
        REQUIRE(f != nullptr);
        uint8_t buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
        std::fclose(f);
        bytes.resize((bytes.size() + 15) & ~size_t(15));
        REQUIRE(data_load(bytes.data(), bytes.size(), &data) == DataError::kOk);
    }
};

const GameData& pak() {
    static Pak p;
    return p.data;
}

World& fresh(uint32_t seed = 1) {
    static World w;
    world_init(w, pak(), seed);
    return w;
}

int installed_games(const World& w, uint8_t pc) {
    int n = 0;
    for (int g = 0; g < w.data->game_count; ++g) n += pc_has_game(w, pc, static_cast<Index>(g)) ? 1 : 0;
    return n;
}

// Тики до первого в очереди (не дольше суток).
bool until_queue(World& w) {
    for (int i = 0; i < kMinutesPerDay && w.queue_len == 0; ++i) tick(w);
    return w.queue_len > 0;
}

}  // namespace

TEST_CASE("DBG: деньги +1 000 / +10 000") {
    World& w = fresh();
    const int32_t before = w.economy.money;
    CHECK(debug_add_money(w, 1000) == CmdResult::kOk);
    CHECK(debug_add_money(w, 10000) == CmdResult::kOk);
    CHECK(w.economy.money == before + 11000);
}

TEST_CASE("DBG: промотка — до открытия, до закрытия, до утра следующего дня") {
    World& w = fresh();
    REQUIRE(w.clock.phase == Phase::kMorning);
    const int32_t day = w.clock.day_index;

    CHECK(debug_skip(w, DebugSkip::kToOpen) == CmdResult::kOk);
    CHECK(w.clock.phase == Phase::kOpen);
    CHECK(w.clock.minute == kOpenMinute);
    CHECK(w.clock.day_index == day);

    CHECK(debug_skip(w, DebugSkip::kToClose) == CmdResult::kOk);
    CHECK(w.clock.phase == Phase::kClosed);
    CHECK(w.clock.day_index == day);
    CHECK(can_save(w));  // здесь срабатывает автосохранение при закрытии

    CHECK(debug_skip(w, DebugSkip::kToMorning) == CmdResult::kOk);
    CHECK(w.clock.phase == Phase::kMorning);
    CHECK(w.clock.minute == kMorningMinute);
    CHECK(w.clock.day_index == day + 1);
    CHECK(w.stats.count == 1);  // день закрыт по-настоящему: итоги в архиве

    // Из утра «до утра» — следующее утро; из открытого «до открытия» — завтрашнее открытие.
    CHECK(debug_skip(w, DebugSkip::kToMorning) == CmdResult::kOk);
    CHECK(w.clock.phase == Phase::kMorning);
    CHECK(w.clock.day_index == day + 2);
    debug_skip(w, DebugSkip::kToOpen);
    CHECK(debug_skip(w, DebugSkip::kToOpen) == CmdResult::kOk);
    CHECK(w.clock.phase == Phase::kOpen);
    CHECK(w.clock.day_index == day + 3);

    w.clock.phase = Phase::kGameOver;
    CHECK(debug_skip(w, DebugSkip::kToMorning) == CmdResult::kWrongPhase);
}

TEST_CASE("DBG: ускорение ×4/×16 не сбрасывается очередью, ×1 — обычные правила") {
    World& w = fresh();
    debug_skip(w, DebugSkip::kToOpen);
    CHECK(debug_minutes_per_real_second(w, 1) == doctest::Approx(minutes_per_real_second(w)));
    CHECK(debug_minutes_per_real_second(w, 4) == doctest::Approx(8.0f));
    CHECK(debug_minutes_per_real_second(w, 16) == doctest::Approx(32.0f));
    REQUIRE(until_queue(w));
    CHECK(minutes_per_real_second(w) == doctest::Approx(2.0f));  // GDD §4: при очереди ×1
    CHECK(debug_minutes_per_real_second(w, 16) == doctest::Approx(32.0f));
    debug_skip(w, DebugSkip::kToClose);
    CHECK(debug_minutes_per_real_second(w, 16) == doctest::Approx(0.0f));
}

TEST_CASE("DBG: посетитель — только когда открыто, доходит до очереди") {
    World& w = fresh();
    CHECK(debug_spawn_visitor(w) == CmdResult::kWrongPhase);  // утро: клуб закрыт
    debug_skip(w, DebugSkip::kToOpen);
    REQUIRE(w.queue_len == 0);
    CHECK(debug_spawn_visitor(w) == CmdResult::kOk);
    for (int i = 0; i < 2 * kTransitMinutes && w.queue_len == 0; ++i) tick(w);
    CHECK(w.queue_len >= 1);
    for (int i = 0; i < kMaxQueue + 2; ++i) debug_spawn_visitor(w);
    CHECK(debug_spawn_visitor(w) == CmdResult::kLimit);  // очередь (с идущими к стойке) полна
}

TEST_CASE("DBG: все игры на все ПК — посетитель садится за свою игру") {
    World& w = fresh(7);
    REQUIRE(w.club.seats > 0);
    for (int pc = 0; pc < w.club.seats; ++pc) REQUIRE(installed_games(w, static_cast<uint8_t>(pc)) == 0);
    debug_skip(w, DebugSkip::kToOpen);
    REQUIRE(until_queue(w));
    const uint8_t v = w.queue[0];
    // Без игр посадить некуда.
    CHECK(best_pc_for(w, v) == kNoPc);

    const int n = debug_install_all(w);
    CHECK(n > 0);
    int released = 0;
    for (int g = 0; g < w.data->game_count; ++g) released += game_released(w, static_cast<Index>(g)) ? 1 : 0;
    for (int pc = 0; pc < w.club.seats; ++pc) {
        if (w.pcs[pc].state == PcState::kEmpty) continue;
        CHECK(installed_games(w, static_cast<uint8_t>(pc)) == released);
    }
    CHECK(debug_install_all(w) == 0);  // второй раз ставить нечего

    const uint8_t pc = best_pc_for(w, v);
    REQUIRE(pc != kNoPc);
    CHECK(pc_has_game(w, pc, w.visitors[v].want_game));
    const ServeResult r = cmd_serve(w, v, pc, Tariff::kHourly);
    CHECK((r == ServeResult::kSeated || r == ServeResult::kLeftPrice));
    if (r == ServeResult::kSeated) {
        CHECK(w.pcs[pc].state == PcState::kBusy);
        CHECK(w.visitors[v].game == w.visitors[v].want_game);
    }
}

TEST_CASE("DBG: ПК — сломать, повесить, запылить, починить") {
    World& w = fresh();
    const uint8_t pc = 0;
    REQUIRE(w.pcs[pc].state == PcState::kIdle);

    CHECK(debug_dust_pc(w, pc) == CmdResult::kOk);
    CHECK(w.pcs[pc].dust >= 70.0f);

    CHECK(debug_hang_pc(w, pc) == CmdResult::kOk);
    CHECK(w.pcs[pc].state == PcState::kHung);
    CHECK(debug_hang_pc(w, pc) == CmdResult::kInvalid);   // уже висит
    CHECK(debug_break_pc(w, pc) == CmdResult::kInvalid);  // ломается только работающий
    CHECK(cmd_reboot(w, pc, 1.0f) == CmdResult::kOk);     // обычная перезагрузка снимает
    CHECK(w.pcs[pc].state == PcState::kIdle);

    CHECK(debug_break_pc(w, pc) == CmdResult::kOk);
    CHECK(w.pcs[pc].state == PcState::kBroken);
    CHECK(w.pcs[pc].broken_component < kComponentCount);
    CHECK(w.stats.today.pc_failures == 1);
    CHECK(w.stats.today.pc_hangs == 1);

    w.pcs[pc].wear[kCompCpu] = 55.0f;
    CHECK(debug_fix_pc(w, pc) == CmdResult::kOk);
    CHECK(w.pcs[pc].state == PcState::kIdle);
    CHECK(w.pcs[pc].dust == 0.0f);
    CHECK(w.pcs[pc].wear[kCompCpu] == 0.0f);

    CHECK(debug_break_pc(w, kMaxPcs) == CmdResult::kInvalid);
    CHECK(debug_fix_pc(w, w.club.seats) == CmdResult::kInvalid);
}

TEST_CASE("DBG: починить занятый ПК — посетитель снова играет, просьба снята") {
    World& w = fresh(7);
    debug_skip(w, DebugSkip::kToOpen);
    debug_install_all(w);
    ServeResult r = ServeResult::kInvalid;
    uint8_t v = kNoVisitor, pc = kNoPc;
    for (int tries = 0; tries < 20 && r != ServeResult::kSeated; ++tries) {
        REQUIRE(until_queue(w));
        v = w.queue[0];
        pc = best_pc_for(w, v);
        REQUIRE(pc != kNoPc);
        r = cmd_serve(w, v, pc, Tariff::kHourly);
    }
    REQUIRE(r == ServeResult::kSeated);
    CHECK(debug_hang_pc(w, pc) == CmdResult::kOk);
    CHECK(w.visitors[v].request == Request::kHang);
    CHECK(debug_fix_pc(w, pc) == CmdResult::kOk);
    CHECK(w.pcs[pc].state == PcState::kBusy);
    CHECK(w.visitors[v].request == Request::kNone);
    CHECK(debug_break_pc(w, pc) == CmdResult::kOk);
    CHECK(w.visitors[v].request == Request::kBroken);
    CHECK(debug_fix_pc(w, pc) == CmdResult::kOk);
    CHECK(w.pcs[pc].state == PcState::kBusy);
    CHECK(w.pcs[pc].running_game == w.visitors[v].game);
}

TEST_CASE("DBG: событие по выбору — письмом в Почте, ответ работает") {
    World& w = fresh();
    REQUIRE(w.data->event_count > 0);
    const uint8_t mail_before = w.mailbox.count;
    CHECK(debug_fire_event(w, 0) == CmdResult::kOk);
    int slot = -1;
    for (int i = 0; i < kMaxActiveEvents; ++i)
        if (w.events.active[i].event == 0) slot = i;
    REQUIRE(slot >= 0);
    CHECK(w.mailbox.count == mail_before + 1);
    CHECK(w.events.active[slot].mail == w.mailbox.count - 1);
    CHECK(w.mailbox.mail[w.events.active[slot].mail].kind == MailKind::kEvent);
    // Первый доступный вариант применяется и освобождает слот.
    int choice = -1;
    for (int c = 0; c < w.data->events[0].choice_count && choice < 0; ++c)
        if (event_choice_available(w, static_cast<uint8_t>(slot), static_cast<uint8_t>(c))) choice = c;
    REQUIRE(choice >= 0);
    CHECK(cmd_event_choose(w, static_cast<uint8_t>(slot), static_cast<uint8_t>(choice)) == CmdResult::kOk);
    CHECK(w.events.active[slot].event == kNone);

    CHECK(debug_fire_event(w, static_cast<Index>(w.data->event_count)) == CmdResult::kInvalid);
    // Все слоты заняты — kLimit, мир не меняется.
    for (int i = 0; i < kMaxActiveEvents; ++i) REQUIRE(debug_fire_event(w, 0) == CmdResult::kOk);
    const uint8_t mails = w.mailbox.count;
    CHECK(debug_fire_event(w, 0) == CmdResult::kLimit);
    CHECK(w.mailbox.count == mails);
}

TEST_CASE("DBG: после промотки до утра мир сохраняется и загружается") {
    World& w = fresh();
    debug_skip(w, DebugSkip::kToOpen);
    debug_add_money(w, 12345);
    debug_install_all(w);
    debug_skip(w, DebugSkip::kToMorning);
    alignas(4) static uint8_t buf[kSaveMaxSize];
    size_t size = 0;
    REQUIRE(save_world(w, buf, sizeof(buf), &size) == SaveError::kOk);
    static World loaded;
    REQUIRE(load_world(loaded, pak(), buf, size) == SaveError::kOk);
    CHECK(loaded.economy.money == w.economy.money);
    CHECK(loaded.clock.day_index == w.clock.day_index);
    CHECK(installed_games(loaded, 0) == installed_games(w, 0));
    // «Стёртый» слот (16 нулей, debug_host.cpp) не читается — значит, новая игра.
    const uint8_t empty[16] = {};
    CHECK(load_world(loaded, pak(), empty, sizeof(empty)) != SaveError::kOk);
}
