#include "render/anim.h"
#include "platform/platform_api.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>

namespace render {
static_assert(std::is_trivially_copyable<AnimState>::value);
static_assert(std::is_standard_layout<AnimState>::value);
static_assert(std::is_trivial<AnimState>::value);
namespace {

using Quat = format::AnimQuat;
struct LocalPose {
    Quat rotation[format::kAnimMaxParts];
    core::Vec3 root;
};

Quat nlerp(Quat a, Quat b, float t) {
    if (a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w < 0.0f) b = {-b.x, -b.y, -b.z, -b.w};
    Quat q{a.x + (b.x-a.x)*t, a.y + (b.y-a.y)*t, a.z + (b.z-a.z)*t, a.w + (b.w-a.w)*t};
    const float inv = 1.0f / std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    return {q.x*inv, q.y*inv, q.z*inv, q.w*inv};
}

bool valid_state(const AnimState& s) {
    return s.set && s.set->view.data && s.clip >= 0 && s.clip < s.set->view.clip_count;
}

float advance(const format::AnimClip& c, float time, float dt) {
    const float duration = float(c.loop ? c.frame_count : c.frame_count - 1) / 15.0f;
    if (!c.loop) return time + std::min(dt, std::max(0.0f, duration - time));
    // Остаток до сложения предотвращает переполнение при очень большом конечном dt.
    return std::fmod(time + std::fmod(dt, duration), duration);
}

LocalPose sample(const AnimSet& set, int16_t clip, float time) {
    format::AnimClip c{};
    format::anim_clip(set.view, static_cast<uint16_t>(clip), &c);
    const float frame = time * 15.0f;
    const auto a = static_cast<uint16_t>(std::min(float(c.frame_count - 1), std::floor(frame)));
    const auto b = static_cast<uint16_t>(a + 1 < c.frame_count ? a + 1 : (c.loop ? 0 : a));
    const float t = std::min(1.0f, std::max(0.0f, frame - float(a)));
    LocalPose pose{};
    float ra[3], rb[3];
    format::anim_root(set.view, c, a, ra);
    format::anim_root(set.view, c, b, rb);
    pose.root = {ra[0] + (rb[0]-ra[0])*t, ra[1] + (rb[1]-ra[1])*t, ra[2] + (rb[2]-ra[2])*t};
    for (uint16_t p = 0; p < set.view.part_count; ++p)
        pose.rotation[p] = nlerp(format::anim_rotation(set.view, c, a, p),
                                 format::anim_rotation(set.view, c, b, p), t);
    return pose;
}

LocalPose local_pose(const AnimState& s) {
    LocalPose result = sample(*s.set, s.clip, s.time);
    if (s.previous_clip < 0) return result;
    LocalPose source{};
    if (s.frozen_source) {
        std::memcpy(source.rotation, s.source_rotation, sizeof(source.rotation));
        source.root = {s.source_root[0], s.source_root[1], s.source_root[2]};
    } else {
        source = sample(*s.set, s.previous_clip, s.previous_time);
    }
    const float t = std::min(1.0f, s.fade_time / s.fade_duration);
    result.root = source.root + (result.root - source.root) * t;
    for (uint16_t p = 0; p < s.set->view.part_count; ++p)
        result.rotation[p] = nlerp(source.rotation[p], result.rotation[p], t);
    return result;
}

core::Mat4 rotation_matrix(Quat q) {
    const float x=q.x, y=q.y, z=q.z, w=q.w;
    return {{1.0f-2.0f*(y*y+z*z), 2.0f*(x*y+z*w), 2.0f*(x*z-y*w), 0.0f,
             2.0f*(x*y-z*w), 1.0f-2.0f*(x*x+z*z), 2.0f*(y*z+x*w), 0.0f,
             2.0f*(x*z+y*w), 2.0f*(y*z-x*w), 1.0f-2.0f*(x*x+y*y), 0.0f,
             0.0f, 0.0f, 0.0f, 1.0f}};
}

}  // namespace

bool anim_load(AnimArena& arena, const void* data, size_t size, AnimSet* out, const char** error) {
    if (out) *out = {};
    if (error) *error = nullptr;
    format::AnimView view{};
    if (!format::validate_anim(data, size, &view, error)) return false;
    auto fail = [&](const char* reason) { if (error) *error = reason; return false; };
    if (!out || !arena.data || (reinterpret_cast<uintptr_t>(arena.data) & 15) || arena.used > arena.size)
        return fail("ANM1 invalid arena/output");
    if (arena.used > SIZE_MAX - 15) return fail("ANM1 arena overflow");
    const size_t start = (arena.used + 15) & ~size_t(15);
    if (start > arena.size || size > arena.size - start) return fail("ANM1 arena exhausted");
    std::memcpy(arena.data + start, data, size);
    view.data = arena.data + start;
    *out = {view};
    arena.used = start + size;
    return true;
}

bool anim_load_file(AnimArena& arena, const char* path, AnimSet* out) {
    if (out) *out = {};
    size_t size = 0, read = 0;
    if (!path || !out || !platform::file_size(path, &size) || size < 32 || size > format::kAnimMaxFileBytes) {
        std::fprintf(stderr, "%s: ANM1 file size/read error\n", path ? path : "<null>");
        return false;
    }
    void* data = std::malloc(size);
    if (!data) {
        std::fprintf(stderr, "%s: ANM1 temporary buffer allocation failed\n", path);
        return false;
    }
    const char* error = "ANM1 file read error";
    const bool ok = platform::file_read(path, data, size, &read) && read == size &&
                    anim_load(arena, data, size, out, &error);
    if (!ok) std::fprintf(stderr, "%s: %s\n", path, error);
    std::free(data);
    return ok;
}

int16_t anim_find_clip(const AnimSet& set, const char* name) {
    if (!set.view.data || !name) return -1;
    for (uint16_t i = 0; i < set.view.clip_count; ++i) {
        format::AnimClip c{};
        format::anim_clip(set.view, i, &c);
        if (!std::strcmp(c.name, name)) return static_cast<int16_t>(i);
    }
    return -1;
}

bool anim_start(AnimState& state, const AnimSet& set, int16_t clip) {
    if (!set.view.data || clip < 0 || clip >= set.view.clip_count) return false;
    state = {};
    state.set = &set;
    state.clip = clip;
    state.previous_clip = -1;
    return true;
}

bool anim_play(AnimState& s, int16_t clip, float fade_seconds) {
    if (!valid_state(s) || clip < 0 || clip >= s.set->view.clip_count ||
        !std::isfinite(fade_seconds) || fade_seconds < 0.0f) return false;
    if (clip == s.clip) return true;
    const bool interrupted = s.previous_clip >= 0;
    if (interrupted && fade_seconds > 0.0f) {
        const LocalPose source = local_pose(s);
        std::memcpy(s.source_rotation, source.rotation, sizeof(s.source_rotation));
        s.source_root[0] = source.root.x;
        s.source_root[1] = source.root.y;
        s.source_root[2] = source.root.z;
    }
    s.previous_clip = fade_seconds > 0.0f ? s.clip : -1;
    s.previous_time = s.time;
    s.clip = clip;
    s.time = s.fade_time = 0.0f;
    s.fade_duration = fade_seconds;
    s.frozen_source = interrupted;
    return true;
}

void anim_update(AnimState& s, float dt) {
    if (!valid_state(s) || !std::isfinite(dt) || dt < 0.0f) return;
    format::AnimClip c{};
    format::anim_clip(s.set->view, static_cast<uint16_t>(s.clip), &c);
    s.time = advance(c, s.time, dt);
    if (s.previous_clip >= 0) {
        if (!s.frozen_source) {
            format::anim_clip(s.set->view, static_cast<uint16_t>(s.previous_clip), &c);
            s.previous_time = advance(c, s.previous_time, dt);
        }
        s.fade_time += std::min(dt, std::max(0.0f, s.fade_duration - s.fade_time));
        if (s.fade_time >= s.fade_duration) s.previous_clip = -1;
    }
}

bool anim_pose(const AnimState& s, const core::Mat4& world, core::Mat4* out, size_t capacity) {
    if (!valid_state(s) || !out || capacity < s.set->view.part_count) return false;
    const LocalPose pose = local_pose(s);
    for (uint16_t i = 0; i < s.set->view.part_count; ++i) {
        format::AnimPart part{};
        format::anim_part(s.set->view, i, &part);
        core::Mat4 local = rotation_matrix(pose.rotation[i]);
        local.m[12] = part.offset[0];
        local.m[13] = part.offset[1];
        local.m[14] = part.offset[2];
        if (part.parent < 0) {
            local.m[12] += pose.root.x;
            local.m[13] += pose.root.y;
            local.m[14] += pose.root.z;
        }
        out[i] = (part.parent < 0 ? world : out[part.parent]) * local;
    }
    return true;
}

void anim_draw(const AnimSet& set, const ModelId* models, const core::Mat4* pose, size_t count) {
    if (!set.view.data || !models || !pose || count < set.view.part_count) return;
    for (uint16_t i = 0; i < set.view.part_count; ++i) draw_model(models[i], pose[i]);
}

}  // namespace render
