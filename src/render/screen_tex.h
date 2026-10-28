#pragma once
// Экраны зала (T042, GDD §9.3). Ресурсами владеет render до render::shutdown().
// screens_init вызывает загрузчик сцены один раз после render::init; отсутствие
// текстур допустимо, false означает, что не удалось создать геометрию.
// ScreenState — POD вызывающего. Каждый элемент массива перед использованием
// инициализировать screen_state_init с индексом места. Ядро только читается,
// указатели не сохраняются. Уже занятый ПК при загрузке сразу показывает игру.
// В игровом цикле: screen_select для каждого ПК, затем screens_update и screen_draw.
// Все вызовы — с потока рендера; update/draw не выделяют память.
//
//     render::ScreenState states[core::kMaxPcs];
//     render::screens_init();
//     render::screen_state_init(states[0], 0, world.pcs[0], data);
//     ...
//     render::screen_select(states[0], world.pcs[0], data);
//     render::screens_update(states, 1, dt);  // реальные секунды, не минуты ядра
//     render::screen_draw(states[0], transform); // после begin_frame/set_camera
#include "core/core_api.h"
#include "render/render_api.h"

namespace render {

enum class ScreenId : uint8_t {
    kShooter, kRts, kTbs, kRpg, kSim, kRace, kAction, kCasual,
    kDesktop, kLoading, kBsod, kOff, kCount
};

constexpr float kScreenFramesPerSecond = 4.0f;
constexpr float kScreenLoadingSeconds = 2.0f;

struct ScreenState {
    ScreenId screen;
    ScreenId target;
    core::PcState previous_pc_state;
    core::Index previous_game;
    float phase;              // в кадрах; сохраняется при смене экрана
    float loading_remaining;  // реальные секунды
};

bool screens_init();
ScreenId screen_for_genre(core::Genre genre);
// Чистый выбор без перехода загрузки. Некорректная/неизвестная игра — desktop.
ScreenId screen_for_pc(const core::Pc& pc, const core::GameData& data);
void screen_state_init(ScreenState& state, size_t pc_index,
                       const core::Pc& pc, const core::GameData& data);
void screen_select(ScreenState& state, const core::Pc& pc, const core::GameData& data);
void screens_update(ScreenState* states, size_t count, float dt);
// frame_count берётся из TEX1; 0/1 (в том числе отсутствующая текстура) — кадр 0.
uint16_t screen_frame(const ScreenState& state, uint16_t frame_count);
// Плоскость XY 0,32 × 0,24 м, центр в начале координат, лицевая сторона +Z,
// UV 0..1, верх текстуры на +Y. transform размещает её внутри кинескопа.
void screen_draw(const ScreenState& state, const core::Mat4& transform);

}  // namespace render
