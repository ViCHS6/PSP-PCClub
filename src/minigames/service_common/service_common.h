#pragma once

#include "core/rng_api.h"
#include "minigames/sdk.h"

namespace minigame::service {

constexpr int kWidth = 440;
constexpr int kHeight = 236;
constexpr float kMinimumTime = 10.0f;

struct Attempt {
    float time = 0.0f;
    float quality = 0.0f;
    bool finished = false;
    bool completed = false;
};

inline float clamp01(float value) {
    if (!(value >= 0.0f)) return 0.0f;
    return value > 1.0f ? 1.0f : value;
}

inline bool tick(Attempt& a, float dt, float limit) {
    if (a.finished) return true;
    a.time += dt;
    if (a.time >= limit) {
        a.time = limit;
        a.finished = true;
        a.quality = 0.0f;
    }
    return a.finished;
}

// Общая шкала §6 minigame.md: быстро и без ошибок — 1, у таймера — 0,2.
inline float quality(float time, float normal, float limit, float errors) {
    const float late = clamp01((time - normal) / (limit - normal));
    return (1.0f - 0.8f * late) / (1.0f + 0.2f * errors);
}

inline Status finish(Attempt& a, float value) {
    if (a.time < kMinimumTime) return Status::kRunning;
    a.finished = true;
    a.quality = clamp01(value);
    a.completed = a.quality > 0.0f;
    return Status::kFinished;
}

inline void result(const Attempt& a, Result* out) {
    out->outcome = a.completed ? Outcome::kCompleted : Outcome::kFailed;
    out->score = 0;
    out->quality = a.completed ? a.quality : 0.0f;
    // duration_ms принадлежит хосту.
}

inline void move_menu(int& selected, int count, const Input& in) {
    if (in.pressed & platform::kButtonUp) selected = (selected + count - 1) % count;
    else if (in.pressed & platform::kButtonDown) selected = (selected + 1) % count;
}

inline void shuffle(core::Rng& rng, int* items, int count) {
    for (int i = count - 1; i > 0; --i) {
        const int j = core::rng_range(rng, 0, i);
        const int tmp = items[i];
        items[i] = items[j];
        items[j] = tmp;
    }
}

inline void bar(const Canvas& c, int y, float value, render::Color color) {
    fill(c, 12, y, kWidth - 24, 8, ui::kColShadow);
    fill(c, 12, y, static_cast<int>((kWidth - 24) * clamp01(value)), 8, color);
}

inline void panel(const Canvas& c, const char* hint, const char* time_label,
                  const Attempt& a, float limit) {
    fill(c, 0, 0, c.w, c.h, ui::kColField);
    text(c, 12, 8, hint, ui::kColText);
    text(c, 12, 204, time_label, ui::kColTextDim);
    bar(c, 224, 1.0f - a.time / limit, ui::kColWarn);
}

inline void menu_item(const Canvas& c, int row, const char* label, bool selected) {
    const int y = 68 + row * 22;
    if (selected) fill(c, 12, y - 2, kWidth - 24, 20, ui::kColAccent);
    text(c, 20, y, label, selected ? ui::kColTextInv : ui::kColText);
}

}  // namespace minigame::service
