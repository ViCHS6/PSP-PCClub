#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

#include "render/screen_tex.h"

using render::ScreenId;
using render::ScreenState;

static_assert(std::is_trivial<ScreenState>::value && std::is_standard_layout<ScreenState>::value,
              "ScreenState must remain POD");

namespace {

constexpr std::array<ScreenId, 13> kGenres = {
    ScreenId::kShooter, ScreenId::kShooter, ScreenId::kShooter, ScreenId::kRts,
    ScreenId::kTbs, ScreenId::kRpg, ScreenId::kRpg, ScreenId::kSim, ScreenId::kAction,
    ScreenId::kRace, ScreenId::kRace, ScreenId::kCasual, ScreenId::kCasual
};
static_assert(core::kGenreCount == kGenres.size(), "Update genre coverage");

struct Draw {
    render::MeshId mesh;
    render::TextureId texture;
    uint16_t frame;
};

std::array<bool, 12> g_loaded{};
std::array<uint16_t, 12> g_frames{};
std::array<uint16_t, 12> g_counts{};
std::array<std::array<render::Vertex, 4>, 2> g_meshes{};
std::vector<Draw> g_draws;
unsigned g_loads = 0;
unsigned g_creates = 0;
bool g_missing = false;
bool g_mesh_failure = false;

constexpr const char* kNames[] = {
    "scr_shooter", "scr_rts", "scr_tbs", "scr_rpg", "scr_sim", "scr_race",
    "scr_action", "scr_casual", "scr_desktop", "scr_loading", "scr_bsod", "scr_off"
};

struct Fixture {
    core::GameRec games[2]{};
    core::GameData data;
    core::Pc pc{};
    ScreenState state{};

    Fixture() {
        games[0].genre = core::kGenreShooterTeam;
        games[1].genre = core::kGenreShooterArena;
        data.games = games;
        data.game_count = 2;
        pc.state = core::PcState::kIdle;
        pc.running_game = core::kNone;
        render::screen_state_init(state, 0, pc, data);
    }

    void start() {
        pc.state = core::PcState::kBusy;
        pc.running_game = 0;
        render::screen_select(state, pc, data);
    }
};

void reset_gpu(bool missing = false) {
    g_loaded.fill(false);
    g_frames.fill(0);
    g_counts.fill(4);
    g_counts[8] = g_counts[10] = g_counts[11] = 1;
    g_draws.clear();
    g_loads = g_creates = 0;
    g_missing = missing;
    g_mesh_failure = false;
}

}  // namespace

// Отдельная цель без окна: проверяем последовательность set_frame/draw и fallback.
namespace render {

TextureId texture_find(const char* name) {
    for (size_t i = 0; i < g_loaded.size(); ++i) {
        if (g_loaded[i] && std::strcmp(name, kNames[i]) == 0) return static_cast<TextureId>(i);
    }
    return kNoTexture;
}

TextureId texture_load_file(const char* path) {
    ++g_loads;
    if (g_missing) return kNoTexture;
    for (size_t i = 0; i < g_loaded.size(); ++i) {
        char expected[64];
        std::snprintf(expected, sizeof(expected), "assets/%s.tex", kNames[i]);
        if (std::strcmp(path, expected) == 0) {
            g_loaded[i] = true;
            return static_cast<TextureId>(i);
        }
    }
    FAIL("Unexpected screen asset path");
    return kNoTexture;
}

uint16_t texture_frame_count(TextureId texture) {
    return texture < g_counts.size() ? g_counts[texture] : 0;
}

void texture_set_frame(TextureId texture, uint16_t frame) {
    REQUIRE(texture < g_frames.size());
    REQUIRE(frame < g_counts[texture]);
    g_frames[texture] = frame;
}

MeshId mesh_create(const Vertex* vertices, uint16_t vertex_count,
                   const uint16_t* indices, uint16_t index_count) {
    if (g_mesh_failure) return kInvalidMesh;
    REQUIRE(vertex_count == 4);
    REQUIRE(index_count == 6);
    REQUIRE(g_creates < g_meshes.size());
    for (size_t i = 0; i < 6; ++i) REQUIRE(indices[i] < 4);
    std::memcpy(g_meshes[g_creates].data(), vertices, sizeof(Vertex) * 4);
    return static_cast<MeshId>(g_creates++);
}

void draw_mesh(MeshId mesh, const core::Mat4&, TextureId texture) {
    g_draws.push_back({mesh, texture, texture < g_frames.size() ? g_frames[texture] : uint16_t(0)});
}

}  // namespace render

TEST_CASE("screen genres cover all thirteen core genres") {
    for (size_t i = 0; i < kGenres.size(); ++i) {
        CAPTURE(i);
        CHECK(render::screen_for_genre(static_cast<core::Genre>(i)) == kGenres[i]);
    }
    CHECK(render::screen_for_genre(static_cast<core::Genre>(255)) == ScreenId::kDesktop);
}

TEST_CASE("screen selection reads every PC state and running game safely") {
    Fixture f;
    const core::PcState states[] = {core::PcState::kEmpty, core::PcState::kOff, core::PcState::kIdle,
        core::PcState::kBusy, core::PcState::kHung, core::PcState::kBroken};
    const ScreenId expected[] = {ScreenId::kOff, ScreenId::kOff, ScreenId::kDesktop,
        ScreenId::kShooter, ScreenId::kBsod, ScreenId::kOff};
    f.pc.running_game = 0;
    for (size_t i = 0; i < 6; ++i) {
        f.pc.state = states[i];
        CHECK(render::screen_for_pc(f.pc, f.data) == expected[i]);
    }
    f.pc.state = core::PcState::kBusy;
    for (size_t i = 0; i < kGenres.size(); ++i) {
        f.games[0].genre = static_cast<uint8_t>(i);
        CHECK(render::screen_for_pc(f.pc, f.data) == kGenres[i]);
    }
    f.pc.running_game = core::kNone;
    CHECK(render::screen_for_pc(f.pc, f.data) == ScreenId::kDesktop);
    f.pc.running_game = f.data.game_count;
    CHECK(render::screen_for_pc(f.pc, f.data) == ScreenId::kDesktop);
    f.pc.running_game = 0;
    f.data.games = nullptr;
    CHECK(render::screen_for_pc(f.pc, f.data) == ScreenId::kDesktop);
}

TEST_CASE("idle to busy loads for two seconds without restarting each selection") {
    Fixture f;
    CHECK(f.state.screen == ScreenId::kDesktop);
    f.start();
    const core::Pc saved_pc = f.pc;
    const core::GameRec saved_game = f.games[0];
    CHECK(f.state.screen == ScreenId::kLoading);
    CHECK(f.state.loading_remaining == 2.0f);
    render::screens_update(&f.state, 1, 1.5f);
    render::screen_select(f.state, f.pc, f.data);
    CHECK(f.state.loading_remaining == 0.5f);
    render::screens_update(&f.state, 1, 0.25f);
    CHECK(f.state.screen == ScreenId::kLoading);
    render::screens_update(&f.state, 1, 0.25f);
    CHECK(f.state.screen == ScreenId::kShooter);
    CHECK(f.state.loading_remaining == 0.0f);
    render::screen_select(f.state, f.pc, f.data);
    CHECK(f.state.screen == ScreenId::kShooter);
    CHECK(std::memcmp(&saved_pc, &f.pc, sizeof(f.pc)) == 0);
    CHECK(std::memcmp(&saved_game, &f.games[0], sizeof(saved_game)) == 0);
}

TEST_CASE("changing game restarts loading even within same genre screen") {
    Fixture f;
    f.start();
    render::screens_update(&f.state, 1, 0.5f);
    f.pc.running_game = 1;
    render::screen_select(f.state, f.pc, f.data);
    CHECK(f.state.screen == ScreenId::kLoading);
    CHECK(f.state.loading_remaining == 2.0f);
    render::screens_update(&f.state, 1, 3.0f);
    CHECK(f.state.screen == ScreenId::kShooter);
    f.games[0].genre = core::kGenreRts;
    f.pc.running_game = 0;
    render::screen_select(f.state, f.pc, f.data);
    CHECK(f.state.screen == ScreenId::kLoading);
    render::screens_update(&f.state, 1, 2.0f);
    CHECK(f.state.screen == ScreenId::kRts);
}

TEST_CASE("PC interruption cancels loading and initialization restores existing busy screen") {
    for (auto pc_state : {core::PcState::kHung, core::PcState::kOff, core::PcState::kBroken,
                          core::PcState::kEmpty, core::PcState::kIdle}) {
        Fixture f;
        f.start();
        REQUIRE(f.state.screen == ScreenId::kLoading);
        f.pc.state = pc_state;
        render::screen_select(f.state, f.pc, f.data);
        CHECK(f.state.loading_remaining == 0.0f);
        CHECK(f.state.screen == render::screen_for_pc(f.pc, f.data));
        render::screens_update(&f.state, 1, 4.0f);
        CHECK(f.state.screen == render::screen_for_pc(f.pc, f.data));
    }
    Fixture f;
    f.pc.state = core::PcState::kBusy;
    f.pc.running_game = 0;
    render::screen_state_init(f.state, 0, f.pc, f.data);
    CHECK(f.state.screen == ScreenId::kShooter);
    CHECK(f.state.loading_remaining == 0.0f);
    f.pc.running_game = core::kNone;
    render::screen_select(f.state, f.pc, f.data);
    CHECK(f.state.screen == ScreenId::kDesktop);
    CHECK(f.state.loading_remaining == 0.0f);
}

TEST_CASE("animation advances four frames per second and wraps for every supported strip length") {
    Fixture f;
    CHECK(render::screen_frame(f.state, 0) == 0);
    CHECK(render::screen_frame(f.state, 1) == 0);
    render::screens_update(&f.state, 1, 0.125f);
    CHECK(render::screen_frame(f.state, 4) == 0);
    render::screens_update(&f.state, 1, 0.125f);
    CHECK(render::screen_frame(f.state, 4) == 1);
    for (uint16_t count = 4; count <= 8; ++count) {
        f.state.phase = 0.0f;
        render::screens_update(&f.state, 1, static_cast<float>(count - 1) * 0.25f);
        CHECK(render::screen_frame(f.state, count) == count - 1);
        render::screens_update(&f.state, 1, 0.25f);
        CHECK(render::screen_frame(f.state, count) == 0);
        f.state.phase = 839.75f;
        render::screens_update(&f.state, 1, 0.0625f);
        CHECK(f.state.phase == 0.0f);
        CHECK(render::screen_frame(f.state, count) == 0);
    }
}

TEST_CASE("sixteen monitors retain distinct initial phases through updates and screen changes") {
    Fixture f;
    ScreenState states[16];
    for (size_t i = 0; i < 16; ++i) render::screen_state_init(states[i], i, f.pc, f.data);
    for (size_t i = 0; i < 16; ++i) {
        for (size_t j = i + 1; j < 16; ++j) CHECK(states[i].phase != states[j].phase);
    }
    CHECK(render::screen_frame(states[0], 4) != render::screen_frame(states[1], 4));
    render::screens_update(states, 16, 0.125f);
    CHECK(states[1].phase - states[0].phase == doctest::Approx(1.25f));
    const float phase = states[1].phase;
    f.pc.state = core::PcState::kBusy;
    f.pc.running_game = 0;
    render::screen_select(states[1], f.pc, f.data);
    CHECK(states[1].phase == phase);
}

TEST_CASE("invalid delta and empty arrays do not change animation or loading") {
    Fixture f;
    f.start();
    for (float dt : {-1.0f, std::numeric_limits<float>::infinity(),
                     std::numeric_limits<float>::quiet_NaN()}) {
        render::screens_update(&f.state, 1, dt);
        CHECK(f.state.phase == 0.0f);
        CHECK(f.state.loading_remaining == 2.0f);
    }
    render::screens_update(nullptr, 16, 1.0f);
    render::screens_update(&f.state, 0, 1.0f);
    CHECK(f.state.loading_remaining == 2.0f);
    render::screens_update(&f.state, 1, 210.25f);
    CHECK(f.state.phase == 1.0f);
    CHECK(f.state.loading_remaining == 0.0f);
}

TEST_CASE("screen assets reuse registry and each draw snapshots its own frame") {
    reset_gpu();
    g_loaded[0] = true;
    REQUIRE(render::screens_init());
    CHECK(g_loads == 11);
    CHECK(g_creates == 2);
    CHECK(g_meshes[0][0].u == 0.0f);
    CHECK(g_meshes[0][0].v == 1.0f);
    CHECK(g_meshes[0][2].u == 1.0f);
    CHECK(g_meshes[0][2].v == 0.0f);
    CHECK(g_meshes[0][2].x - g_meshes[0][0].x == doctest::Approx(0.32f));
    CHECK(g_meshes[0][2].y - g_meshes[0][0].y == doctest::Approx(0.24f));
    Fixture f;
    for (unsigned i = 0; i < 12; ++i) {
        f.state.screen = static_cast<ScreenId>(i);
        f.state.phase = 3.5f;
        render::screen_draw(f.state, core::mat4_identity());
        REQUIRE(g_draws.size() == i + 1);
        CHECK(g_draws.back().texture == i);
        CHECK(g_draws.back().frame == (g_counts[i] == 1 ? 0 : 3));
    }
    g_draws.clear();
    f.state.screen = ScreenId::kShooter;
    f.state.phase = 0.0f;
    render::screen_draw(f.state, core::mat4_identity());
    f.state.phase = 2.0f;
    render::screen_draw(f.state, core::mat4_identity());
    REQUIRE(g_draws.size() == 2);
    CHECK(g_draws[0].texture == g_draws[1].texture);
    CHECK(g_draws[0].frame == 0);
    CHECK(g_draws[1].frame == 2);
}

TEST_CASE("missing textures draw a dark grey untextured quad and mesh failure is reported") {
    reset_gpu(true);
    REQUIRE(render::screens_init());
    CHECK(g_loads == 12);
    Fixture f;
    render::screen_draw(f.state, core::mat4_identity());
    REQUIRE(g_draws.size() == 1);
    CHECK(g_draws[0].texture == render::kNoTexture);
    CHECK(g_meshes[g_draws[0].mesh][0].color == render::rgba(32, 32, 32));
    reset_gpu();
    g_mesh_failure = true;
    CHECK_FALSE(render::screens_init());
    render::screen_draw(f.state, core::mat4_identity());
    CHECK(g_draws.empty());
}
