#include "game/demo_scene.h"

#include <cmath>

namespace game {
namespace {

using render::rgba;
using render::Vertex;

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
    cube_ = create_cube();
    floor_ = create_floor();
    checker_ = render::texture_load_file("assets/demo_checker.tex");
    desk_ = render::model_load_file("assets/prop_desk_chipboard.msh");
    return cube_ != render::kInvalidMesh && floor_ != render::kInvalidMesh &&
           checker_ != render::kNoTexture && desk_ != render::kInvalidModel;
}

void DemoScene::update(float dt, const platform::InputState& input) {
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
    if (auto_spin_) spin_ += dt;
}

void DemoScene::draw() const {
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
}

}  // namespace game
