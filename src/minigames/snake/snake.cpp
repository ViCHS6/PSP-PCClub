#include "minigames/snake/snake.h"

#include <cmath>
#include <cstdio>

#include "core/rng_api.h"

namespace minigame {
namespace {

constexpr int kColumns = 20;
constexpr int kRows = 12;
constexpr int kCells = kColumns * kRows;
constexpr int kCellSize = 16;
constexpr int kBoardX = 8;
constexpr int kBoardY = 28;
constexpr int kSideX = 344;
constexpr float kTimeLimit = 120.0f;
constexpr float kFeedbackTime = 0.8f;
constexpr int kFoodScore = 10;
constexpr int kDx[] = {1, 0, -1, 0};
constexpr int kDy[] = {0, 1, 0, -1};

enum End : uint8_t { kPlaying, kCollision, kTime, kFull };

struct Cell {
    int16_t x;
    int16_t y;
};

struct State {
    core::Rng rng;
    Cell body[kCells];
    Cell food;
    int length;
    int32_t score;
    int32_t best;
    float elapsed;
    float accumulator;
    float step;
    float feedback;
    uint8_t direction;
    uint8_t pending;
    uint8_t turn_queued;
    uint8_t has_best;
    End end;
};
static_assert(sizeof(State) <= kStateBudget, "Snake state exceeds 32 KiB");

const char* g_hint = "";
const char* g_score = "";
const char* g_best = "";
const char* g_time = "";
const char* g_snack = "";
const char* g_ends[4] = {};

bool same(Cell a, Cell b) { return a.x == b.x && a.y == b.y; }

bool occupied(const State& s, Cell cell, int count) {
    for (int i = 0; i < count; ++i)
        if (same(s.body[i], cell)) return true;
    return false;
}

void place_food(State& s) {
    // Выбираем свободную клетку за ограниченное число шагов даже на полном поле.
    int free_index = core::rng_range(s.rng, 0, kCells - s.length - 1);
    for (int y = 0; y < kRows; ++y) {
        for (int x = 0; x < kColumns; ++x) {
            const Cell cell{static_cast<int16_t>(x), static_cast<int16_t>(y)};
            if (!occupied(s, cell, s.length) && free_index-- == 0) {
                s.food = cell;
                return;
            }
        }
    }
}

bool load() {
    g_hint = str("mg.snake.hint");
    g_score = str("mg.snake.score");
    g_best = str("mg.snake.best");
    g_time = str("mg.snake.time");
    g_snack = str("mg.snake.snack");
    g_ends[kPlaying] = str("mg.snake.greeting");
    g_ends[kCollision] = str("mg.snake.collision");
    g_ends[kTime] = str("mg.snake.timeout");
    g_ends[kFull] = str("mg.snake.full");
    return true;
}

void start(void* p, const StartParams& params) {
    State& s = *static_cast<State*>(p);
    core::rng_seed(s.rng, params.seed);
    float difficulty = params.difficulty;
    if (!std::isfinite(difficulty)) difficulty = 0.5f;
    if (difficulty < 0.0f) difficulty = 0.0f;
    if (difficulty > 1.0f) difficulty = 1.0f;
    s.step = 0.2f - 0.1f * difficulty;
    s.length = 4;
    for (int i = 0; i < s.length; ++i)
        s.body[i] = {static_cast<int16_t>(4 - i), 6};
    s.best = params.best_score;
    s.has_best = params.has_best;
    place_food(s);
}

void queue_turn(State& s, const Input& in) {
    if (s.turn_queued) return;
    int direction = s.direction;
    const uint32_t buttons = in.pressed ? in.pressed : in.held;
    if (buttons & platform::kButtonUp) direction = 3;
    else if (buttons & platform::kButtonDown) direction = 1;
    else if (buttons & platform::kButtonLeft) direction = 2;
    else if (buttons & platform::kButtonRight) direction = 0;
    else if (std::fabs(in.stick_x) > std::fabs(in.stick_y)) {
        if (in.stick_x > 0.5f) direction = 0;
        else if (in.stick_x < -0.5f) direction = 2;
    } else {
        if (in.stick_y > 0.5f) direction = 3;
        else if (in.stick_y < -0.5f) direction = 1;
    }
    if ((direction & 1) != (s.direction & 1)) {
        s.pending = static_cast<uint8_t>(direction);
        s.turn_queued = 1;
    }
}

void move(State& s) {
    if (s.turn_queued) s.direction = s.pending;
    s.turn_queued = 0;
    const Cell head{static_cast<int16_t>(s.body[0].x + kDx[s.direction]),
                    static_cast<int16_t>(s.body[0].y + kDy[s.direction])};
    const bool eats = same(head, s.food);
    // Уходящий хвост можно занять, если в этом шаге змейка не растёт.
    if (head.x < 0 || head.x >= kColumns || head.y < 0 || head.y >= kRows ||
        occupied(s, head, s.length - (eats ? 0 : 1))) {
        s.end = kCollision;
        return;
    }
    if (eats) {
        ++s.length;
        s.score += kFoodScore;
    }
    for (int i = s.length - 1; i > 0; --i) s.body[i] = s.body[i - 1];
    s.body[0] = head;
    if (s.length == kCells) s.end = kFull;
    else if (eats) place_food(s);
}

Status update(void* p, float dt, const Input& in) {
    State& s = *static_cast<State*>(p);
    if (s.end != kPlaying) {
        s.feedback += dt;
        return s.feedback >= kFeedbackTime ? Status::kFinished : Status::kRunning;
    }
    queue_turn(s, in);
    s.elapsed += dt;
    if (s.elapsed >= kTimeLimit) {
        s.elapsed = kTimeLimit;
        s.end = kTime;
        return Status::kRunning;
    }
    s.accumulator += dt;
    while (s.accumulator >= s.step && s.end == kPlaying) {
        s.accumulator -= s.step;
        move(s);
    }
    return Status::kRunning;
}

void draw(const void* p, const Canvas& c) {
    const State& s = *static_cast<const State*>(p);
    fill(c, 0, 0, c.w, c.h, ui::kColField);
    fill(c, kBoardX - 2, kBoardY - 2, kColumns * kCellSize + 4,
         kRows * kCellSize + 4, ui::kColShadow);
    fill(c, kBoardX, kBoardY, kColumns * kCellSize, kRows * kCellSize,
         render::rgba(32, 40, 32));
    for (int i = s.length - 1; i >= 0; --i) {
        const render::Color color = i == 0 ? ui::kColLight : ui::kColGood;
        fill(c, kBoardX + s.body[i].x * kCellSize + 1,
             kBoardY + s.body[i].y * kCellSize + 1, kCellSize - 2, kCellSize - 2, color);
    }
    if (s.end != kFull)
        fill(c, kBoardX + s.food.x * kCellSize + 4, kBoardY + s.food.y * kCellSize + 4,
             kCellSize - 8, kCellSize - 8, ui::kColWarn);
    text(c, kBoardX, 6, g_hint, ui::kColTextDim);
    char buf[64];
    std::snprintf(buf, sizeof(buf), g_score, static_cast<int>(s.score));
    text(c, kSideX, 30, buf, ui::kColText);
    if (s.has_best) {
        std::snprintf(buf, sizeof(buf), g_best, static_cast<int>(s.best));
        text(c, kSideX, 52, buf, ui::kColTextDim);
    }
    std::snprintf(buf, sizeof(buf), g_time, static_cast<int>(std::ceil(kTimeLimit - s.elapsed)));
    text(c, kSideX, 80, buf, ui::kColText);
    text(c, kSideX, 110, g_snack, ui::kColWarn);
    text(c, kBoardX, 222, g_ends[s.end], s.end == kCollision ? ui::kColBad : ui::kColTextDim);
    fill(c, kSideX, 98, static_cast<int>(96.0f * (1.0f - s.elapsed / kTimeLimit)), 4, ui::kColWarn);
}

void result(const void* p, Result* out) {
    const State& s = *static_cast<const State*>(p);
    out->score = s.score;
    out->quality = 0.0f;
    out->outcome = Outcome::kCompleted;
}

}  // namespace

const Desc& snake_desc() {
    static const Desc desc = [] {
        Desc d;
        d.id = "snake";
        d.title_key = "mg.snake.name";
        d.kinds = kPlayable;
        d.width = 456;
        d.height = 236;
        d.state_size = sizeof(State);
        d.load = load;
        d.start = start;
        d.update = update;
        d.draw = draw;
        d.result = result;
        return d;
    }();
    return desc;
}

}  // namespace minigame
