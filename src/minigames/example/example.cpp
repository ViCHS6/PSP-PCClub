// «Пример» (T050): образец мини-игры по SDK. Правила — example.h.
#include "minigames/example/example.h"

#include <cstdio>

#include "core/rng_api.h"

namespace minigame {

namespace {

constexpr int kWidth = 240;
constexpr int kHeight = 120;
constexpr int kBarX = 10;
constexpr int kBarY = 52;
constexpr int kBarW = kWidth - 2 * kBarX;
constexpr int kBarH = 16;
constexpr float kFeedbackTime = 0.4f;  // пауза после нажатия: видно, попал или нет

// Всё состояние попытки — в арене хоста.
struct State {
    core::Rng rng;
    float pos;        // ползунок 0…1
    float dir;        // +1 / −1
    float speed;      // шкал в секунду
    float zone_c;     // центр зелёной зоны 0…1
    float zone_w;     // ширина зоны 0…1
    float time;       // с начала попытки
    float feedback;   // > 0 — показываем итог нажатия
    int round;
    int hits;
    uint8_t kind;
    uint8_t last_hit;
    uint8_t has_best;
    int32_t best;
};
static_assert(sizeof(State) <= kStateBudget, "State");

const char* g_hint = "";
const char* g_hits_fmt = "";
const char* g_best_fmt = "";

void new_zone(State& s) {
    const float half = s.zone_w * 0.5f;
    s.zone_c = half + core::rng_float01(s.rng) * (1.0f - s.zone_w);
    if (s.zone_c < half) s.zone_c = half;
}

bool load() {
    g_hint = str("mg.example.hint");
    g_hits_fmt = str("mg.example.hits");
    g_best_fmt = str("mg.example.best");
    return true;
}

void start(void* p, const StartParams& params) {
    State& s = *static_cast<State*>(p);
    float d = params.difficulty;
    if (d < 0.0f) d = 0.0f;
    if (d > 1.0f) d = 1.0f;
    core::rng_seed(s.rng, params.seed);
    s.dir = 1.0f;
    s.speed = 0.8f + 0.8f * d;
    s.zone_w = 0.25f - 0.15f * d;
    s.kind = params.kind;
    s.has_best = params.has_best;
    s.best = params.best_score;
    new_zone(s);
}

Status update(void* p, float dt, const Input& in) {
    State& s = *static_cast<State*>(p);
    s.time += dt;
    if (s.feedback > 0.0f) {
        s.feedback -= dt;
        if (s.feedback <= 0.0f) {
            if (s.round >= kExampleRounds) return Status::kFinished;
            new_zone(s);
        }
    } else {
        s.pos += s.dir * s.speed * dt;
        if (s.pos > 1.0f) { s.pos = 2.0f - s.pos; s.dir = -1.0f; }
        if (s.pos < 0.0f) { s.pos = -s.pos; s.dir = 1.0f; }
        if (in.pressed & platform::kButtonCross) {
            const float off = s.pos - s.zone_c;
            s.last_hit = (off < 0.0f ? -off : off) <= s.zone_w * 0.5f;
            s.hits += s.last_hit;
            ++s.round;
            s.feedback = kFeedbackTime;
        }
    }
    return s.time >= kExampleTimeLimit ? Status::kFinished : Status::kRunning;
}

void draw(const void* p, const Canvas& c) {
    const State& s = *static_cast<const State*>(p);
    fill(c, 0, 0, c.w, c.h, ui::kColField);
    text(c, kBarX, 8, g_hint, ui::kColText);

    fill(c, kBarX, kBarY, kBarW, kBarH, ui::kColShadow);
    const int zx = kBarX + static_cast<int>((s.zone_c - s.zone_w * 0.5f) * kBarW);
    fill(c, zx, kBarY, static_cast<int>(s.zone_w * kBarW), kBarH, ui::kColGood);
    const render::Color slider =
        s.feedback > 0.0f ? (s.last_hit ? ui::kColText : ui::kColBad) : ui::kColAccent;
    fill(c, kBarX + static_cast<int>(s.pos * (kBarW - 4)), kBarY - 3, 4, kBarH + 6, slider);

    char buf[64];
    std::snprintf(buf, sizeof(buf), g_hits_fmt, s.hits, kExampleRounds);
    text(c, kBarX, 78, buf, ui::kColText);
    if (s.has_best) {
        std::snprintf(buf, sizeof(buf), g_best_fmt, static_cast<int>(s.best));
        text(c, kBarX + 120, 78, buf, ui::kColTextDim);
    }
    // Оставшееся время — полоска внизу.
    const float left = 1.0f - s.time / kExampleTimeLimit;
    fill(c, kBarX, 104, static_cast<int>(kBarW * (left > 0.0f ? left : 0.0f)), 6, ui::kColWarn);
}

void result(const void* p, Result* out) {
    const State& s = *static_cast<const State*>(p);
    out->score = 100 * s.hits;
    out->quality = static_cast<float>(s.hits) / kExampleRounds;
    out->outcome = (s.kind == kService && s.hits == 0) ? Outcome::kFailed : Outcome::kCompleted;
}

}  // namespace

const Desc& example_desc() {
    static Desc d = [] {
        Desc x;
        x.id = "example";
        x.title_key = "mg.example.name";
        x.kinds = kPlayable | kService;
        x.width = kWidth;
        x.height = kHeight;
        x.state_size = sizeof(State);
        x.load = load;
        x.start = start;
        x.update = update;
        x.draw = draw;
        x.result = result;
        return x;
    }();
    return d;
}

}  // namespace minigame
