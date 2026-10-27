#include "render/texture_format.h"
#include "render/mesh_format.h"

#include <cstring>

namespace render::format {
namespace {

constexpr uint32_t kBits[] = {16, 16, 16, 32, 4, 8};

uint8_t tex_byte(const TextureView& t, uint16_t frame, uint32_t x, uint32_t y) {
    return t.data[uint64_t(t.h.pixels_offset) + uint64_t(frame) * t.h.frame_bytes +
                  texture_byte_offset(x, y, t.stride_bytes, (t.h.flags & 1) != 0)];
}

uint32_t texel(const TextureView& t, uint16_t frame, uint32_t x, uint32_t y, uint32_t* index) {
    const auto& h = t.h;
    auto byte = [&](uint32_t offset) { return tex_byte(t, frame, offset, y); };
    *index = 0;
    if (h.format >= 4) {
        *index = h.format == 4 ? (byte(x / 2) >> (4 * (x % 2))) & 15 : byte(x);
        return read_u32(t.data + h.clut_offset + *index * 4);
    }
    if (h.format == 3) {
        return uint32_t(byte(x * 4)) | (uint32_t(byte(x * 4 + 1)) << 8) |
               (uint32_t(byte(x * 4 + 2)) << 16) | (uint32_t(byte(x * 4 + 3)) << 24);
    }
    const uint32_t word = uint32_t(byte(x * 2)) | (uint32_t(byte(x * 2 + 1)) << 8);
    auto expand5 = [](uint32_t v) { return (v << 3) | (v >> 2); };
    auto expand6 = [](uint32_t v) { return (v << 2) | (v >> 4); };
    uint32_t r, g, b, a;
    if (h.format == 0) {
        r = expand5(word & 31); g = expand6((word >> 5) & 63); b = expand5((word >> 11) & 31); a = 255;
    } else if (h.format == 1) {
        r = expand5(word & 31); g = expand5((word >> 5) & 31); b = expand5((word >> 10) & 31);
        a = (word >> 15) * 255;
    } else {
        r = (word & 15) * 17; g = ((word >> 4) & 15) * 17; b = ((word >> 8) & 15) * 17;
        a = (word >> 12) * 17;
    }
    return r | (g << 8) | (b << 16) | (a << 24);
}

}  // namespace

uint64_t texture_byte_offset(uint32_t x_byte, uint32_t y, uint32_t stride_bytes, bool swizzled) {
    if (!swizzled) return uint64_t(y) * stride_bytes + x_byte;
    const uint64_t block = x_byte / 16 + uint64_t(y / 8) * (stride_bytes / 16);
    return block * 128 + (y % 8) * 16 + x_byte % 16;
}

bool validate_texture(const void* data, size_t size, TextureView* out, const char** error) {
    if (out) *out = {};
    if (error) *error = nullptr;
    auto fail = [&](const char* reason) { if (error) *error = reason; return false; };
    if (!data || size < 32) return fail("truncated TEX1 header");
    const auto* p = static_cast<const uint8_t*>(data);
    if (std::memcmp(p, "TEX1", 4) || read_u16(p + 4) != 1) return fail("TEX1 magic/version");
    TextureView t;
    t.data = p;
    auto& h = t.h;
    h.format = p[6]; h.flags = p[7]; h.width = read_u16(p + 8); h.height = read_u16(p + 10);
    h.stride = read_u16(p + 12); h.frame_count = read_u16(p + 14); h.clut_count = read_u16(p + 16);
    h.clut_offset = read_u32(p + 20); h.pixels_offset = read_u32(p + 24); h.frame_bytes = read_u32(p + 28);
    auto pow2 = [](uint16_t v) { return v >= 8 && v <= 512 && (v & (v - 1)) == 0; };
    if (h.format > 5 || (h.flags & ~3) || read_u16(p + 18) || !h.frame_count ||
        !pow2(h.width) || !pow2(h.height)) return fail("TEX1 format/flags/dimensions/count");
    const uint64_t active_bytes = (uint64_t(h.width) * kBits[h.format] + 7) / 8;
    const uint64_t stride_bytes = (active_bytes + 15) & ~uint64_t(15);
    const uint64_t stored_height = (uint64_t(h.height) + 7) & ~uint64_t(7);
    if (h.stride != stride_bytes * 8 / kBits[h.format] || h.frame_bytes != stride_bytes * stored_height)
        return fail("TEX1 stride/frame size");
    t.stride_bytes = static_cast<uint32_t>(stride_bytes);
    const uint64_t clut_count = h.format == 4 ? 16 : h.format == 5 ? 256 : 0;
    const uint64_t pixels = 32 + clut_count * 4;
    const uint64_t end = pixels + uint64_t(h.frame_count) * h.frame_bytes;
    if (h.clut_count != clut_count || h.clut_offset != (clut_count ? 32u : 0u) ||
        h.pixels_offset != pixels || (h.pixels_offset & 15) || end != size)
        return fail("TEX1 CLUT/offsets/file size");
    for (uint64_t i = 0; i < clut_count; ++i) {
        const uint32_t c = read_u32(p + 32 + i * 4);
        const uint32_t a = c >> 24;
        if ((a != 0 && a != 255) || (a == 0 && c != 0)) return fail("TEX1 CLUT alpha");
    }
    if (clut_count && (h.flags & 2) && read_u32(p + 32)) return fail("TEX1 transparent CLUT[0]");
    bool transparent = false;
    for (uint32_t f = 0; f < h.frame_count; ++f) {
        for (uint32_t y = 0; y < stored_height; ++y) {
            for (uint32_t x = y < h.height ? static_cast<uint32_t>(active_bytes) : 0; x < stride_bytes; ++x)
                if (tex_byte(t, static_cast<uint16_t>(f), x, y)) return fail("TEX1 pixel padding");
        }
        for (uint32_t y = 0; y < h.height; ++y) {
            for (uint32_t x = 0; x < h.width; ++x) {
                uint32_t index;
                const uint32_t a = texel(t, static_cast<uint16_t>(f), x, y, &index) >> 24;
                if (a != 0 && a != 255) return fail("TEX1 pixel alpha");
                if (a == 0) {
                    if (clut_count && index != 0) return fail("TEX1 transparent index must be zero");
                    transparent = true;
                }
            }
        }
    }
    if (transparent != ((h.flags & 2) != 0)) return fail("TEX1 transparency flag mismatch");
    if (out) *out = t;
    return true;
}

bool decode_texture_frame(const TextureView& texture, uint16_t frame, void* rgba, size_t capacity) {
    if (!texture.data || !rgba || frame >= texture.h.frame_count ||
        uint64_t(texture.h.width) * texture.h.height * 4 > capacity) return false;
    auto* dst = static_cast<uint8_t*>(rgba);
    for (uint32_t y = 0; y < texture.h.height; ++y) {
        for (uint32_t x = 0; x < texture.h.width; ++x) {
            uint32_t index;
            const uint32_t c = texel(texture, frame, x, y, &index);
            const uint64_t offset = (uint64_t(y) * texture.h.width + x) * 4;
            for (uint32_t b = 0; b < 4; ++b) dst[offset + b] = static_cast<uint8_t>(c >> (b * 8));
        }
    }
    return true;
}

}  // namespace render::format
