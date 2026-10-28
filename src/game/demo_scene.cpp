#include "game/demo_scene.h"

#include <cmath>
#include <cstdio>
#include "platform/audio_api.h"
#include <cstdlib>

namespace game {
namespace {

using render::rgba;
using render::Vertex;

alignas(16) uint8_t g_anim_bytes[64 * 1024];

// Куб 1×1×1 с центром в начале координат: 6 граней × 4 вершины, свой цвет у каждой грани.
render::MeshId create_cube() {
    struct Face {
        core::Vec3 normal, u, v;
        render::Color color;
    };
    const Face faces[6] = {
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}, rgba(216, 208, 184)},   // бежевый пластик
        {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}, rgba(184, 174, 148)},
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}, rgba(94, 122, 90)},     // панельный зелёный
        {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}, rgba(75, 100, 73)},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}, rgba(32, 80, 192)},     // синий экран
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}, rgba(143, 134, 112)},
    };
    Vertex vertices[24];
    uint16_t indices[36];
    for (int f = 0; f < 6; ++f) {
        const Face& face = faces[f];
        const float corners[4][2] = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}};
        for (int c = 0; c < 4; ++c) {
            const core::Vec3 p = face.normal * 0.5f + face.u * corners[c][0] + face.v * corners[c][1];
            vertices[f * 4 + c] = {corners[c][0] + 0.5f, 0.5f - corners[c][1], face.color, p.x, p.y, p.z};
        }
        const uint16_t base = static_cast<uint16_t>(f * 4);
        const uint16_t quad[6] = {0, 1, 2, 0, 2, 3};
        for (int i = 0; i < 6; ++i) indices[f * 6 + i] = static_cast<uint16_t>(base + quad[i]);
    }
    return render::mesh_create(vertices, 24, indices, 36);
}

// Пол 6×6 м, текстура «линолеум» повторяется 6 раз.
render::MeshId create_floor() {
    const render::Color c = render::kWhite;
    const Vertex vertices[4] = {
        {0, 0, c, -3, 0, -3},
        {6, 0, c, 3, 0, -3},
        {6, 6, c, 3, 0, 3},
        {0, 6, c, -3, 0, 3},
    };
    const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    return render::mesh_create(vertices, 4, indices, 6);
}

}  // namespace

bool DemoScene::init() {
    audio::sound_load_file("assets/sfx_click.wav");
    audio::music_play("assets/music_demo.wav", true);
    if (!render::screens_init()) return false;
    const core::Genre genres[8] = {
        core::kGenreShooterTeam, core::kGenreRts, core::kGenreTbs, core::kGenreArpg,
        core::kGenreSim, core::kGenreRace, core::kGenreAction, core::kGenreCasual
    };
    core::GameRec games[8]{};
    for (size_t i = 0; i < 8; ++i) games[i].genre = genres[i];
    core::GameData data;
    data.games = games;
    data.game_count = 8;
    for (size_t i = 0; i < 16; ++i) {
        core::Pc pc{};
        pc.state = core::PcState::kBusy;
        pc.running_game = static_cast<core::Index>(i < 8 ? i : 0);
        if (i == 8 || i == 9) pc.state = core::PcState::kIdle;
        if (i == 10) pc.state = core::PcState::kHung;
        if (i == 11) pc.state = core::PcState::kOff;
        render::screen_state_init(screens_[i], i, pc, data);
        if (i == 9) {
            pc.state = core::PcState::kBusy;
            render::screen_select(screens_[i], pc, data);
        }
    }
#ifdef PCCLUB_SCREEN_BENCH
    screen_bench_ = std::getenv("PCCLUB_SCREEN_BENCH") != nullptr;
    // Для замера два одинаковых запуска: 0 и 16 мониторов, по 150+ кадров.
    bench_monitors_ = std::getenv("PCCLUB_SCREEN_BENCH_16") != nullptr;
    if (screen_bench_) std::fprintf(stderr, "T042 benchmark: %u monitors; warmup 30, sample 120 frames\n",
                                    bench_monitors_ ? 16u : 0u);
#endif
    cube_ = create_cube();
    floor_ = create_floor();
    checker_ = render::texture_load_file("assets/demo_checker.tex");
    desk_ = render::model_load_file("assets/prop_desk_chipboard.msh");
    render::AnimArena arena{g_anim_bytes, sizeof(g_anim_bytes), 0};
    if (!render::anim_load_file(arena, "assets/chr_placeholder.anm", &animation_)) return false;
    for (uint16_t i = 0; i < animation_.view.part_count; ++i) {
        render::format::AnimPart part{};
        render::format::anim_part(animation_.view, i, &part);
        char path[96];
        std::snprintf(path, sizeof(path), "assets/chr_placeholder_%s.msh", part.name);
        parts_[i] = render::model_load_file(path);
        if (parts_[i] == render::kInvalidModel) return false;
    }
    if (!render::anim_start(anim_state_, animation_, render::anim_find_clip(animation_, "walk"))) return false;
    return cube_ != render::kInvalidMesh && floor_ != render::kInvalidMesh &&
           checker_ != render::kNoTexture && desk_ != render::kInvalidModel;
}

void DemoScene::update(float dt, const platform::InputState& input) {
    if (input.pressed & platform::kButtonTriangle) show_screens_ = !show_screens_;
    render::screens_update(screens_, 16, dt);
    // Единственный демонстрационный слот повторяет загрузку, чтобы она была видна
    // на снимке после 120 кадров; остальные состояния работают обычным образом.
    loading_demo_time_ += dt;
    if (loading_demo_time_ >= render::kScreenLoadingSeconds) {
        loading_demo_time_ = 0.0f;
        screens_[9].screen = render::ScreenId::kLoading;
        screens_[9].loading_remaining = render::kScreenLoadingSeconds;
    }
    constexpr float kTurnSpeed = 1.8f;  // рад/с
    yaw_ += input.stick_x * kTurnSpeed * dt;
    pitch_ += input.stick_y * kTurnSpeed * dt;
    if (input.held & platform::kButtonLeft) yaw_ -= kTurnSpeed * dt;
    if (input.held & platform::kButtonRight) yaw_ += kTurnSpeed * dt;
    if (input.held & platform::kButtonUp) pitch_ += kTurnSpeed * dt;
    if (input.held & platform::kButtonDown) pitch_ -= kTurnSpeed * dt;
    if (pitch_ < 0.05f) pitch_ = 0.05f;
    if (pitch_ > 1.4f) pitch_ = 1.4f;
    if (input.pressed & platform::kButtonCross) auto_spin_ = !auto_spin_;
    if (input.pressed & platform::kButtonCross) audio::play(audio::sound_find("sfx_click"), 0.8f);
    if (auto_spin_) spin_ += dt;
    if (input.pressed & platform::kButtonSquare) {
        const char* clips[] = {"walk", "sit_down", "type"};
        clip_index_ = static_cast<uint16_t>((clip_index_ + 1) % 3);
        render::anim_play(anim_state_, render::anim_find_clip(animation_, clips[clip_index_]));
    }
    render::anim_update(anim_state_, dt);
}

void DemoScene::draw() const {
    if (show_screens_) {
#ifdef PCCLUB_SCREEN_BENCH
        if (screen_bench_) {
            const uint64_t now = platform::time_us();
            if (bench_frame_ > 30 && bench_frame_ <= 150) bench_total_us_ += now - bench_previous_us_;
            if (bench_frame_ == 150) {
                std::fprintf(stderr, "T042 benchmark: monitors=%u frame_us=%u draw_calls=%u triangles=%u\n",
                    bench_monitors_ ? 16u : 0u, unsigned(bench_total_us_ / 120),
                    unsigned(render::stats().draw_calls), unsigned(render::stats().triangles));
            }
            bench_previous_us_ = now;
            ++bench_frame_;
        }
#endif
        // Сетка ниже включённого по умолчанию отладочного оверлея.
        const core::Mat4 view = core::mat4_look_at({0.0f, 0.25f, 1.75f}, {0.0f, 0.25f, 0.0f}, {0, 1, 0});
        const core::Mat4 projection = core::mat4_perspective(core::deg_to_rad(60.0f),
            static_cast<float>(render::kScreenWidth) / render::kScreenHeight, 0.1f, 10.0f);
        render::set_camera(view, projection);
#ifdef PCCLUB_SCREEN_BENCH
        if (screen_bench_ && !bench_monitors_) return;
#endif
        for (size_t i = 0; i < 16; ++i) {
            const float x = (static_cast<float>(i % 4) - 1.5f) * 0.60f;
            const float y = (1.5f - static_cast<float>(i / 4)) * 0.29f;
            render::screen_draw(screens_[i], core::mat4_translate({x, y, 0.0f}));
        }
        return;
    }
    constexpr float kDistance = 4.0f;
    const core::Vec3 target = {0, 0.5f, 0};
    const core::Vec3 eye = {
        target.x + kDistance * std::cos(pitch_) * std::sin(yaw_),
        target.y + kDistance * std::sin(pitch_),
        target.z + kDistance * std::cos(pitch_) * std::cos(yaw_),
    };
    const core::Mat4 view = core::mat4_look_at(eye, target, {0, 1, 0});
    const core::Mat4 projection = core::mat4_perspective(
        core::deg_to_rad(60.0f), static_cast<float>(render::kScreenWidth) / render::kScreenHeight, 0.1f, 50.0f);
    render::set_camera(view, projection);

    render::draw_mesh(floor_, core::mat4_identity(), checker_);
    const core::Mat4 model = core::mat4_translate({-0.9f, 0.65f, 0}) * core::mat4_rotate_y(spin_) *
                             core::mat4_rotate_x(spin_ * 0.6f);
    render::draw_mesh(cube_, model);
    render::draw_model(desk_, core::mat4_translate({0.8f, 0, 0}));
    core::Mat4 pose[render::format::kAnimMaxParts];
    const core::Mat4 character = core::mat4_translate({0.8f, 0, 0.6f}) * core::mat4_rotate_y(core::kPi);
    if (render::anim_pose(anim_state_, character, pose, render::format::kAnimMaxParts))
        render::anim_draw(animation_, parts_, pose, render::format::kAnimMaxParts);
}

}  // namespace game
