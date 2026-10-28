#pragma once
// Стойка администратора ↔ ОС (T033, GDD §9.2): × у стойки — камера въезжает в монитор, ОС
// включается с открытой Кассой; ○ в Кассе (или на рабочем столе) — камера выезжает обратно.
// Здесь же замер быстрого сценария обслуживания (цель GDD §9.2, §11.2: ≤ 4 с) для оверлея.
//
// Владение: AdminDesk — значение у main; World — у вызывающего. Память в кадре не выделяется.
//
// Кадр (main):
//
//     desk.update(dt, input, world);   // ДО ui::update: ○ в Кассе — выход
//     ui::update(...);
//     scene.update(dt, desk.world_input() && !ui::active() ? input : platform::InputState{});
//     if (scene.take_os_request()) desk.enter(world);
//     ...
//     if (desk.draw_world()) scene.draw(desk.hud(), desk.camera(scene.player()));
//     desk.draw_fade();
//     ui::draw();
//
// Пока камера едет, мир не получает ввода; во время ОС 3D не рисуется (рабочий стол
// непрозрачен). Если ОС выключили не через стойку (START), стойка просто выезжает.
#include <cstdint>

#include "core/core_api.h"
#include "core/math.h"
#include "game/player.h"
#include "platform/platform_api.h"
#include "ui/ui_api.h"

namespace game {

namespace desk {
constexpr float kServiceTarget = 4.0f;  // с — GDD §9.2, §11.2
constexpr float kEnterTime = 0.5f;      // с — въезд в монитор; входит в замер
constexpr float kLeaveTime = 0.4f;      // с — выезд
constexpr float kFadeShare = 0.4f;      // доля пути у экрана, где мир растворяется в обоях ОС
// От экрана до глаза в конце въезда: экран 0,24 м по высоте заполняет вертикальный угол 60°
// (0,12 / tan 30° ≈ 0,208 м).
constexpr float kScreenDistance = 0.21f;
// Путь камеры идёт через точку на kApproach м перед экраном (за спиной сидящего админа).
// Если игрок нажал × с другой стороны стойки (позади экрана), эта точка поднята на kOverLift —
// камера перелетает над монитором, а не сквозь него.
constexpr float kApproach = 0.8f;
constexpr float kOverLift = 0.6f;
}  // namespace desk

// Поза камеры: глаз и углы как у Player (yaw = 0 — взгляд в −Z).
struct CameraPose {
    core::Vec3 eye{};
    float yaw = 0.0f;
    float pitch = 0.0f;
};

core::Mat4 pose_view(const CameraPose& pose);
CameraPose pose_of(const Player& player);
// Поза «лицом в экран»: screen — преобразование плоскости экрана (render::screen_draw,
// лицевая сторона +Z), глаз — на distance перед её центром.
CameraPose screen_pose(const core::Mat4& screen, float distance = desk::kScreenDistance);
// t ∈ [0, 1] со сглаживанием (smoothstep); yaw — по короткой дуге.
CameraPose pose_lerp(const CameraPose& a, const CameraPose& b, float t);
// Поза на пути от глаз игрока (t = 0) к экрану (t = 1): углы — как pose_lerp, глаз — по
// квадратичной кривой через точку подхода (desk::kApproach, desk::kOverLift).
CameraPose desk_pose(const CameraPose& player, const CameraPose& screen, float t);

// Замер обслуживания у стойки. Отсчёт идёт с × у стойки, если посетитель уже ждёт, иначе —
// с момента, когда он встал к стойке при открытой ОС. Кончается, когда посетитель перестал
// быть первым в очереди по решению админа: сел, ушёл после ответа Кассы (нет игры, цена) или
// получил отказ. Ушёл сам (терпение, закрытие, событие) или админ вышел из ОС — замер
// отбрасывается. Время — реальные секунды, не минуты ядра.
class ServiceTimer {
public:
    void start(const core::World& world);  // админ сел за ПК стойки
    void stop();                           // админ вышел из ОС
    void update(float dt, const core::World& world);

    bool armed() const { return armed_; }
    bool timing() const { return visitor_ >= 0; }
    float current() const { return elapsed_; }
    int count() const { return count_; }
    float last() const { return last_; }
    float best() const { return best_; }
    float worst() const { return worst_; }
    float average() const { return count_ > 0 ? total_ / static_cast<float>(count_) : 0.0f; }

private:
    void begin(const core::World& world);

    bool armed_ = false;
    int visitor_ = -1;       // индекс World::visitors, за которым следим
    uint16_t serial_ = 0;
    float elapsed_ = 0.0f;
    int count_ = 0;
    float last_ = 0.0f, best_ = 0.0f, worst_ = 0.0f, total_ = 0.0f;
};

class AdminDesk {
public:
    enum class State : uint8_t { kWorld, kEntering, kOs, kLeaving };

    // screen — преобразование экрана монитора стойки (ClubScene::admin_screen()),
    // cashier — программа, которая открывается при входе (ui::find_app("cashier")).
    void init(const core::Mat4& screen, ui::AppId cashier);

    // × у стойки. Игнорируется, если камера уже едет или ОС включена.
    void enter(const core::World* world);
    void update(float dt, const platform::InputState& input, const core::World* world);

    State state() const { return state_; }
    bool world_input() const { return state_ == State::kWorld; }
    bool draw_world() const { return state_ != State::kOs; }
    bool hud() const { return state_ == State::kWorld; }
    // Вид для ClubScene::draw: nullptr — камера игрока.
    const core::Mat4* camera(const Player& player);
    // Растворение в обои ОС у экрана — после 3D, до ui::draw().
    void draw_fade() const;

    const ServiceTimer& timer() const { return timer_; }

private:
    State state_ = State::kWorld;
    float t_ = 0.0f;  // 0 — у игрока, 1 — в экране
    CameraPose screen_{};
    core::Mat4 view_{};
    ui::AppId cashier_ = ui::kNoApp;
    ServiceTimer timer_;
};

}  // namespace game
