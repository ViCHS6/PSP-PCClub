#pragma once
// Игрок от первого лица (T010): ходьба и поворот по GDD §9.1, ограничение наклона,
// коллизия с AABB, притяжение липкого прицела.
//
// Владение: Player и Colliders — значения с фиксированными массивами, живут у сцены.
// Colliders заполняет сцена при загрузке (T014 — из расстановки зала); в кадре только читается.
//
// Кадр:
//
//     core::Vec3 aim;
//     const bool has_aim = interact.aim_target(&aim);
//     player.update(dt, input, colliders, has_aim ? &aim : nullptr);
//     interact.update(player.eye(), player.forward());
//     interact.handle_input(input);
//     render::set_camera(player.view(), projection);
//
// Оси — как у ассетов (ART.md, assets/placeholders/README.md): правая система, Y вверх, метры.
// yaw = 0 — взгляд в −Z, положительный yaw — поворот налево (против часовой сверху).
#include <array>
#include <cstdint>

#include "core/math.h"
#include "platform/platform_api.h"

namespace game {

// Числа «ощущений» управления. Это не баланс экономики, а подстройка под стик PSP:
// проверяются руками на Go (T015), поэтому лежат здесь константами, а не в data/.
namespace controls {
constexpr float kWalkSpeed = 1.4f;          // м/с — шаг взрослого
constexpr float kRunSpeed = 2.6f;           // м/с с R; зал 8×6 м — диагональ за ~4 с (GDD §9.1: ≤ 5–7 с)
constexpr float kStrafeSpeed = 0.8f;        // м/с, крестовина ←/→ — «слегка вбок», R не ускоряет (T015)
constexpr float kTurnSpeed = 2.4f;          // рад/с при стике до упора
constexpr float kLookYawSpeed = 2.0f;       // рад/с, L + стик
constexpr float kLookPitchSpeed = 1.6f;     // рад/с, L + стик
constexpr float kStickCurve = 2.0f;         // степень отклика стика: мелкие движения — точнее
constexpr float kPitchMin = -1.05f;         // −60°: вниз — на клавиатуру на столе
constexpr float kPitchMax = 0.87f;          // +50°: вверх — на полку и часы
constexpr float kPitchNeutral = -0.14f;     // −8°: куда возвращается взгляд при ходьбе
constexpr float kPitchReturn = 2.5f;        // 1/с — скорость возврата наклона при ходьбе
constexpr float kAimPull = 4.75f;           // 1/с — притяжение к цели (T015 на Go: −5 % от 5,0)
constexpr float kAimPullMaxInput = 0.6f;    // при повороте стика сильнее — притяжения нет (вырваться)
constexpr float kAimFriction = 0.57f;       // множитель поворота при цели (T015: замедление 45 % → 43 %)
constexpr float kEyeHeight = 1.6f;          // м — глаза стоящего взрослого
constexpr float kRadius = 0.25f;            // м — радиус игрока в плоскости XZ
constexpr float kHeight = 1.75f;            // м — для отбора препятствий по высоте
constexpr float kStepHeight = 0.2f;         // препятствия ниже — не мешают (пороги, ножки стула)
}  // namespace controls

struct Aabb {
    core::Vec3 min;
    core::Vec3 max;
};

// Статические препятствия: мебель, стены, стойка. Игрок — вертикальный цилиндр.
class Colliders {
public:
    static constexpr int kMax = 96;  // 6–16 мест × (стол + стул) + стены + стойка + полки

    bool add(const Aabb& box);  // false — массив полон
    void clear() { count_ = 0; }
    int count() const { return count_; }
    const Aabb& at(int i) const { return boxes_[i]; }

    // Выталкивает круг (center.xz, radius) из всех AABB, которые пересекают по высоте
    // [feet_y + kStepHeight, feet_y + kHeight]. Возвращает исправленную позицию.
    core::Vec3 resolve(core::Vec3 position, float radius) const;

private:
    std::array<Aabb, kMax> boxes_{};
    int count_ = 0;
};

class Player {
public:
    // position — ноги игрока (Y пола).
    void reset(core::Vec3 position, float yaw, float pitch = controls::kPitchNeutral);

    // aim_point — точка липкого прицела с прошлого кадра или nullptr.
    // L удерживается — стик управляет только взглядом (обзор), иначе Y — ходьба, X — поворот.
    // Крестовина ←/→ — шаг вбок, в любом режиме (в том числе с L).
    void update(float dt, const platform::InputState& input, const Colliders& colliders,
                const core::Vec3* aim_point);

    core::Vec3 position() const { return position_; }
    float yaw() const { return yaw_; }
    float pitch() const { return pitch_; }
    core::Vec3 eye() const;
    core::Vec3 forward() const;  // единичный вектор взгляда
    core::Mat4 view() const;

private:
    core::Vec3 position_{};
    float yaw_ = 0.0f;
    float pitch_ = controls::kPitchNeutral;
};

// Знак сохраняется, |v| возводится в степень kStickCurve.
float stick_response(float v);

// Разница углов a − b, приведённая к (−π, π].
float angle_delta(float a, float b);

}  // namespace game
