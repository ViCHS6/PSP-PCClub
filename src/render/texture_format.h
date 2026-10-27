#pragma once
#include <cstddef>
#include <cstdint>

namespace render::format {

struct TextureHeader {
    uint8_t format, flags;
    uint16_t width, height, stride, frame_count, clut_count;
    uint32_t clut_offset, pixels_offset, frame_bytes;
};

struct TextureView {
    const uint8_t* data = nullptr;
    TextureHeader h{};
    uint32_t stride_bytes = 0;
};

bool validate_texture(const void* data, size_t size, TextureView* out, const char** error);
// Выход — плотные RGBA-байты, без padding и без повторного переворота V.
bool decode_texture_frame(const TextureView& texture, uint16_t frame, void* rgba, size_t capacity);
uint64_t texture_byte_offset(uint32_t x_byte, uint32_t y, uint32_t stride_bytes, bool swizzled);

}  // namespace render::format
