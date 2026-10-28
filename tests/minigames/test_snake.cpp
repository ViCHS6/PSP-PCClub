#include <doctest/doctest.h>

#include <array>
#include <climits>
#include <cstring>

#include "minigames/snake/snake.h"

namespace {

constexpr float kDt = 0.1f;

struct Attempt {
    alignas(16) std::array<uint8_t, minigame::kStateBudget> arena{};
    minigame::Status status = minigame::Status::kRunning;
    int frames = 0;

    explicit Attempt(uint32_t seed, float difficulty = 1.0f, uint8_t variant = 0,
                     int32_t best = 0) {
        minigame::StartParams params;
        params.seed = seed;
        params.difficulty = difficulty;
        params.variant = variant;
        params.has_best = best != 0;
        params.best_score = best;
        minigame::snake_desc().start(arena.data(), params);
    }

    void tick(uint32_t buttons = 0, float dt = kDt) {
        minigame::Input input;
        input.held = input.pressed = buttons;
        status = minigame::snake_desc().update(arena.data(), dt, input);
        ++frames;
    }

    minigame::Result result() const {
        minigame::Result result;
        minigame::snake_desc().result(arena.data(), &result);
        return result;
    }
};

// Прямоугольный маршрут в центре поля; это сценарий игрока, без доступа к State.
uint32_t patrol(int frame) {
    const int phase = frame % 32;
    if (phase < 10) return platform::kButtonRight;
    if (phase < 16) return platform::kButtonDown;
    if (phase < 26) return platform::kButtonLeft;
    return platform::kButtonUp;
}

}  // namespace

TEST_CASE("snake: descriptor fits SDK and supports records") {
    const auto& desc = minigame::snake_desc();
    CHECK(std::strcmp(desc.id, "snake") == 0);
    CHECK(desc.kinds == minigame::kPlayable);
    CHECK(desc.state_size > 0);
    CHECK(desc.state_size <= minigame::kStateBudget);
    CHECK(desc.width <= minigame::kMaxCanvasWidth);
    CHECK(desc.height <= minigame::kMaxCanvasHeight);
    REQUIRE(desc.start);
    REQUIRE(desc.update);
    REQUIRE(desc.draw);
    REQUIRE(desc.result);
}

TEST_CASE("snake: equal seed and input give equal scores and completion time") {
    for (uint32_t seed : {0u, 1u, 42u, UINT32_MAX}) {
        Attempt first(seed);
        Attempt second(seed);
        for (int frame = 0; frame < 1300 && first.status == minigame::Status::kRunning; ++frame) {
            first.tick(patrol(frame));
            second.tick(patrol(frame));
            CHECK(first.status == second.status);
            CHECK(first.result().score == second.result().score);
        }
        CHECK(first.status == minigame::Status::kFinished);
        CHECK(first.result().outcome == second.result().outcome);
        CHECK(first.result().quality == second.result().quality);
    }
}

TEST_CASE("snake: walls finish a scored attempt and difficulty changes speed") {
    int slow_frames = 0;
    for (float difficulty : {0.0f, 1.0f}) {
        Attempt attempt(42, difficulty);
        while (attempt.status == minigame::Status::kRunning && attempt.frames < 60) attempt.tick();
        REQUIRE(attempt.status == minigame::Status::kFinished);
        const auto result = attempt.result();
        CHECK(result.outcome == minigame::Outcome::kCompleted);
        CHECK(result.quality == 0.0f);
        CHECK(result.score >= 0);
        CHECK(result.score % 10 == 0);
        if (difficulty == 0.0f) slow_frames = attempt.frames;
        else CHECK(attempt.frames < slow_frames);
        attempt.tick();
        CHECK(attempt.status == minigame::Status::kFinished);
        CHECK(attempt.result().score == result.score);
    }
}

TEST_CASE("snake: reverse is ignored and best and variant do not change play") {
    Attempt straight(10);
    Attempt reverse(10, 1.0f, 255, INT32_MAX);
    while (straight.status == minigame::Status::kRunning && straight.frames < 60) {
        straight.tick();
        reverse.tick(platform::kButtonLeft);
        CHECK(straight.status == reverse.status);
        CHECK(straight.result().score == reverse.result().score);
    }
    CHECK(straight.status == minigame::Status::kFinished);
}

TEST_CASE("snake: at most one turn is buffered before a movement step") {
    Attempt first(5, 0.0f);
    Attempt second(5, 0.0f);
    first.tick(platform::kButtonUp, 0.05f);
    second.tick(platform::kButtonUp, 0.05f);
    first.tick(platform::kButtonLeft, 0.05f);
    second.tick(0, 0.05f);
    for (int frame = 0; frame < 60 && first.status == minigame::Status::kRunning; ++frame) {
        first.tick();
        second.tick();
        CHECK(first.status == second.status);
        CHECK(first.result().score == second.result().score);
    }
    CHECK(first.status == minigame::Status::kFinished);
}

TEST_CASE("snake: a surviving route ends by its timer with bounded score") {
    // Из начальной точки вверх, затем замкнутый прямоугольник вдали от стен.
    Attempt attempt(1);
    for (int i = 0; i < 4; ++i) attempt.tick(platform::kButtonUp);
    for (int frame = 0; frame < 1300 && attempt.status == minigame::Status::kRunning; ++frame)
        attempt.tick(patrol(frame));
    CHECK(attempt.status == minigame::Status::kFinished);
    CHECK(attempt.frames * kDt >= 120.0f);
    CHECK(attempt.frames * kDt <= 121.1f);
    const auto result = attempt.result();
    CHECK(result.outcome == minigame::Outcome::kCompleted);
    CHECK(result.score >= 0);
    CHECK(result.score <= 2360);
    CHECK(result.quality == 0.0f);
}

TEST_CASE("snake: seed changes food while all scores remain bounded") {
    bool different_scores = false;
    int32_t previous_score = -1;
    for (uint32_t seed = 1; seed <= 32; ++seed) {
        Attempt attempt(seed);
        while (attempt.status == minigame::Status::kRunning && attempt.frames < 60) attempt.tick();
        REQUIRE(attempt.status == minigame::Status::kFinished);
        const auto result = attempt.result();
        CHECK(result.score >= 0);
        CHECK(result.score <= 2360);
        CHECK(result.score % 10 == 0);
        if (previous_score >= 0 && result.score != previous_score) different_scores = true;
        previous_score = result.score;
    }
    CHECK(different_scores);
}

TEST_CASE("snake: food grows the body and a self collision keeps the earned score") {
    Attempt attempt(4);
    while (attempt.result().score == 0 && attempt.frames < 20) attempt.tick();
    REQUIRE(attempt.result().score == 10);
    attempt.tick(platform::kButtonUp);
    attempt.tick(platform::kButtonLeft);
    attempt.tick(platform::kButtonDown);
    for (int i = 0; i < 10 && attempt.status == minigame::Status::kRunning; ++i) attempt.tick();
    CHECK(attempt.status == minigame::Status::kFinished);
    CHECK(attempt.result().score == 10);
    CHECK(attempt.result().outcome == minigame::Outcome::kCompleted);
}

TEST_CASE("snake: an ungrown snake can enter the departing tail cell") {
    Attempt attempt(1);
    for (int loop = 0; loop < 5; ++loop) {
        for (uint32_t turn : {platform::kButtonUp, platform::kButtonLeft,
                              platform::kButtonDown, platform::kButtonRight}) {
            attempt.tick(turn);
            CHECK(attempt.status == minigame::Status::kRunning);
            CHECK(attempt.result().score == 0);
        }
    }
}
