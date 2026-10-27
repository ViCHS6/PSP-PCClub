// Общая часть рендера: арены, статистика, отладочный текст.
#include <cstdlib>
#include <cstring>

#include "render/debug_font_data.h"
#include "render/render_internal.h"

namespace render {
namespace {

namespace font = debug_font_data;

constexpr int kAtlasSize = 128;
constexpr int kAtlasColumns = kAtlasSize / font::kGlyphWidth;
static_assert(font::kGlyphWidth == kDebugGlyphWidth && font::kGlyphHeight == kDebugGlyphHeight,
              "размер глифа в данных не совпадает с контрактом");
static_assert((font::kGlyphCount + kAtlasColumns - 1) / kAtlasColumns * font::kGlyphHeight <= kAtlasSize,
              "глифы не помещаются в атлас");

FrameStats g_stats;
TextureId g_font_texture = kNoTexture;

int glyph_index(uint32_t codepoint) {
    int lo = 0, hi = font::kGlyphCount - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (font::kCodepoints[mid] == codepoint) return mid;
        if (font::kCodepoints[mid] < codepoint) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}

// Следующий символ UTF-8; на битой последовательности — U+FFFD и шаг на байт.
uint32_t next_codepoint(const char*& s) {
    const auto* p = reinterpret_cast<const uint8_t*>(s);
    uint32_t cp = 0xFFFD;
    int len = 1;
    if (p[0] < 0x80) {
        cp = p[0];
    } else if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        cp = ((p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu);
        len = 2;
    } else if ((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        cp = ((p[0] & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
        len = 3;
    } else if ((p[0] & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 &&
               (p[3] & 0xC0) == 0x80) {
        cp = ((p[0] & 0x07u) << 18) | ((p[1] & 0x3Fu) << 12) | ((p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu);
        len = 4;
    }
    s += len;
    return cp;
}

}  // namespace

namespace internal {

void* Arena::alloc(size_t bytes, size_t align) {
    if (!base || !align || (align & (align - 1)) || used > size || align - 1 > size - used)
        return nullptr;
    const size_t start = (used + align - 1) & ~(align - 1);
    if (start > size || bytes > size - start) return nullptr;
    used = start + bytes;
    return base + start;
}

FrameStats& mutable_stats() { return g_stats; }

bool debug_font_init() {
    // Временный буфер только на время загрузки.
    auto* pixels = static_cast<uint32_t*>(std::calloc(kAtlasSize * kAtlasSize, sizeof(uint32_t)));
    if (!pixels) return false;
    for (int i = 0; i < font::kGlyphCount; ++i) {
        const int ox = (i % kAtlasColumns) * font::kGlyphWidth;
        const int oy = (i / kAtlasColumns) * font::kGlyphHeight;
        for (int row = 0; row < font::kGlyphHeight; ++row) {
            const uint8_t bits = font::kRows[i][row];
            for (int col = 0; col < font::kGlyphWidth; ++col) {
                if (bits & (0x80u >> col)) pixels[(oy + row) * kAtlasSize + ox + col] = kWhite;
            }
        }
    }
    g_font_texture = texture_create(kAtlasSize, kAtlasSize, TextureFormat::kRgba8888, pixels);
    std::free(pixels);
    return g_font_texture != kNoTexture;
}

}  // namespace internal

const FrameStats& stats() { return g_stats; }

int debug_text(int x, int y, const char* utf8, Color color) {
    constexpr int kBatch = 64;
    Sprite batch[kBatch];
    int count = 0;
    int pen_x = x, pen_y = y, max_width = 0;
    const int fallback = glyph_index('?');

    while (*utf8) {
        const uint32_t cp = next_codepoint(utf8);
        if (cp == '\n') {
            pen_x = x;
            pen_y += font::kGlyphHeight;
            continue;
        }
        if (cp != ' ') {
            int index = glyph_index(cp);
            if (index < 0) index = fallback;
            Sprite& s = batch[count++];
            s.x = static_cast<int16_t>(pen_x);
            s.y = static_cast<int16_t>(pen_y);
            s.w = font::kGlyphWidth;
            s.h = font::kGlyphHeight;
            s.u = static_cast<int16_t>((index % kAtlasColumns) * font::kGlyphWidth);
            s.v = static_cast<int16_t>((index / kAtlasColumns) * font::kGlyphHeight);
            s.uw = font::kGlyphWidth;
            s.vh = font::kGlyphHeight;
            s.color = color;
            if (count == kBatch) {
                draw_sprites(g_font_texture, batch, count);
                count = 0;
            }
        }
        pen_x += font::kGlyphWidth;
        if (pen_x - x > max_width) max_width = pen_x - x;
    }
    if (count > 0) draw_sprites(g_font_texture, batch, count);
    return max_width;
}

}  // namespace render
