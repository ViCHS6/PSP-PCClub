#include "game/player.h"

#include <cmath>

namespace game {

float stick_response(float v) {
    const float a = std::pow(std::fabs(v), controls::kStickCurve);
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
    const bool look = (input.held & platform::kButtonL) != 0;
    const float sx = stick_response(input.stick_x);
    const float sy = stick_response(input.stick_y);
    const float friction = aim_point ? kAimFriction : 1.0f;

    float walk = 0.0f;
    if (look) {
        // GDD §9.1: L + стик — свободный обзор, без ходьбы.
        yaw_ -= sx * kLookYawSpeed * friction * dt;
        pitch_ += sy * kLookPitchSpeed * friction * dt;
    } else {
        yaw_ -= sx * kTurnSpeed * friction * dt;
        walk = sy;
    }

    // Липкий прицел: тянем взгляд к цели, пока игрок не крутит стик сильно.
    const float turn_input = std::fabs(input.stick_x) + (look ? std::fabs(input.stick_y) : 0.0f);
    if (aim_point && turn_input < kAimPullMaxInput) {
        const core::Vec3 to = *aim_point - eye();
        const float flat = std::sqrt(to.x * to.x + to.z * to.z);
        if (flat > 1e-3f) {
            const float target_yaw = std::atan2(-to.x, -to.z);
            const float target_pitch = std::atan2(to.y, flat);
            const float k = 1.0f - std::exp(-kAimPull * dt);
            yaw_ += angle_delta(target_yaw, yaw_) * k;
            pitch_ += (target_pitch - pitch_) * k;
        }
    } else if (!look && std::fabs(walk) > 0.3f) {
        // При ходьбе без цели взгляд возвращается к горизонту: на одном стике наклон иначе не поправить.
        const float k = 1.0f - std::exp(-kPitchReturn * dt);
        pitch_ += (kPitchNeutral - pitch_) * k;
    }

    if (pitch_ < kPitchMin) pitch_ = kPitchMin;
    if (pitch_ > kPitchMax) pitch_ = kPitchMax;
    yaw_ = angle_delta(yaw_, 0.0f);

    if (walk != 0.0f) {
        const float speed = (input.held & platform::kButtonR) ? kRunSpeed : kWalkSpeed;
        const float step = walk * speed * dt;
        position_.x += -std::sin(yaw_) * step;
        position_.z += -std::cos(yaw_) * step;
    }
    position_ = colliders.resolve(position_, kRadius);
}

}  // namespace game
