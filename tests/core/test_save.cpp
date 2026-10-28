// T060: сохранение мира — круговая сериализация, продолжение бит в бит, битые и чужие файлы.
#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "core/core_api.h"
#include "core/data_api.h"
#include "core/save_api.h"

using namespace core;

namespace {

std::vector<uint8_t> read_pak() {
    std::vector<uint8_t> bytes;
    std::FILE* f = std::fopen(PCCLUB_DATA_PAK, "rb");
    if (!f) return bytes;
    uint8_t buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
    std::fclose(f);
    bytes.resize((bytes.size() + 15) & ~size_t(15));
    return bytes;
}

struct Pak {
    std::vector<uint8_t> bytes = read_pak();
    GameData data{};
    Pak() {
        REQUIRE(!bytes.empty());
        REQUIRE(data_load(bytes.data(), bytes.size(), &data) == DataError::kOk);
    }
};

const Pak& pak() {
    static Pak p;
    return p;
}

uint32_t crc32(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

// Правка файла с пересчётом CRC — чтобы дойти до проверок секций и диапазонов.
void fix_crc(std::vector<uint8_t>& f) {
    const uint32_t c = crc32(f.data() + 16, f.size() - 16);
    for (int i = 0; i < 4; ++i) f[12 + i] = static_cast<uint8_t>(c >> (8 * i));
}

// Простой детерминированный бот: обслуживает очередь, перезагружает, отвечает на события.
void step(World& w) {
    if (w.clock.phase == Phase::kClosed) {
        cmd_end_day(w);
        return;
    }
    if (w.queue_len > 0) {
        const uint8_t v = w.queue[0];
        const uint8_t pc = best_pc_for(w, v);
        const ServeResult r = pc != kNoPc ? cmd_serve(w, v, pc, Tariff::kHourly) : ServeResult::kInvalid;
        if (r == ServeResult::kWrongPc || r == ServeResult::kPcUnavailable || r == ServeResult::kInvalid)
            cmd_refuse(w, v);
    }
    for (uint8_t pc = 0; pc < w.club.seats; ++pc)
        if (w.pcs[pc].state == PcState::kHung) cmd_reboot(w, pc, 1.0f);
    for (uint8_t a = 0; a < kMaxActiveEvents; ++a) {
        if (w.events.active[a].event == kNone) continue;
        for (uint8_t ch = 0; ch < 4; ++ch) {
            if (event_choice_available(w, a, ch)) {
                cmd_event_choose(w, a, ch);
                break;
            }
        }
    }
    tick(w);
}

void run_until(World& w, int32_t day, Phase phase) {
    for (int guard = 0; guard < 400 * kMinutesPerDay; ++guard) {
        if (w.clock.day_index == day && w.clock.phase == phase) return;
        step(w);
    }
    FAIL("день не наступил");
}

std::vector<uint8_t> save(const World& w) {
    std::vector<uint8_t> f(kSaveMaxSize);
    size_t size = 0;
    REQUIRE(save_world(w, f.data(), f.size(), &size) == SaveError::kOk);
    f.resize(size);
    return f;
}

World g_a, g_b;

}  // namespace

TEST_CASE("save: круговая сериализация утром — те же байты и поля") {
    const GameData& d = pak().data;
    world_init(g_a, d, 77);
    run_until(g_a, 6, Phase::kMorning);
    REQUIRE(g_a.mailbox.count > 0);
    const std::vector<uint8_t> f = save(g_a);
    CHECK(f.size() < kSaveMaxSize);

    world_init(g_b, d, 1);
    REQUIRE(load_world(g_b, d, f.data(), f.size()) == SaveError::kOk);
    CHECK(save(g_b) == f);
    CHECK(g_b.data == &d);
    CHECK(g_b.rng.state == g_a.rng.state);
    CHECK(g_b.clock.date == g_a.clock.date);
    CHECK(g_b.clock.day_index == 6);
    CHECK(g_b.clock.minute == g_a.clock.minute);
    CHECK(g_b.clock.phase == Phase::kMorning);
    CHECK(g_b.economy.money == g_a.economy.money);
    CHECK(g_b.economy.rep == g_a.economy.rep);
    CHECK(g_b.mailbox.count == g_a.mailbox.count);
    CHECK(g_b.stats.count == g_a.stats.count);
    CHECK(std::memcmp(g_b.pcs, g_a.pcs, sizeof(g_a.pcs)) == 0);
    CHECK(std::memcmp(&g_b.stats, &g_a.stats, sizeof(g_a.stats)) == 0);
    CHECK(std::memcmp(&g_b.events.cooldown_until, &g_a.events.cooldown_until,
                      sizeof(g_a.events.cooldown_until)) == 0);

    SaveInfo info;
    REQUIRE(save_peek(f.data(), f.size(), &info) == SaveError::kOk);
    CHECK(info.date == g_a.clock.date);
    CHECK(info.day_index == 6);
    CHECK(info.phase == Phase::kMorning);
}

TEST_CASE("save: после загрузки утром мир идёт так же, как без неё") {
    const GameData& d = pak().data;
    world_init(g_a, d, 2001);
    run_until(g_a, 4, Phase::kMorning);
    const std::vector<uint8_t> f = save(g_a);
    REQUIRE(load_world(g_b, d, f.data(), f.size()) == SaveError::kOk);
    run_until(g_a, 9, Phase::kMorning);
    run_until(g_b, 9, Phase::kMorning);
    CHECK(save(g_b) == save(g_a));
    CHECK(g_b.economy.money == g_a.economy.money);
    CHECK(g_b.stats.today.arrived == g_a.stats.today.arrived);
}

TEST_CASE("save: при закрытии посетители уходят, игра продолжается") {
    const GameData& d = pak().data;
    world_init(g_a, d, 5);
    run_until(g_a, 3, Phase::kOpen);
    while (g_a.clock.phase != Phase::kClosed) step(g_a);
    const std::vector<uint8_t> f = save(g_a);
    REQUIRE(load_world(g_b, d, f.data(), f.size()) == SaveError::kOk);
    CHECK(g_b.clock.phase == Phase::kClosed);
    CHECK(g_b.queue_len == 0);
    for (const Visitor& v : g_b.visitors) CHECK(v.state == VisitorState::kNone);
    for (const Pc& p : g_b.pcs) {
        CHECK(p.state != PcState::kBusy);
        CHECK(p.visitor == kNoVisitor);
    }
    for (const RegularState& r : g_b.regulars) CHECK(r.in_club == 0);
    CHECK(g_b.stats.today.revenue[kRevHours] == g_a.stats.today.revenue[kRevHours]);
    CHECK(cmd_end_day(g_b) == CmdResult::kOk);
    run_until(g_b, 5, Phase::kMorning);
}

TEST_CASE("save: не в kMorning/kClosed и в маленький буфер — отказ") {
    const GameData& d = pak().data;
    world_init(g_a, d, 3);
    run_until(g_a, 1, Phase::kOpen);
    uint8_t buf[64];
    size_t size = 123;
    std::vector<uint8_t> big(kSaveMaxSize);
    CHECK(save_world(g_a, big.data(), big.size(), &size) == SaveError::kWrongPhase);
    CHECK(size == 0);
    world_init(g_a, d, 3);
    CHECK(save_world(g_a, buf, sizeof(buf), &size) == SaveError::kNoSpace);
    CHECK(size == 0);
}

TEST_CASE("save: битый, обрезанный и чужой файл не меняют мир") {
    const GameData& d = pak().data;
    world_init(g_a, d, 9);
    run_until(g_a, 2, Phase::kMorning);
    const std::vector<uint8_t> good = save(g_a);
    world_init(g_b, d, 42);
    const std::vector<uint8_t> before = save(g_b);

    auto load = [&](const std::vector<uint8_t>& f) { return load_world(g_b, d, f.data(), f.size()); };
    std::vector<uint8_t> f;

    SUBCASE("пусто и меньше заголовка") {
        CHECK(load_world(g_b, d, nullptr, 0) == SaveError::kTooSmall);
        f.assign(good.begin(), good.begin() + 20);
        CHECK(load(f) == SaveError::kTooSmall);
    }
    SUBCASE("обрезан") {
        f.assign(good.begin(), good.end() - 7);
        CHECK(load(f) == SaveError::kBadSize);
        f.assign(good.begin(), good.begin() + good.size() / 2);
        CHECK(load(f) == SaveError::kBadSize);
    }
    SUBCASE("обрезан, но заголовок подогнан") {
        f.assign(good.begin(), good.end() - 7);
        const uint32_t n = static_cast<uint32_t>(f.size());
        for (int i = 0; i < 4; ++i) f[8 + i] = static_cast<uint8_t>(n >> (8 * i));
        fix_crc(f);
        CHECK(load(f) == SaveError::kBadSection);
    }
    SUBCASE("лишние байты") {
        f = good;
        f.push_back(0);
        CHECK(load(f) == SaveError::kBadSize);
    }
    SUBCASE("испорчен байт") {
        f = good;
        f[f.size() / 2] ^= 0x5A;
        CHECK(load(f) == SaveError::kBadCrc);
    }
    SUBCASE("чужой файл") {
        f = good;
        f[0] = 'X';
        CHECK(load(f) == SaveError::kBadMagic);
    }
    SUBCASE("другая версия") {
        f = good;
        f[4] = static_cast<uint8_t>(kSaveVersion + 1);
        CHECK(load(f) == SaveError::kBadVersion);
        f[4] = 0;
        CHECK(load(f) == SaveError::kBadVersion);
    }
    SUBCASE("переставлена секция") {
        f = good;
        f[32 + 62] = 'X';  // тег CLCK: сразу за DATA (8 + 9 · 6 байт)
        fix_crc(f);
        CHECK(load(f) == SaveError::kBadSection);
    }
    SUBCASE("значение вне диапазона") {
        f = good;
        f[32 + 62 + 8] = 7;  // CLCK.weekday
        fix_crc(f);
        CHECK(load(f) == SaveError::kBadValue);
        f = good;
        f[26] = static_cast<uint8_t>(Phase::kOpen);
        fix_crc(f);
        CHECK(load(f) == SaveError::kBadValue);
    }
    CHECK(save(g_b) == before);
}

TEST_CASE("save: data.pak дописан — грузится, переставлен — kDataMismatch") {
    const GameData& full = pak().data;
    REQUIRE(full.game_count > 2);
    REQUIRE(full.goods_count > 1);

    GameData old_data = full;  // «старый» data.pak: последней игры и товара ещё нет
    --old_data.game_count;
    --old_data.goods_count;
    world_init(g_a, old_data, 11);
    const std::vector<uint8_t> old_save = save(g_a);
    REQUIRE(load_world(g_b, full, old_save.data(), old_save.size()) == SaveError::kOk);
    CHECK(g_b.data == &full);
    CHECK(g_b.priv.cache_valid == 0);
    CHECK(g_b.economy.goods_price[full.goods_count - 1] == full.goods[full.goods_count - 1].sell_price);

    world_init(g_a, full, 11);
    const std::vector<uint8_t> f = save(g_a);
    world_init(g_b, full, 42);
    const std::vector<uint8_t> before = save(g_b);
    CHECK(load_world(g_b, old_data, f.data(), f.size()) == SaveError::kDataMismatch);

    std::vector<GameRec> games(full.games, full.games + full.game_count);
    std::swap(games[0], games[1]);
    GameData swapped = full;
    swapped.games = games.data();
    CHECK(load_world(g_b, swapped, f.data(), f.size()) == SaveError::kDataMismatch);
    CHECK(save(g_b) == before);
}
