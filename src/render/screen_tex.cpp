#include "render/screen_tex.h"

#include <cmath>
#include <cstdio>

namespace render {
namespace {

constexpr size_t kScreenCount = static_cast<size_t>(ScreenId::kCount);
constexpr const char* kScreenNames[kScreenCount] = {
    "scr_shooter", "scr_rts", "scr_tbs", "scr_rpg", "scr_sim", "scr_race",
    "scr_action", "scr_casual", "scr_desktop", "scr_loading", "scr_bsod", "scr_off"
};
// НОК 1..8: ограничиваем float без скачка кадра для любой ленты A011.
constexpr float kPhasePeriod = 840.0f;
TextureId g_textures[kScreenCount];
MeshId g_quad = kInvalidMesh;
MeshId g_fallback_quad = kInvalidMesh;

MeshId create_quad(Color color) {
    const Vertex vertices[4] = {
        {0.0f, 1.0f, color, -0.16f, -0.12f, 0.0f},
        {1.0f, 1.0f, color,  0.16f, -0.12f, 0.0f},
        {1.0f, 0.0f, color,  0.16f,  0.12f, 0.0f},
        {0.0f, 0.0f, color, -0.16f,  0.12f, 0.0f}
    };
    const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    return mesh_create(vertices, 4, indices, 6);
}

}  // namespace

bool screens_init() {
    for (size_t i = 0; i < kScreenCount; ++i) {
        g_textures[i] = texture_find(kScreenNames[i]);
        if (g_textures[i] == kNoTexture) {
            char path[64];
            std::snprintf(path, sizeof(path), "assets/%s.tex", kScreenNames[i]);
            g_textures[i] = texture_load_file(path);
        }
        if (g_textures[i] == kNoTexture) {
            std::fprintf(stderr, "screen_tex: %s missing; using dark grey\n", kScreenNames[i]);
        }
    }
    g_quad = create_quad(kWhite);
    g_fallback_quad = create_quad(rgba(32, 32, 32));
    return g_quad != kInvalidMesh && g_fallback_quad != kInvalidMesh;
}

ScreenId screen_for_genre(core::Genre genre) {
    switch (genre) {
        case core::kGenreShooterTeam:
        case core::kGenreShooterArena:
        case core::kGenreShooter3p: return ScreenId::kShooter;
        case core::kGenreRts: return ScreenId::kRts;
        case core::kGenreTbs: return ScreenId::kTbs;
        case core::kGenreArpg:
        case core::kGenreMmo: return ScreenId::kRpg;
        case core::kGenreSim: return ScreenId::kSim;
        case core::kGenreRace:
        case core::kGenreSport: return ScreenId::kRace;
        case core::kGenreAction: return ScreenId::kAction;
        case core::kGenreCasual:
        case core::kGenreMinigame: return ScreenId::kCasual;
        default: return ScreenId::kDesktop;
    }
}

ScreenId screen_for_pc(const core::Pc& pc, const core::GameData& data) {
    switch (pc.state) {
        case core::PcState::kIdle: return ScreenId::kDesktop;
        case core::PcState::kHung: return ScreenId::kBsod;
        case core::PcState::kBusy:
            if (data.games && pc.running_game < data.game_count) {
                return screen_for_genre(static_cast<core::Genre>(data.games[pc.running_game].genre));
            }
            return ScreenId::kDesktop;
        default: return ScreenId::kOff;
    }
}

void screen_state_init(ScreenState& state, size_t pc_index,
                       const core::Pc& pc, const core::GameData& data) {
    state.screen = state.target = screen_for_pc(pc, data);
    state.previous_pc_state = pc.state;
    state.previous_game = pc.running_game;
    // Различаются и целый кадр, и дробный момент следующего переключения.
    state.phase = std::fmod(static_cast<float>(pc_index % 672) * 1.25f, kPhasePeriod);
    state.loading_remaining = 0.0f;
}

void screen_select(ScreenState& state, const core::Pc& pc, const core::GameData& data) {
    state.target = screen_for_pc(pc, data);
    const bool starting = state.previous_pc_state == core::PcState::kIdle;
    const bool changed = state.previous_game != pc.running_game;
    if (pc.state == core::PcState::kBusy && state.target != ScreenId::kDesktop && (starting || changed)) {
        state.loading_remaining = kScreenLoadingSeconds;
    } else if (pc.state != core::PcState::kBusy || state.target == ScreenId::kDesktop) {
        state.loading_remaining = 0.0f;
    }
    state.screen = state.loading_remaining > 0.0f ? ScreenId::kLoading : state.target;
    state.previous_pc_state = pc.state;
    state.previous_game = pc.running_game;
}

void screens_update(ScreenState* states, size_t count, float dt) {
    if (!states || !std::isfinite(dt) || dt < 0.0f) return;
    const float advance = std::fmod(dt, kPhasePeriod / kScreenFramesPerSecond) * kScreenFramesPerSecond;
    for (size_t i = 0; i < count; ++i) {
        ScreenState& state = states[i];
        state.phase = std::fmod(state.phase + advance, kPhasePeriod);
        if (state.loading_remaining > 0.0f) {
            state.loading_remaining = state.loading_remaining > dt ? state.loading_remaining - dt : 0.0f;
            if (state.loading_remaining == 0.0f) state.screen = state.target;
        }
    }
}

uint16_t screen_frame(const ScreenState& state, uint16_t frame_count) {
    if (frame_count <= 1) return 0;
    return static_cast<uint16_t>(std::fmod(state.phase, static_cast<float>(frame_count)));
}

void screen_draw(const ScreenState& state, const core::Mat4& transform) {
    if (g_quad == kInvalidMesh) return;
    const size_t index = static_cast<size_t>(state.screen);
    const TextureId texture = index < kScreenCount ? g_textures[index] : kNoTexture;
    if (texture == kNoTexture) {
        draw_mesh(g_fallback_quad, transform);
        return;
    }
    // draw_mesh выбирает GL-кадр / записывает адрес TEX1 в список GU сразу.
    texture_set_frame(texture, screen_frame(state, texture_frame_count(texture)));
    draw_mesh(g_quad, transform, texture);
}

}  // namespace render
