#include <doctest/doctest.h>

#include <algorithm>
#include <cstdio>

#include "core/core_api.h"
#include "core/data_api.h"
#include "tools/balance_bots.h"

namespace {

struct Fixture {
    alignas(16) uint8_t buffer[1 << 20]{};
    core::GameData data{};
    core::World world{};

    void load() {
        FILE* file = std::fopen(PCCLUB_BALANCE_DATA, "rb");
        REQUIRE(file != nullptr);
        const size_t size = std::fread(buffer, 1, sizeof(buffer), file);
        const bool read_error = std::ferror(file) != 0;
        const bool too_large = std::fgetc(file) != EOF;
        const int close_result = std::fclose(file);
        REQUIRE_FALSE(read_error);
        REQUIRE_FALSE(too_large);
        REQUIRE(close_result == 0);
        REQUIRE(core::data_load(buffer, size, &data) == core::DataError::kOk);
        core::world_init(world, data, 1);
    }
};

int installed_games(const core::World& world, uint8_t pc) {
    int count = 0;
    for (core::Index g = 0; g < world.data->game_count; ++g) {
        if (core::pc_has_game(world, pc, g)) ++count;
    }
    return count;
}

}  // namespace

TEST_CASE("T025-fix: normal spreads licensed games across PCs by the end of day three") {
    // В smoke-паке одна игра: скопление разных игр на двух ПК видно на игровых данных.
    static Fixture fixture;
    fixture.load();
    core::World& world = fixture.world;
    balance::Bot bot;
    bot.strategy = balance::Strategy::kNormal;
    int completed = 0;
    for (int minute = 0; minute < 3 * core::kMinutesPerDay && completed < 3; ++minute) {
        REQUIRE(world.clock.phase != core::Phase::kGameOver);
        if (world.clock.phase == core::Phase::kClosed) {
            ++completed;
            if (completed < 3) REQUIRE(core::cmd_end_day(world) == core::CmdResult::kOk);
        } else {
            balance::bot_act(bot, world);
            core::tick(world);
        }
    }
    REQUIRE(completed == 3);
    int working = 0;
    int least = core::kMaxGames;
    int most = 0;
    for (uint8_t pc = 0; pc < world.club.seats; ++pc) {
        const core::PcState state = world.pcs[pc].state;
        if (state != core::PcState::kIdle && state != core::PcState::kBusy &&
            state != core::PcState::kHung) continue;
        ++working;
        const int count = installed_games(world, pc);
        least = std::min(least, count);
        most = std::max(most, count);
    }
    REQUIRE(working > 2);
    REQUIRE(most > 0);
    bool all_licenses_used = true;
    for (core::Index g = 0; g < world.data->game_count; ++g) {
        if (core::free_licenses(world, g) > 0) all_licenses_used = false;
    }
    CAPTURE(working);
    CAPTURE(least);
    CAPTURE(most);
    CAPTURE(all_licenses_used);
    CHECK((least > 0 || (all_licenses_used && most - least <= 1)));
}
