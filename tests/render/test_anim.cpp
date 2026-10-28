#include <doctest/doctest.h>
#include "render/anim.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace {

using Bytes = std::vector<uint8_t>;

void u16(Bytes& b, size_t at, uint16_t v) {
    b[at] = static_cast<uint8_t>(v);
    b[at+1] = static_cast<uint8_t>(v >> 8);
}
void u32(Bytes& b, size_t at, uint32_t v) {
    const std::array<uint8_t, 4> encoded = {
        static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8),
        static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 24)};
    // at() проверяет границы каждой записи в тестовую фикстуру.
    for (size_t i = 0; i < encoded.size(); ++i) b.at(at+i) = encoded[i];
}
void f32(Bytes& b, size_t at, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    u32(b, at, bits);
}

// Независимая сборка ANM1: pelvis -> part_1, 3 клипа по 2 ключа.
Bytes fixture(uint16_t parts = 2, uint16_t frames = 2, uint16_t clip_count = 3) {
    const size_t clips = 32 + size_t(parts)*48;
    Bytes b(clips + size_t(clip_count)*48);
    std::memcpy(b.data(), "ANM1", 4);
    u16(b, 4, 1); u16(b, 8, parts); u16(b, 10, clip_count);
    u32(b, 12, 32); u32(b, 16, static_cast<uint32_t>(clips));
    u32(b, 20, static_cast<uint32_t>(b.size()));
    for (uint16_t p = 0; p < parts; ++p) {
        char name[32];
        if (p == 0) std::strcpy(name, "pelvis");
        else std::snprintf(name, sizeof(name), "part_%u", unsigned(p));
        std::memcpy(b.data() + 32 + p*48, name, std::strlen(name));
        u16(b, 64 + p*48, p == 0 ? 65535 : p-1);
        f32(b, 68 + p*48 + (p == 0 ? 4 : 0), 1.0f);
    }
    const char* names[] = {"walk", "sit_down", "type"};
    for (uint16_t c = 0; c < clip_count; ++c) {
        const size_t at = clips + c*48, keys = b.size();
        char name[32];
        if (c < 3) std::strcpy(name, names[c]);
        else std::snprintf(name, sizeof(name), "clip_%u", unsigned(c));
        std::memcpy(b.data() + at, name, std::strlen(name));
        u16(b, at+32, frames); u16(b, at+34, 15); u32(b, at+36, c == 1 ? 0 : 1);
        u32(b, at+40, static_cast<uint32_t>(keys));
        b.resize((keys + size_t(frames)*(12 + 8*parts) + 15) & ~size_t(15));
        for (uint16_t f = 0; f < frames; ++f) {
            const size_t k = keys + size_t(f)*(12 + 8*parts);
            const float progress = frames == 1 ? 0.0f : float(f)/float(frames-1);
            f32(b, k, c == 0 ? 2.0f*progress : (c == 1 ? 4.0f : -2.0f));
            for (uint16_t p = 0; p < parts; ++p) {
                const bool rotated = frames > 1 && c == 0 && p == 0 && f == frames-1;
                u16(b, k+12+p*8+(rotated ? 4 : 6), 32767);
            }
        }
    }
    u32(b, 24, static_cast<uint32_t>(b.size()));
    return b;
}

struct Player {
    alignas(16) std::array<uint8_t, 16384> memory{};
    render::AnimArena arena{memory.data(), memory.size(), 0};
    render::AnimSet set{};
    render::AnimState state{};
    core::Mat4 matrices[11]{};

    explicit Player(uint16_t parts = 2, uint16_t frames = 2) {
        const Bytes b = fixture(parts, frames);
        REQUIRE(render::anim_load(arena, b.data(), b.size(), &set));
        REQUIRE(render::anim_start(state, set, 0));
    }
    void pose() { REQUIRE(render::anim_pose(state, core::mat4_identity(), matrices, 11)); }
};

size_t g_anim_draws = 0;
render::ModelId g_anim_last_model = render::kInvalidModel;
core::Mat4 g_anim_last_matrix{};

}  // namespace

// pcclub_tests не линкует GPU-бэкенд; проверяем передачу матриц в существующий API.
namespace render {
void draw_model(ModelId model, const core::Mat4& transform) {
    ++g_anim_draws;
    g_anim_last_model = model;
    g_anim_last_matrix = transform;
}
}  // namespace render

TEST_CASE("ANM1 rejects corrupt data before consuming arena") {
    Player p;
    const Bytes good = fixture();
    const size_t used = p.arena.used;
    auto reject = [&](const Bytes& b) {
        render::AnimSet out = p.set;
        const char* error = nullptr;
        CHECK_FALSE(render::anim_load(p.arena, b.data(), b.size(), &out, &error));
        CHECK(out.view.data == nullptr);
        CHECK(error != nullptr);
        CHECK(p.arena.used == used);
    };
    for (size_t length = 0; length < good.size(); ++length)
        reject(Bytes(good.begin(), good.begin() + length));
    for (size_t offset : {size_t(0), size_t(4), size_t(6), size_t(12), size_t(16), size_t(20),
                           size_t(24), size_t(28), size_t(32), size_t(63), size_t(66),
                           size_t(160), size_t(162), size_t(164), size_t(168), size_t(172), size_t(328)}) {
        Bytes b = good; b[offset] ^= 0x80; reject(b);
    }
    { Bytes b = good; u16(b, 8, 0); reject(b); }
    { Bytes b = good; u16(b, 10, 17); reject(b); }
    { Bytes b = good; u16(b, 64, 0); reject(b); }
    { Bytes b = good; u16(b, 112, 1); reject(b); }
    { Bytes b = good; std::memcpy(b.data()+80, b.data()+32, 32); reject(b); }
    { Bytes b = good; std::memcpy(b.data()+176, b.data()+128, 32); reject(b); }
    { Bytes b = good; f32(b, 68, std::numeric_limits<float>::infinity()); reject(b); }
    { Bytes b = good; f32(b, 272, 11.0f); reject(b); }
    { Bytes b = good; u16(b, 290, 0); reject(b); }
    { Bytes b = good; u16(b, 284, 32768); reject(b); }
    { Bytes b = good; b.push_back(0); u32(b, 24, static_cast<uint32_t>(b.size())); reject(b); }
    render::format::AnimView view{};
    CHECK_FALSE(render::format::validate_anim(nullptr, 32, &view, nullptr));
}

TEST_CASE("ANM1 accepts 16 clips and rejects 17 without consuming arena") {
    const Bytes valid = fixture(11, 30, 16);
    render::AnimSet set{};
    render::format::AnimView view{};
    REQUIRE(render::format::validate_anim(valid.data(), valid.size(), &view, nullptr));
    CHECK(view.clip_count == 16);
    CHECK(valid.size() == 49456);
    alignas(16) std::array<uint8_t, 64 * 1024> memory{};
    render::AnimArena arena{memory.data(), memory.size(), 0};
    REQUIRE(render::anim_load(arena, valid.data(), valid.size(), &set));
    CHECK(render::anim_find_clip(set, "clip_15") == 15);
    render::AnimState state{};
    REQUIRE(render::anim_start(state, set, 15));
    render::anim_update(state, 0.1f);
    core::Mat4 pose[11];
    CHECK(render::anim_pose(state, core::mat4_identity(), pose, 11));

    const Bytes invalid = fixture(11, 30, 17);
    CHECK(invalid.size() < 64 * 1024);
    const size_t used = arena.used;
    const char* error = nullptr;
    CHECK_FALSE(render::anim_load(arena, invalid.data(), invalid.size(), &set, &error));
    CHECK(set.view.data == nullptr);
    CHECK(arena.used == used);
    REQUIRE(error != nullptr);
    CHECK(std::strcmp(error, "ANM1 clip_count must be 1..16") == 0);
}

TEST_CASE("ANM1 arena failures roll back and loaded data owns a copy") {
    Player p;
    Bytes b = fixture();
    render::AnimSet out{};
    auto arena = p.arena;
    arena.size = arena.used + b.size() - 1;
    CHECK_FALSE(render::anim_load(arena, b.data(), b.size(), &out));
    CHECK(arena.used == p.arena.used);
    arena = {p.memory.data()+1, 1000, 0};
    CHECK_FALSE(render::anim_load(arena, b.data(), b.size(), &out));
    CHECK(arena.used == 0);
    REQUIRE(render::anim_load(p.arena, b.data(), b.size(), &out));
    CHECK((reinterpret_cast<uintptr_t>(out.view.data) & 15) == 0);
    std::fill(b.begin(), b.end(), 0);
    CHECK(render::anim_find_clip(out, "walk") == 0);
    CHECK(render::anim_find_clip(out, "absent") == -1);
    CHECK(render::anim_find_clip(out, nullptr) == -1);
    CHECK(render::anim_find_clip({}, "walk") == -1);
}

TEST_CASE("animation key zero matches keyed rest pose and hierarchy follows parent") {
    Player p;
    p.pose();
    CHECK(p.matrices[0].m[13] == 1.0f);
    CHECK(p.matrices[1].m[12] == 1.0f);
    CHECK(p.matrices[1].m[13] == 1.0f);
    render::anim_update(p.state, 1.0f/30.0f);
    p.pose();
    CHECK(p.matrices[0].m[12] == doctest::Approx(1.0f));
    CHECK(p.matrices[0].m[0] == doctest::Approx(0.0f).epsilon(0.00001));
    CHECK(p.matrices[0].m[1] == doctest::Approx(1.0f));
    CHECK(p.matrices[1].m[12] == doctest::Approx(1.0f));
    CHECK(p.matrices[1].m[13] == doctest::Approx(2.0f));
    REQUIRE(render::anim_pose(p.state, core::mat4_translate({3,4,5}), p.matrices, 11));
    CHECK(p.matrices[1].m[12] == doctest::Approx(4.0f));
    CHECK(p.matrices[1].m[13] == doctest::Approx(6.0f));
    CHECK(p.matrices[1].m[14] == doctest::Approx(5.0f));
}

TEST_CASE("animation loop wraps including last-to-first interval and one-shot holds last") {
    Player p;
    render::anim_update(p.state, 0.1f);
    p.pose();
    CHECK(p.state.time == doctest::Approx(0.1f));
    CHECK(p.matrices[0].m[12] == doctest::Approx(1.0f));
    render::anim_update(p.state, 1.0f/30.0f);
    CHECK(p.state.time == doctest::Approx(0.0f).epsilon(0.00001));
    render::anim_update(p.state, 1000.0f);
    CHECK(p.state.time >= 0.0f);
    CHECK(p.state.time < 2.0f/15.0f);
    REQUIRE(render::anim_start(p.state, p.set, 1));
    render::anim_update(p.state, 10.0f);
    CHECK(p.state.time == doctest::Approx(1.0f/15.0f));
    p.pose();
    CHECK(p.matrices[0].m[12] == 4.0f);
    render::anim_update(p.state, 10.0f);
    CHECK(p.state.time == doctest::Approx(1.0f/15.0f));
}

TEST_CASE("animation cross-fade is continuous including interrupted transitions") {
    Player p;
    render::anim_update(p.state, 1.0f/30.0f);
    p.pose();
    core::Mat4 before[2] = {p.matrices[0], p.matrices[1]};
    REQUIRE(render::anim_play(p.state, 1));
    p.pose();
    for (int i = 0; i < 16; ++i) CHECK(p.matrices[1].m[i] == doctest::Approx(before[1].m[i]));
    render::anim_update(p.state, 0.1f);
    p.pose();
    // Предыдущий loop за 0.1 с вернулся в начало; смесь root 0 и 4 = 2.
    CHECK(p.matrices[0].m[12] == doctest::Approx(2.0f).epsilon(0.00001));
    before[1] = p.matrices[1];
    REQUIRE(render::anim_play(p.state, 2));
    p.pose();
    for (int i = 0; i < 16; ++i) CHECK(p.matrices[1].m[i] == doctest::Approx(before[1].m[i]));
    CHECK(p.state.frozen_source);
    render::anim_update(p.state, 0.2f);
    p.pose();
    CHECK(p.state.previous_clip == -1);
    CHECK(p.matrices[0].m[12] == -2.0f);
    REQUIRE(render::anim_play(p.state, 1, 0.0f));
    p.pose();
    CHECK(p.matrices[0].m[12] == 4.0f);
}

TEST_CASE("animation single-key clips and huge finite dt stay finite") {
    Player p(1, 1);
    render::anim_update(p.state, std::numeric_limits<float>::max());
    p.pose();
    CHECK(std::isfinite(p.state.time));
    CHECK(p.state.time >= 0.0f);
    CHECK(p.state.time < 1.0f/15.0f);
    CHECK(p.matrices[0].m[0] == 1.0f);
    REQUIRE(render::anim_start(p.state, p.set, 1));
    render::anim_update(p.state, std::numeric_limits<float>::max());
    p.pose();
    CHECK(p.state.time == 0.0f);
    CHECK(p.matrices[0].m[12] == 4.0f);
}

TEST_CASE("animation rejects invalid controls and undersized output without changes") {
    Player p;
    p.pose();
    const core::Mat4 before = p.matrices[0];
    CHECK_FALSE(render::anim_pose(p.state, core::mat4_identity(), p.matrices, 1));
    CHECK(std::memcmp(&before, p.matrices, sizeof(before)) == 0);
    CHECK_FALSE(render::anim_pose({}, core::mat4_identity(), p.matrices, 11));
    CHECK_FALSE(render::anim_play(p.state, 10));
    CHECK_FALSE(render::anim_play(p.state, 1, -1.0f));
    CHECK_FALSE(render::anim_start(p.state, p.set, -1));
    render::anim_update(p.state, -1.0f);
    render::anim_update(p.state, std::numeric_limits<float>::infinity());
    render::anim_update(p.state, std::numeric_limits<float>::quiet_NaN());
    CHECK(p.state.time == 0.0f);
    REQUIRE(render::anim_play(p.state, 0));
    CHECK(p.state.previous_clip == -1);
}

TEST_CASE("animation antipodal quaternion keys use shortest nlerp path") {
    Player p;
    Bytes b = fixture();
    // Ключ 1: -identity вместо +180 вокруг Z.
    u16(b, 316, 0); u16(b, 318, uint16_t(65536-32767));
    render::AnimSet set{};
    REQUIRE(render::anim_load(p.arena, b.data(), b.size(), &set));
    REQUIRE(render::anim_start(p.state, set, 0));
    render::anim_update(p.state, 1.0f/30.0f);
    p.pose();
    CHECK(p.matrices[0].m[0] == 1.0f);
    CHECK(p.matrices[0].m[5] == 1.0f);
}

TEST_CASE("animation draws each rigid part through draw_model") {
    Player p;
    p.pose();
    const render::ModelId models[] = {7, 12};
    g_anim_draws = 0;
    render::anim_draw(p.set, models, p.matrices, 1);
    CHECK(g_anim_draws == 0);
    render::anim_draw(p.set, models, p.matrices, 2);
    CHECK(g_anim_draws == 2);
    CHECK(g_anim_last_model == 12);
    CHECK(std::memcmp(&g_anim_last_matrix, p.matrices+1, sizeof(core::Mat4)) == 0);
}

TEST_CASE("animation pose benchmark 20 characters x 11 parts") {
    Player p(11, 30);
    std::array<render::AnimState, 20> states;
    for (auto& s : states) REQUIRE(render::anim_start(s, p.set, 0));
    volatile float checksum = 0.0f;
    for (bool fade : {false, true}) {
        for (auto& s : states) {
            REQUIRE(render::anim_start(s, p.set, 0));
            if (fade) REQUIRE(render::anim_play(s, 1));
            render::anim_update(s, 0.05f);
        }
        constexpr int kIterations = 2000;
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kIterations; ++i) {
            for (const auto& s : states) {
                const bool ok = render::anim_pose(s, core::mat4_identity(), p.matrices, 11);
                if (!ok) FAIL("benchmark pose failed");
                checksum += p.matrices[10].m[12];
            }
        }
        const auto elapsed = std::chrono::steady_clock::now() - start;
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
        std::printf("ANM1 pose 20x11 %s: %.3f us/frame (%d iterations)\n",
                    fade ? "cross-fade" : "steady", float(ns) / (1000.0f*kIterations), kIterations);
    }
    CHECK(std::isfinite(checksum));
}
