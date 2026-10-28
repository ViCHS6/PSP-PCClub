#pragma once
// Игрок от первого лица (T010): ходьба крестовиной и обзор стиком по GDD §9.1, ограничение наклона,
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
// Схема после проверки на Go (T015): крестовина — ходьба, стик — только обзор.
constexpr float kWalkSpeed = 1.4f;          // м/с, крестовина ↑/↓ — шаг взрослого
constexpr float kRunSpeed = 2.6f;           // м/с с R; зал 8×6 м — диагональ за ~4 с (GDD §9.1: ≤ 5–7 с)
constexpr float kStrafeSpeed = 0.8f;        // м/с, крестовина ←/→ — «слегка вбок», R не ускоряет
constexpr float kLookYawSpeed = 2.4f;       // рад/с, стик X до упора (≈ 137°/с)
constexpr float kLookPitchSpeed = 1.5f;     // рад/с, стик Y до упора
constexpr float kLookDeadZone = 0.08f;      // доля хода после мёртвой зоны платформы (0,2): дрожь стика Go
constexpr float kLookLinear = 0.3f;         // отклик = 0,3·a + 0,7·a²: малый наклон — точный доворот,
                                            // но без «залипания» чистого квадрата у центра
constexpr float kPitchMin = -1.05f;         // −60°: вниз — на клавиатуру на столе
constexpr float kPitchMax = 0.87f;          // +50°: вверх — на полку и часы
constexpr float kPitchNeutral = -0.14f;     // −8°: наклон по умолчанию в reset()
constexpr float kAimPull = 4.75f;           // 1/с — притяжение к цели (T015 на Go: −5 % от 5,0)
constexpr float kAimPullFade = 0.3f;        // |стик| ≥ этого — притяжения нет; между 0 и им — линейно слабее
constexpr float kAimFriction = 0.57f;       // множитель скорости обзора, пока есть цель
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
    // Крестовина ↑/↓ — вперёд/назад (R — бег), ←/→ — шаг вбок, диагональ нормируется.
    // Стик — обзор: X — поворот, Y — наклон. L не используется.
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

// Отклик стика обзора: мёртвая зона kLookDeadZone, затем kLookLinear·a + (1 − kLookLinear)·a².
// Знак сохраняется, |v| ≥ 1 → ±1.
float stick_response(float v);

// Разница углов a − b, приведённая к (−π, π].
float angle_delta(float a, float b);

}  // namespace game
