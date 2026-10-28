#pragma once
// ВРЕМЕННАЯ сцена T001/T012/T042: экраны, куб и стол. Заменяется залом клуба (T010, T014).
// PC-замер (включает swap/vsync, 30 кадров прогрева и 120 измерений):
//   PCCLUB_SCREEN_BENCH=1 build/pc/pcclub --frames 180
//   PCCLUB_SCREEN_BENCH=1 PCCLUB_SCREEN_BENCH_16=1 build/pc/pcclub --frames 180
// Треугольник (I на PC) переключает экраны и старую сцену.
#include "platform/platform_api.h"
#include "render/render_api.h"
#include "render/anim.h"
#include "render/screen_tex.h"

namespace game {

class DemoScene {
public:
    bool init();
    void update(float dt, const platform::InputState& input);
    void draw() const;

private:
    render::ScreenState screens_[16]{};
    float loading_demo_time_ = 0.0f;
    bool show_screens_ = true;  // треугольник переключает старую сцену T001
    // Поля PC-замера сохраняют одинаковую раскладку класса во всех единицах трансляции.
    bool screen_bench_ = false;
    bool bench_monitors_ = false;
    mutable unsigned bench_frame_ = 0;
    mutable uint64_t bench_previous_us_ = 0;
    mutable uint64_t bench_total_us_ = 0;
    render::MeshId cube_ = render::kInvalidMesh;
    render::MeshId floor_ = render::kInvalidMesh;
    render::TextureId checker_ = render::kNoTexture;
    render::ModelId desk_ = render::kInvalidModel;
    render::AnimSet animation_{};
    render::AnimState anim_state_{};
    render::ModelId parts_[render::format::kAnimMaxParts]{};
    uint16_t clip_index_ = 0;
    float yaw_ = 0.6f;
    float pitch_ = 0.35f;
    float spin_ = 0.0f;
    bool auto_spin_ = true;
};

}  // namespace game
