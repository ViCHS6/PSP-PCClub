#include "render/anim_format.h"
#include "render/mesh_format.h"

#include <cmath>
#include <cstring>

namespace render::format {
namespace {

int32_t signed_word(const uint8_t* p) {
    const uint16_t word = read_u16(p);
    return word < 32768 ? int32_t(word) : int32_t(word) - 65536;
}

bool valid_name(const uint8_t* p) {
    if (p[0] < 'a' || p[0] > 'z') return false;
    size_t i = 1;
    for (; i < 32 && p[i]; ++i)
        if (!((p[i] >= 'a' && p[i] <= 'z') || (p[i] >= '0' && p[i] <= '9') || p[i] == '_'))
            return false;
    if (i == 32) return false;
    for (; i < 32; ++i) if (p[i]) return false;
    return true;
}

const uint8_t* key(const AnimView& v, const AnimClip& c, uint16_t frame) {
    return v.data + c.keys_offset + size_t(frame) * (12 + 8 * v.part_count);
}

}  // namespace

bool anim_part(const AnimView& v, uint16_t index, AnimPart* out) {
    if (!v.data || !out || index >= v.part_count) return false;
    const uint8_t* p = v.data + 32 + size_t(index) * 48;
    std::memcpy(out->name, p, 32);
    out->parent = static_cast<int16_t>(signed_word(p + 32));
    for (size_t a = 0; a < 3; ++a) out->offset[a] = read_f32(p + 36 + a * 4);
    return true;
}

bool anim_clip(const AnimView& v, uint16_t index, AnimClip* out) {
    if (!v.data || !out || index >= v.clip_count) return false;
    const uint8_t* p = v.data + v.clips_offset + size_t(index) * 48;
    std::memcpy(out->name, p, 32);
    out->frame_count = read_u16(p + 32);
    out->loop = read_u32(p + 36) != 0;
    out->keys_offset = read_u32(p + 40);
    return true;
}

AnimQuat anim_rotation(const AnimView& v, const AnimClip& c, uint16_t frame, uint16_t part) {
    const uint8_t* p = key(v, c, frame) + 12 + size_t(part) * 8;
    AnimQuat q{float(signed_word(p)) / 32767.0f, float(signed_word(p + 2)) / 32767.0f,
               float(signed_word(p + 4)) / 32767.0f, float(signed_word(p + 6)) / 32767.0f};
    const float inv = 1.0f / std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    return {q.x*inv, q.y*inv, q.z*inv, q.w*inv};
}

void anim_root(const AnimView& v, const AnimClip& c, uint16_t frame, float out[3]) {
    const uint8_t* p = key(v, c, frame);
    for (size_t a = 0; a < 3; ++a) out[a] = read_f32(p + a * 4);
}

bool validate_anim(const void* data, size_t size, AnimView* out, const char** error) {
    if (out) *out = {};
    if (error) *error = nullptr;
    auto fail = [&](const char* reason) { if (error) *error = reason; return false; };
    if (!data || size < 32 || size > kAnimMaxFileBytes) return fail("ANM1 file size");
    const auto* p = static_cast<const uint8_t*>(data);
    if (std::memcmp(p, "ANM1", 4) || read_u16(p + 4) != 1) return fail("ANM1 magic/version");
    if (read_u16(p + 6) || read_u32(p + 28)) return fail("ANM1 flags/reserved");
    const uint16_t parts = read_u16(p + 8), clips = read_u16(p + 10);
    if (!parts || parts > kAnimMaxParts) return fail("ANM1 part_count must be 1..11");
    if (!clips || clips > kAnimMaxClips) return fail("ANM1 clip_count must be 1..16");
    const uint64_t clips_offset = 32 + uint64_t(parts) * 48;
    uint64_t cursor = clips_offset + uint64_t(clips) * 48;
    if (read_u32(p + 12) != 32 || read_u32(p + 16) != clips_offset ||
        read_u32(p + 20) != cursor || read_u32(p + 24) != size || cursor > size)
        return fail("ANM1 offsets/file size");
    for (uint16_t i = 0; i < parts; ++i) {
        const uint8_t* s = p + 32 + size_t(i) * 48;
        if (!valid_name(s)) return fail("ANM1 part name/padding");
        for (uint16_t j = 0; j < i; ++j)
            if (!std::memcmp(s, p + 32 + size_t(j) * 48, 32)) return fail("ANM1 duplicate part");
        const int32_t parent = signed_word(s + 32);
        if ((i == 0 && parent != -1) || (i != 0 && (parent < 0 || parent >= i)))
            return fail("ANM1 parent order/cycle/root");
        if (read_u16(s + 34)) return fail("ANM1 part reserved");
        for (size_t a = 0; a < 3; ++a) {
            const float value = read_f32(s + 36 + a * 4);
            if (!std::isfinite(value) || std::fabs(value) > 10.0f) return fail("ANM1 joint offset");
        }
    }
    for (uint16_t i = 0; i < clips; ++i) {
        const uint8_t* c = p + clips_offset + size_t(i) * 48;
        if (!valid_name(c)) return fail("ANM1 clip name/padding");
        for (uint16_t j = 0; j < i; ++j)
            if (!std::memcmp(c, p + clips_offset + size_t(j) * 48, 32)) return fail("ANM1 duplicate clip");
        const uint16_t frames = read_u16(c + 32);
        if (!frames || read_u16(c + 34) != 15) return fail("ANM1 frames/fps");
        if (read_u32(c + 36) > 1 || read_u32(c + 44)) return fail("ANM1 clip flags/reserved");
        const uint64_t end = cursor + uint64_t(frames) * (12 + 8 * parts);
        if (read_u32(c + 40) != cursor || (cursor & 15) || end > size)
            return fail("ANM1 clip keys range");
        for (uint16_t f = 0; f < frames; ++f) {
            const uint8_t* k = p + cursor + size_t(f) * (12 + 8 * parts);
            for (size_t a = 0; a < 3; ++a) {
                const float value = read_f32(k + a * 4);
                if (!std::isfinite(value) || std::fabs(value) > 10.0f) return fail("ANM1 root displacement");
            }
            for (uint16_t part = 0; part < parts; ++part) {
                float norm = 0.0f;
                for (size_t a = 0; a < 4; ++a) {
                    const int32_t q = signed_word(k + 12 + size_t(part) * 8 + a * 2);
                    if (q == -32768) return fail("ANM1 quaternion range");
                    const float v = float(q) / 32767.0f;
                    norm += v * v;
                }
                if (std::fabs(norm - 1.0f) > 0.0001f) return fail("ANM1 quaternion norm");
            }
        }
        cursor = (end + 15) & ~uint64_t(15);
        if (cursor > size) return fail("ANM1 clip padding range");
        for (uint64_t j = end; j < cursor; ++j) if (p[j]) return fail("ANM1 clip padding");
    }
    if (cursor != size) return fail("ANM1 trailing bytes");
    if (out) *out = {p, parts, clips, static_cast<uint32_t>(clips_offset)};
    return true;
}

}  // namespace render::format
