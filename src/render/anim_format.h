#pragma once
#include <cstddef>
#include <cstdint>

namespace render::format {

constexpr uint16_t kAnimMaxParts = 11;
constexpr uint16_t kAnimMaxClips = 16;
constexpr size_t kAnimMaxFileBytes = 64 * 1024;

// ANM1: структуры описывают ABI файла; чтение выполняется побайтно как LE.
struct AnimHeaderDisk {
    char magic[4];
    uint16_t version, flags, part_count, clip_count;
    uint32_t parts_offset, clips_offset, keys_offset, file_bytes, reserved;
};
struct AnimPartDisk {
    char name[32];
    int16_t parent;
    uint16_t reserved;
    float offset[3];
};
struct AnimClipDisk {
    char name[32];
    uint16_t frame_count, fps;
    uint32_t flags, keys_offset, reserved;
};
static_assert(sizeof(float) == 4);
static_assert(sizeof(AnimHeaderDisk) == 32);
static_assert(sizeof(AnimPartDisk) == 48);
static_assert(sizeof(AnimClipDisk) == 48);
static_assert(offsetof(AnimHeaderDisk, parts_offset) == 12);
static_assert(offsetof(AnimHeaderDisk, file_bytes) == 24);
static_assert(offsetof(AnimPartDisk, parent) == 32);
static_assert(offsetof(AnimPartDisk, offset) == 36);
static_assert(offsetof(AnimClipDisk, frame_count) == 32);
static_assert(offsetof(AnimClipDisk, keys_offset) == 40);

struct AnimView {
    const uint8_t* data;
    uint16_t part_count, clip_count;
    uint32_t clips_offset;
};
struct AnimPart {
    char name[32];
    int16_t parent;
    float offset[3];
};
struct AnimClip {
    char name[32];
    uint16_t frame_count;
    bool loop;
    uint32_t keys_offset;
};
struct AnimQuat { float x, y, z, w; };

// Не выделяет память. На отказе out обнуляется; error — статическая строка.
bool validate_anim(const void* data, size_t size, AnimView* out, const char** error);
bool anim_part(const AnimView& view, uint16_t index, AnimPart* out);
bool anim_clip(const AnimView& view, uint16_t index, AnimClip* out);
// Только для полностью проверенного view, индексы обязаны быть в пределах клипа/скелета.
AnimQuat anim_rotation(const AnimView& view, const AnimClip& clip, uint16_t frame, uint16_t part);
void anim_root(const AnimView& view, const AnimClip& clip, uint16_t frame, float out[3]);

}  // namespace render::format
