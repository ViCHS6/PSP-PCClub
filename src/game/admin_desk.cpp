#include "game/admin_desk.h"

#include "render/render_api.h"

namespace game {
namespace {

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

}  // namespace

void AdminDesk::init(const core::Mat4& screen, ui::AppId cashier) {
    screen_ = screen_pose(screen);
    cashier_ = cashier;
    state_ = State::kWorld;
    t_ = 0.0f;
}

void AdminDesk::enter(const core::World* world) {
    if (state_ != State::kWorld || ui::active()) return;
    state_ = State::kEntering;
    t_ = 0.0f;
    if (world) timer_.start(*world);
}

void AdminDesk::update(float dt, const platform::InputState& input, const core::World* world) {
    // Касса — kAppNoClose: ○ в ней не закрывает окно, а выводит из ОС. ○ на рабочем столе
    // (или START) выключает ОС сам — тогда просто выезжаем.
    const bool circle = (input.pressed & platform::kButtonCircle) && ui::active() && ui::active_app() == cashier_;
    switch (state_) {
    case State::kWorld:
        if (circle) ui::set_active(false);  // ОС открыли по START — выход без камеры
        break;
    case State::kEntering:
        t_ += dt / desk::kEnterTime;
        if (t_ >= 1.0f) {
            t_ = 1.0f;
            state_ = State::kOs;
            // Заново: on_open Кассы ставит фокус на список мест с лучшим местом (T031).
            if (cashier_ != ui::kNoApp) {
                ui::close_app(cashier_);
                ui::open_app(cashier_);
            }
            ui::set_active(true);
        }
        break;
    case State::kOs:
        if (circle) ui::set_active(false);
        if (!ui::active()) {
            state_ = State::kLeaving;
            timer_.stop();
        }
        break;
    case State::kLeaving:
        t_ -= dt / desk::kLeaveTime;
        if (t_ <= 0.0f) {
            t_ = 0.0f;
            state_ = State::kWorld;
        }
        break;
    }
    if (world) timer_.update(dt, *world);
}

const core::Mat4* AdminDesk::camera(const Player& player) {
    if (state_ == State::kWorld) return nullptr;
    view_ = pose_view(desk_pose(pose_of(player), screen_, t_));
    return &view_;
}

void AdminDesk::draw_fade() const {
    if (state_ != State::kEntering && state_ != State::kLeaving) return;
    const float fade = clamp01((t_ - (1.0f - desk::kFadeShare)) / desk::kFadeShare);
    if (fade <= 0.0f) return;
    const auto alpha = static_cast<uint32_t>(fade * 255.0f + 0.5f);
    render::draw_rect(0, 0, render::kScreenWidth, render::kScreenHeight, (ui::kColDesktop & 0x00FFFFFFu) | (alpha << 24));
}

}  // namespace game
