#include "game/player.h"

#include <cmath>

namespace game {

float stick_response(float v) {
    using namespace controls;
    float a = std::fabs(v);
    if (a <= kLookDeadZone) return 0.0f;
    a = (a - kLookDeadZone) / (1.0f - kLookDeadZone);
    if (a > 1.0f) a = 1.0f;
    a = kLookLinear * a + (1.0f - kLookLinear) * a * a;
    return v < 0.0f ? -a : a;
}

float angle_delta(float a, float b) {
    float d = std::fmod(a - b, 2.0f * core::kPi);
    if (d > core::kPi) d -= 2.0f * core::kPi;
    if (d <= -core::kPi) d += 2.0f * core::kPi;
    return d;
}

bool Colliders::add(const Aabb& box) {
    if (count_ >= kMax) return false;
    boxes_[count_++] = box;
    return true;
}

core::Vec3 Colliders::resolve(core::Vec3 p, float radius) const {
    // Две итерации: в углу между двумя ящиками первое выталкивание может завести во второй.
    for (int iter = 0; iter < 2; ++iter) {
        for (int i = 0; i < count_; ++i) {
            const Aabb& b = boxes_[i];
            if (b.max.y <= p.y + controls::kStepHeight || b.min.y >= p.y + controls::kHeight) continue;
            const float cx = std::fmin(std::fmax(p.x, b.min.x), b.max.x);
            const float cz = std::fmin(std::fmax(p.z, b.min.z), b.max.z);
            const float dx = p.x - cx;
            const float dz = p.z - cz;
            const float d2 = dx * dx + dz * dz;
            if (d2 >= radius * radius) continue;
            if (d2 > 1e-8f) {
                const float d = std::sqrt(d2);
                const float push = (radius - d) / d;
                p.x += dx * push;
                p.z += dz * push;
            } else {
                // Центр внутри ящика — к ближайшей грани.
                const float left = p.x - b.min.x, right = b.max.x - p.x;
                const float back = p.z - b.min.z, front = b.max.z - p.z;
                const float m = std::fmin(std::fmin(left, right), std::fmin(back, front));
                if (m == left) p.x = b.min.x - radius;
                else if (m == right) p.x = b.max.x + radius;
                else if (m == back) p.z = b.min.z - radius;
                else p.z = b.max.z + radius;
            }
        }
    }
    return p;
}

void Player::reset(core::Vec3 position, float yaw, float pitch) {
    position_ = position;
    yaw_ = yaw;
    pitch_ = pitch;
}

core::Vec3 Player::eye() const { return {position_.x, position_.y + controls::kEyeHeight, position_.z}; }

core::Vec3 Player::forward() const {
    const float cp = std::cos(pitch_);
    return {-std::sin(yaw_) * cp, std::sin(pitch_), -std::cos(yaw_) * cp};
}

core::Mat4 Player::view() const {
    const core::Vec3 e = eye();
    return core::mat4_look_at(e, e + forward(), {0.0f, 1.0f, 0.0f});
}

void Player::update(float dt, const platform::InputState& input, const Colliders& colliders,
                    const core::Vec3* aim_point) {
    using namespace controls;
    // Обзор стиком (GDD §9.1). Над целью медленнее — легче остановиться на предмете.
    const float friction = aim_point ? kAimFriction : 1.0f;
    yaw_ -= stick_response(input.stick_x) * kLookYawSpeed * friction * dt;
    pitch_ += stick_response(input.stick_y) * kLookPitchSpeed * friction * dt;

    // Липкий прицел не борется с ручным обзором: чем сильнее отклонён стик, тем слабее
    // притяжение, с |стик| ≥ kAimPullFade его нет совсем.
    const float stick = std::fmax(std::fabs(input.stick_x), std::fabs(input.stick_y));
    const float pull = 1.0f - stick / kAimPullFade;
    if (aim_point && pull > 0.0f) {
        const core::Vec3 to = *aim_point - eye();
        const float flat = std::sqrt(to.x * to.x + to.z * to.z);
        if (flat > 1e-3f) {
            const float target_yaw = std::atan2(-to.x, -to.z);
            const float target_pitch = std::atan2(to.y, flat);
            const float k = 1.0f - std::exp(-kAimPull * pull * dt);
            yaw_ += angle_delta(target_yaw, yaw_) * k;
            pitch_ += (target_pitch - pitch_) * k;
        }
    }

    if (pitch_ < kPitchMin) pitch_ = kPitchMin;
    if (pitch_ > kPitchMax) pitch_ = kPitchMax;
    yaw_ = angle_delta(yaw_, 0.0f);

    // Ходьба крестовиной: вперёд — (−sin, −cos), вправо — (cos, −sin) при данном yaw.
    float move = 0.0f, side = 0.0f;
    if (input.held & platform::kButtonUp) move += 1.0f;
    if (input.held & platform::kButtonDown) move -= 1.0f;
    if (input.held & platform::kButtonRight) side += 1.0f;
    if (input.held & platform::kButtonLeft) side -= 1.0f;
    if (move != 0.0f || side != 0.0f) {
        const float speed = (input.held & platform::kButtonR) ? kRunSpeed : kWalkSpeed;
        float fwd = move * speed;
        float right = side * kStrafeSpeed;
        if (move != 0.0f && side != 0.0f) {
            // Диагональ не быстрее прямого шага.
            const float len = std::sqrt(fwd * fwd + right * right);
            const float scale = (speed > kStrafeSpeed ? speed : kStrafeSpeed) / len;
            fwd *= scale;
            right *= scale;
        }
        const float s = std::sin(yaw_), c = std::cos(yaw_);
        position_.x += (-s * fwd + c * right) * dt;
        position_.z += (-c * fwd - s * right) * dt;
    }
    position_ = colliders.resolve(position_, kRadius);
}

}  // namespace game
