#pragma once
// КОНТРАКТ (T020). Детерминированный ГПСЧ ядра (GDD §7, §10.2): xorshift32 с сидом из сохранения.
// Единственный источник случайности в src/core — никаких rand() и системного времени.
// Реализация здесь же: алгоритм — часть контракта, от него зависят прогоны balance-runner
// и воспроизводимость сохранений. Менять — только задачей «меняет контракт».
//
// Состояние — 4 байта, копируется и сохраняется как есть. Один Rng на мир (World::rng);
// тестам и инструментам можно заводить свои.
//
//     core::Rng rng;
//     core::rng_seed(rng, 42);
//     if (core::rng_chance(rng, 0.3f)) { ... }
//     const int32_t money = core::rng_range(rng, 30, 120);
#include <cstdint>

namespace core {

struct Rng {
    uint32_t state = 0x9E3779B9u;
};

// Сид 0 недопустим для xorshift — заменяется константой.
inline void rng_seed(Rng& rng, uint32_t seed) { rng.state = seed != 0 ? seed : 0x9E3779B9u; }

// xorshift32 (Marsaglia, 13/17/5).
inline uint32_t rng_next(Rng& rng) {
    uint32_t x = rng.state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng.state = x;
    return x;
}

// Равномерно в [0, 1): 24 старших бита — ровно столько точно представимо во float.
inline float rng_float01(Rng& rng) {
    return static_cast<float>(rng_next(rng) >> 8) * (1.0f / 16777216.0f);
}

// Равномерно в [lo, hi], оба конца включительно. lo > hi → lo. Смещение остатка от
// деления пренебрежимо для диапазонов игры (< 2^16).
inline int32_t rng_range(Rng& rng, int32_t lo, int32_t hi) {
    if (hi <= lo) return lo;
    const uint32_t span = static_cast<uint32_t>(hi - lo) + 1u;
    return lo + static_cast<int32_t>(rng_next(rng) % span);
}

// Равномерно в [lo, hi).
inline float rng_range_f(Rng& rng, float lo, float hi) { return lo + (hi - lo) * rng_float01(rng); }

// true с вероятностью p; p ≤ 0 → всегда false, p ≥ 1 → всегда true. Один вызов rng_next.
inline bool rng_chance(Rng& rng, float p) { return rng_float01(rng) < p; }

// Приближённо N(0, sigma): сумма 12 равномерных − 6 (Ирвин — Холл), без log/cos —
// только сложения, поэтому одинаково на PSP и PC. Нужен для §7.11. 12 вызовов rng_next.
inline float rng_normal(Rng& rng, float sigma) {
    float s = 0.0f;
    for (int i = 0; i < 12; ++i) s += rng_float01(rng);
    return (s - 6.0f) * sigma;
}

}  // namespace core
