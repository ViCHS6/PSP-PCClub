#include <doctest/doctest.h>

#include <cstdio>
#include <vector>

#include "tools/minigame_runner_input.h"

TEST_CASE("runner: script holds buttons, creates edges and restarts identically") {
    const std::vector<minigame_runner::InputEvent> events = {
        {0, platform::kButtonUp},
        {2, platform::kButtonUp | platform::kButtonCross},
        {3, 0}, {5, platform::kButtonStart},
    };
    for (int repeat = 0; repeat < 2; ++repeat) {
        size_t next = 0;
        uint32_t held = 0;
        auto input = minigame_runner::scripted_input(events, 0, next, held);
        CHECK(input.held == platform::kButtonUp);
        CHECK(input.pressed == platform::kButtonUp);
        input = minigame_runner::scripted_input(events, 1, next, held);
        CHECK(input.held == platform::kButtonUp);
        CHECK(input.pressed == 0);
        input = minigame_runner::scripted_input(events, 2, next, held);
        CHECK(input.pressed == platform::kButtonCross);
        CHECK(input.released == 0);
        input = minigame_runner::scripted_input(events, 3, next, held);
        CHECK(input.held == 0);
        CHECK(input.released == (platform::kButtonUp | platform::kButtonCross));
        input = minigame_runner::scripted_input(events, 4, next, held);
        CHECK(input.released == 0);
        input = minigame_runner::scripted_input(events, 5, next, held);
        CHECK(input.pressed == platform::kButtonStart);
    }
}

namespace {
struct ScriptFile {
    const char* path = "test_snake_runner_input.txt";
    ~ScriptFile() { std::remove(path); }
    void write(const char* contents) const {
        FILE* file = std::fopen(path, "wb");
        REQUIRE(file);
        std::fputs(contents, file);
        std::fclose(file);
    }
};
}  // namespace

TEST_CASE("runner: parser accepts combined buttons, comments and CRLF") {
    ScriptFile script;
    script.write("# input\r\n0 up+cross # hold\r\n12 -\r\n15 down,triangle square\n");
    std::vector<minigame_runner::InputEvent> events;
    REQUIRE(minigame_runner::load_input(script.path, events));
    REQUIRE(events.size() == 3);
    CHECK(events[0].frame == 0);
    CHECK(events[0].held == (platform::kButtonUp | platform::kButtonCross));
    CHECK(events[1].frame == 12);
    CHECK(events[1].held == 0);
    CHECK(events[2].held == (platform::kButtonDown | platform::kButtonTriangle | platform::kButtonSquare));
}

TEST_CASE("runner: malformed or unordered scripts fail instead of silently changing input") {
    ScriptFile script;
    for (const char* contents : {"-1 up\n", "4294967296 up\n", "1x up\n", "1\n",
                                "0 unknown\n", "0 - cross\n", "2 up\n1 down\n", "1 up\n1 -\n"}) {
        script.write(contents);
        std::vector<minigame_runner::InputEvent> events;
        CHECK_FALSE(minigame_runner::load_input(script.path, events));
        CHECK(events.empty());
    }
}
