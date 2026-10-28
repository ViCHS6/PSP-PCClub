#include "game/interact.h"

#include <cmath>

namespace game {
namespace {

constexpr float kScreenW = static_cast<float>(render::kScreenWidth);
constexpr float kScreenH = static_cast<float>(render::kScreenHeight);

// Конус липкого прицела (GDD §9.1). Узкий: шире — цель «прыгает» между соседними ПК.
constexpr float kStickyCone = 0.21f;      // рад, ≈ 12°
constexpr float kStickyKeepCone = 0.30f;  // рад, ≈ 17° — у текущей цели (гистерезис)

}  // namespace

bool ray_aabb(core::Vec3 o, core::Vec3 d, core::Vec3 bmin, core::Vec3 bmax, float max_t, float* out_t) {
    float t0 = 0.0f, t1 = max_t;
    const float origin[3] = {o.x, o.y, o.z};
    const float dir[3] = {d.x, d.y, d.z};
    const float lo[3] = {bmin.x, bmin.y, bmin.z};
    const float hi[3] = {bmax.x, bmax.y, bmax.z};
    for (int a = 0; a < 3; ++a) {
        if (std::fabs(dir[a]) < 1e-8f) {
            if (origin[a] < lo[a] || origin[a] > hi[a]) return false;
            continue;
        }
        const float inv = 1.0f / dir[a];
        float tn = (lo[a] - origin[a]) * inv;
        float tf = (hi[a] - origin[a]) * inv;
        if (tn > tf) {
            const float t = tn;
            tn = tf;
            tf = t;
        }
        if (tn > t0) t0 = tn;
        if (tf < t1) t1 = tf;
        if (t0 > t1) return false;
    }
    if (out_t) *out_t = t0;
    return true;
}

InteractId InteractSystem::add(const InteractDesc& desc) {
    if (!desc.hint_key || !desc.on_action) return kNoInteract;
    for (int i = 0; i < kMaxInteractables; ++i) {
        if (slots_[i].used) continue;
        slots_[i].desc = desc;
        slots_[i].used = true;
        ++count_;
        return static_cast<InteractId>(i);
    }
    return kNoInteract;
}

void InteractSystem::clear() {
    for (Slot& s : slots_) s.used = false;
    count_ = 0;
    hovered_ = kNoInteract;
}

const InteractDesc* InteractSystem::desc(InteractId id) const {
    if (id >= kMaxInteractables || !slots_[id].used) return nullptr;
    return &slots_[id].desc;
}

void InteractSystem::set_enabled(InteractId id, bool enabled) {
    if (id >= kMaxInteractables || !slots_[id].used) return;
    slots_[id].desc.enabled = enabled;
    if (!enabled && hovered_ == id) hovered_ = kNoInteract;
}

void InteractSystem::set_hint(InteractId id, const char* hint_key, int16_t hint_number) {
    if (id >= kMaxInteractables || !slots_[id].used || !hint_key) return;
    slots_[id].desc.hint_key = hint_key;
    slots_[id].desc.hint_number = hint_number;
}

void InteractSystem::set_alt(InteractId id, const char* alt_key) {
    if (id >= kMaxInteractables || !slots_[id].used) return;
    slots_[id].desc.alt_key = alt_key;
}

void InteractSystem::set_bounds(InteractId id, core::Vec3 bmin, core::Vec3 bmax, core::Vec3 aim) {
    if (id >= kMaxInteractables || !slots_[id].used) return;
    slots_[id].desc.bounds_min = bmin;
    slots_[id].desc.bounds_max = bmax;
    slots_[id].desc.aim_point = aim;
}

void InteractSystem::update(core::Vec3 eye, core::Vec3 forward) {
    const core::Vec3 dir = core::normalize(forward);
    InteractId ray_best = kNoInteract;
    float ray_t = 0.0f;
    InteractId cone_best = kNoInteract;
    float cone_angle = 0.0f;
    for (int i = 0; i < kMaxInteractables; ++i) {
        const Slot& s = slots_[i];
        if (!s.used || !s.desc.enabled) continue;
        const core::Vec3 to = s.desc.aim_point - eye;
        const float dist = core::length(to);
        if (dist > s.desc.max_distance) continue;

        float t = 0.0f;
        if (ray_aabb(eye, dir, s.desc.bounds_min, s.desc.bounds_max, s.desc.max_distance, &t) &&
            (ray_best == kNoInteract || t < ray_t)) {
            ray_best = static_cast<InteractId>(i);
            ray_t = t;
        }
        if (dist < 1e-4f) continue;
        float c = core::dot(to, dir) / dist;
        if (c > 1.0f) c = 1.0f;
        const float angle = std::acos(c);
        const float cone = (i == hovered_) ? kStickyKeepCone : kStickyCone;
        if (angle <= cone && (cone_best == kNoInteract || angle < cone_angle)) {
            cone_best = static_cast<InteractId>(i);
            cone_angle = angle;
        }
    }
    // Попадание лучом важнее конуса: игрок смотрит прямо на предмет.
    hovered_ = ray_best != kNoInteract ? ray_best : cone_best;
}

bool InteractSystem::handle_input(const platform::InputState& input) {
    if (hovered_ == kNoInteract) return false;
    const InteractDesc& d = slots_[hovered_].desc;
    if (input.pressed & platform::kButtonCross) {
        d.on_action(hovered_, InteractAction::kPrimary, d.user);
        return true;
    }
    if ((input.pressed & platform::kButtonSquare) && d.alt_key) {
        d.on_action(hovered_, InteractAction::kSecondary, d.user);
        return true;
    }
    return false;
}

InteractHint InteractSystem::hint() const {
    InteractHint h;
    if (hovered_ == kNoInteract) return h;
    const InteractDesc& d = slots_[hovered_].desc;
    h.id = hovered_;
    h.hint_key = d.hint_key;
    h.alt_key = d.alt_key;
    h.hint_number = d.hint_number;
    return h;
}

bool InteractSystem::aim_target(core::Vec3* out_point) const {
    if (hovered_ == kNoInteract) return false;
    if (out_point) *out_point = slots_[hovered_].desc.aim_point;
    return true;
}

bool InteractSystem::highlight_rect(const core::Mat4& vp, ScreenRect* out) const {
    if (hovered_ == kNoInteract) return false;
    const InteractDesc& d = slots_[hovered_].desc;
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int c = 0; c < 8; ++c) {
        const core::Vec3 p = {(c & 1) ? d.bounds_max.x : d.bounds_min.x,
                              (c & 2) ? d.bounds_max.y : d.bounds_min.y,
                              (c & 4) ? d.bounds_max.z : d.bounds_min.z};
        const float* m = vp.m;
        const float x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
        const float y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
        const float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
        // Угол за камерой: рамка по проекции была бы вывернута — растягиваем на весь экран.
        if (w < 0.05f) {
            x0 = y0 = -1e9f;
            x1 = y1 = 1e9f;
            break;
        }
        const float sx = (x / w * 0.5f + 0.5f) * kScreenW;
        const float sy = (0.5f - y / w * 0.5f) * kScreenH;
        x0 = std::fmin(x0, sx);
        y0 = std::fmin(y0, sy);
        x1 = std::fmax(x1, sx);
        y1 = std::fmax(y1, sy);
    }
    x0 = std::fmax(x0, 0.0f);
    y0 = std::fmax(y0, 0.0f);
    x1 = std::fmin(x1, kScreenW - 1.0f);
    y1 = std::fmin(y1, kScreenH - 1.0f);
    if (x1 <= x0 || y1 <= y0) return false;
    out->x = static_cast<int16_t>(x0);
    out->y = static_cast<int16_t>(y0);
    out->w = static_cast<int16_t>(x1 - x0);
    out->h = static_cast<int16_t>(y1 - y0);
    return true;
}

}  // namespace game
