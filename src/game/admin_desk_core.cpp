// Чистая часть стойки (T033): поза камеры и замер обслуживания — без ui/render, для тестов.
#include "game/admin_desk.h"

#include <cmath>

namespace game {
namespace {

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
float smooth(float t) { return t * t * (3.0f - 2.0f * t); }

core::Vec3 pose_forward(float yaw, float pitch) {
    const float cp = std::cos(pitch);
    return {-std::sin(yaw) * cp, std::sin(pitch), -std::cos(yaw) * cp};
}

// Ушёл ли посетитель по решению админа: сел, не согласился после ответа Кассы, отказ.
bool served(const core::Visitor& v) {
    using core::LeaveReason;
    using core::VisitorState;
    if (v.state == VisitorState::kToSeat || v.state == VisitorState::kPlaying) return true;
    return v.state == VisitorState::kLeaving &&
           (v.leave_reason == LeaveReason::kNoGame || v.leave_reason == LeaveReason::kPrice ||
            v.leave_reason == LeaveReason::kRefused);
}

}  // namespace

core::Mat4 pose_view(const CameraPose& pose) {
    return core::mat4_look_at(pose.eye, pose.eye + pose_forward(pose.yaw, pose.pitch), {0.0f, 1.0f, 0.0f});
}

CameraPose pose_of(const Player& player) { return {player.eye(), player.yaw(), player.pitch()}; }

CameraPose screen_pose(const core::Mat4& screen, float distance) {
    const core::Vec3 center = core::mat4_transform_point(screen, {0.0f, 0.0f, 0.0f});
    const core::Vec3 normal = core::normalize(core::mat4_transform_point(screen, {0.0f, 0.0f, 1.0f}) - center);
    // Взгляд против нормали: −sin(yaw)·cos(pitch) = −n.x, −cos(yaw)·cos(pitch) = −n.z, sin(pitch) = −n.y.
    const float flat = std::sqrt(normal.x * normal.x + normal.z * normal.z);
    return {center + normal * distance, std::atan2(normal.x, normal.z), std::atan2(-normal.y, flat)};
}

CameraPose pose_lerp(const CameraPose& a, const CameraPose& b, float t) {
    const float s = smooth(clamp01(t));
    return {a.eye + (b.eye - a.eye) * s, angle_delta(a.yaw + angle_delta(b.yaw, a.yaw) * s, 0.0f),
            a.pitch + (b.pitch - a.pitch) * s};
}

CameraPose desk_pose(const CameraPose& player, const CameraPose& screen, float t) {
    CameraPose p = pose_lerp(player, screen, t);
    const float s = smooth(clamp01(t));
    const core::Vec3 normal = pose_forward(screen.yaw, screen.pitch) * -1.0f;  // из экрана к зрителю
    core::Vec3 approach = screen.eye + normal * (desk::kApproach - desk::kScreenDistance);
    if (core::dot(player.eye - screen.eye, normal) < 0.0f) approach.y += desk::kOverLift;
    const float u = 1.0f - s;
    p.eye = player.eye * (u * u) + approach * (2.0f * u * s) + screen.eye * (s * s);
    return p;
}

// ---------------------------------------------------------------------------

void ServiceTimer::start(const core::World& world) {
    armed_ = true;
    visitor_ = -1;
    elapsed_ = 0.0f;
    if (world.queue_len > 0) begin(world);
}

void ServiceTimer::stop() {
    armed_ = false;
    visitor_ = -1;
    elapsed_ = 0.0f;
}

void ServiceTimer::begin(const core::World& world) {
    visitor_ = world.queue[0];
    serial_ = world.visitors[visitor_].serial;
    elapsed_ = 0.0f;
}

void ServiceTimer::update(float dt, const core::World& world) {
    if (!armed_) return;
    if (visitor_ >= 0) {
        const core::Visitor& v = world.visitors[visitor_];
        const bool same = v.serial == serial_;
        if (same && world.queue_len > 0 && world.queue[0] == visitor_) {
            elapsed_ += dt;
            return;
        }
        if (same && served(v)) {
            last_ = elapsed_;
            if (count_ == 0 || elapsed_ < best_) best_ = elapsed_;
            if (elapsed_ > worst_) worst_ = elapsed_;
            total_ += elapsed_;
            ++count_;
        }
        visitor_ = -1;
        elapsed_ = 0.0f;
    }
    // Следующий в очереди встаёт к стойке сразу — его отсчёт с этого кадра.
    if (world.queue_len > 0) begin(world);
}

}  // namespace game
