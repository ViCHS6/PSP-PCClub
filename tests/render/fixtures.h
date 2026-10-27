#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace render_test {

using Bytes = std::vector<uint8_t>;

inline void u16(Bytes& b, size_t at, uint16_t v) {
    b[at] = static_cast<uint8_t>(v);
    b[at + 1] = static_cast<uint8_t>(v >> 8);
}

inline void u32(Bytes& b, size_t at, uint32_t v) {
    for (size_t i = 0; i < 4; ++i) b[at + i] = static_cast<uint8_t>(v >> (i * 8));
}

inline void f32(Bytes& b, size_t at, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    u32(b, at, bits);
}

inline Bytes quad() {
    Bytes b(220, 0);
    std::memcpy(b.data(), "MSH1", 4);
    u16(b, 4, 1); u16(b, 8, 4); u16(b, 10, 1);
    u32(b, 12, 6); u32(b, 16, 144); u32(b, 20, 208); u32(b, 24, 96);
    f32(b, 32, 1.0f); f32(b, 36, 1.0f);
    for (size_t a = 0; a < 3; ++a) f32(b, 44 + a * 4, 1.0f);
    f32(b, 56, -1.0f);
    f32(b, 64, 1.5000228882f); f32(b, 68, 0.5000076294f);
    f32(b, 84, 2.0f); f32(b, 88, 2.0f);
    u32(b, 100, 6);
    const uint16_t uv[4][2] = {{0, 65535}, {65535, 65535}, {65535, 0}, {0, 0}};
    const uint16_t pos[4][2] = {{32768, 32768}, {32767, 32768}, {32767, 32767}, {32768, 32767}};
    const uint16_t indices[] = {0, 1, 2, 0, 2, 3};
    for (size_t i = 0; i < 4; ++i) {
        const size_t at = 144 + i * 16;
        u16(b, at, uv[i][0]); u16(b, at + 2, uv[i][1]);
        u32(b, at + 4, 0xFF404040);
        u16(b, at + 8, pos[i][0]); u16(b, at + 10, pos[i][1]);
    }
    for (size_t i = 0; i < 6; ++i) u16(b, 208 + i * 2, indices[i]);
    return b;
}

inline Bytes split_quad() {
    auto b = quad();
    b.insert(b.begin() + 144, 48, 0);
    u16(b, 10, 2); u32(b, 16, 192); u32(b, 20, 256);
    u32(b, 100, 3); u32(b, 144, 3); u32(b, 148, 3);
    std::memcpy(b.data() + 104, "first", 5);
    std::memcpy(b.data() + 152, "second", 6);
    return b;
}

inline size_t offset(size_t x, size_t y, size_t stride, bool swizzle) {
    return swizzle ? (x / 16 + (y / 8) * (stride / 16)) * 128 + (y % 8) * 16 + x % 16 :
                    y * stride + x;
}

inline Bytes texture(uint8_t format = 3, uint16_t width = 8, uint16_t height = 16,
                     uint16_t frames = 1, bool swizzle = true) {
    const size_t bits[] = {16, 16, 16, 32, 4, 8};
    const size_t stride = ((width * bits[format] + 7) / 8 + 15) & ~size_t(15);
    const uint16_t clut = format == 4 ? 16 : format == 5 ? 256 : 0;
    const size_t pixels = 32 + clut * 4;
    const size_t frame_bytes = stride * height;
    Bytes b(pixels + frames * frame_bytes, 0);
    std::memcpy(b.data(), "TEX1", 4);
    u16(b, 4, 1); b[6] = format; b[7] = swizzle ? 1 : 0;
    u16(b, 8, width); u16(b, 10, height); u16(b, 12, static_cast<uint16_t>(stride * 8 / bits[format]));
    u16(b, 14, frames); u16(b, 16, clut);
    u32(b, 20, clut ? 32 : 0); u32(b, 24, static_cast<uint32_t>(pixels));
    u32(b, 28, static_cast<uint32_t>(frame_bytes));
    if (clut) u32(b, 36, 0xFF0000FF);
    for (size_t f = 0; f < frames; ++f) {
        const size_t base = pixels + f * frame_bytes;
        for (size_t y = 0; y < height; ++y) {
            const size_t active = width * bits[format] / 8;
            for (size_t x = 0; x < active; ++x) {
                uint8_t value;
                if (format == 3) {
                    const uint8_t rgba[] = {static_cast<uint8_t>(x / 4), static_cast<uint8_t>(y),
                                            static_cast<uint8_t>(f), 255};
                    value = rgba[x % 4];
                } else if (format >= 4) {
                    value = format == 4 ? 0x11 : 1;
                } else {
                    const uint16_t red[] = {0x001F, 0x801F, 0xF00F};
                    value = static_cast<uint8_t>(red[format] >> ((x % 2) * 8));
                }
                b[base + offset(x, y, stride, swizzle)] = value;
            }
        }
    }
    return b;
}

}  // namespace render_test
