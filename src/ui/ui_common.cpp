// Шрифты FNT1 (docs/formats/font.md), UTF-8, деньги, навигация фокуса — без render/platform.
#include <cstring>

#include "ui/ui_internal.h"

namespace ui::internal {
namespace {

constexpr size_t kHeaderSize = 32;
constexpr size_t kGlyphSize = 16;

uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

bool pow2_in_range(uint16_t v) { return v >= 8 && v <= 256 && (v & (v - 1)) == 0; }

bool fail(const char** error, const char* text) {
    if (error) *error = text;
    return false;
}

}  // namespace

bool font_parse(const void* data, size_t size, FontData* out, const char** error) {
    const auto* p = static_cast<const uint8_t*>(data);
    if (!p || size < kHeaderSize) return fail(error, "file too small");
    if (std::memcmp(p, "FNT1", 4) != 0) return fail(error, "bad magic");
    if (rd16(p + 4) != 1) return fail(error, "bad version");
    if (rd16(p + 6) & ~1u) return fail(error, "unknown flags");
    const uint16_t count = rd16(p + 8);
    if (count == 0 || count > kMaxGlyphs) return fail(error, "bad glyph count");
    if (size != kHeaderSize + kGlyphSize * count) return fail(error, "size mismatch");
    const uint16_t fallback = rd16(p + 10);
    if (fallback >= count) return fail(error, "bad fallback index");

    FontData& f = *out;
    f.glyph_count = count;
    f.fallback = fallback;
    f.line_height = p[12];
    f.baseline = p[13];
    f.atlas_width = rd16(p + 16);
    f.atlas_height = rd16(p + 18);
    if (f.line_height == 0 || f.baseline > f.line_height) return fail(error, "bad line metrics");
    if (!pow2_in_range(f.atlas_width) || !pow2_in_range(f.atlas_height))
        return fail(error, "bad atlas size");

    // atlas_name: 1–11 символов [A-Za-z0-9_], затем только нули.
    const char* name = reinterpret_cast<const char*>(p + 20);
    int len = 0;
    while (len < 12 && name[len] != 0) {
        const char c = name[len];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return fail(error, "bad atlas name");
        ++len;
    }
    if (len == 0 || len > 11) return fail(error, "bad atlas name");
    for (int i = len; i < 12; ++i)
        if (name[i] != 0) return fail(error, "bad atlas name tail");
    std::memcpy(f.atlas_name, name, 12);

    uint32_t prev = 0;
    for (uint16_t i = 0; i < count; ++i) {
        const uint8_t* g = p + kHeaderSize + kGlyphSize * i;
        Glyph& out_g = f.glyphs[i];
        out_g.codepoint = rd32(g);
        out_g.x = rd16(g + 4);
        out_g.y = rd16(g + 6);
        out_g.w = g[8];
        out_g.h = g[9];
        out_g.x_offset = static_cast<int8_t>(g[10]);
        out_g.y_offset = static_cast<int8_t>(g[11]);
        out_g.advance = g[12];
        if (g[13] || g[14] || g[15]) return fail(error, "nonzero reserved");
        const uint32_t cp = out_g.codepoint;
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return fail(error, "bad codepoint");
        if (i > 0 && cp <= prev) return fail(error, "codepoints not sorted");
        prev = cp;
        if ((out_g.w == 0) != (out_g.h == 0)) return fail(error, "bad glyph size");
        if (out_g.w != 0 && (out_g.x + out_g.w > f.atlas_width || out_g.y + out_g.h > f.atlas_height))
            return fail(error, "glyph outside atlas");
        if (out_g.advance == 0) return fail(error, "zero advance");
    }
    if (f.glyphs[fallback].codepoint != '?') return fail(error, "fallback is not '?'");
    return true;
}

const Glyph& font_glyph(const FontData& font, uint32_t codepoint) {
    int lo = 0, hi = font.glyph_count - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        const uint32_t c = font.glyphs[mid].codepoint;
        if (c == codepoint) return font.glyphs[mid];
        if (c < codepoint) lo = mid + 1;
        else hi = mid - 1;
    }
    return font.glyphs[font.fallback];
}

uint32_t utf8_next(const char** s) {
    const auto* p = reinterpret_cast<const uint8_t*>(*s);
    const uint8_t c = p[0];
    int len = 0;
    uint32_t cp = 0, min = 0;
    if (c < 0x80) {
        *s += 1;
        return c;
    } else if ((c & 0xE0) == 0xC0) {
        len = 2, cp = c & 0x1F, min = 0x80;
    } else if ((c & 0xF0) == 0xE0) {
        len = 3, cp = c & 0x0F, min = 0x800;
    } else if ((c & 0xF8) == 0xF0) {
        len = 4, cp = c & 0x07, min = 0x10000;
    } else {
        *s += 1;
        return 0xFFFD;
    }
    for (int i = 1; i < len; ++i) {
        if ((p[i] & 0xC0) != 0x80) {  // в т.ч. NUL — не читаем за концом строки
            *s += 1;
            return 0xFFFD;
        }
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        *s += 1;
        return 0xFFFD;
    }
    *s += len;
    return cp;
}

int font_text_width(const FontData& font, const char* utf8) {
    if (!utf8 || font.glyph_count == 0) return 0;
    int best = 0, pen = 0;
    while (*utf8) {
        const uint32_t cp = utf8_next(&utf8);
        if (cp == '\n') {
            if (pen > best) best = pen;
            pen = 0;
            continue;
        }
        pen += font_glyph(font, cp).advance;
    }
    return pen > best ? pen : best;
}

int nav_pick(const Rect* rects, int count, int from, Dir dir) {
    if (from < 0 || from >= count || dir == Dir::kNone) return from;
    const Rect& a = rects[from];
    const bool vertical = dir == Dir::kUp || dir == Dir::kDown;
    const int sign = (dir == Dir::kDown || dir == Dir::kRight) ? 1 : -1;
    // Центры удвоены — без дробей.
    const int acx = 2 * a.x + a.w, acy = 2 * a.y + a.h;
    int best = from;
    long best_score = 0;
    for (int i = 0; i < count; ++i) {
        if (i == from) continue;
        const Rect& b = rects[i];
        const int bcx = 2 * b.x + b.w, bcy = 2 * b.y + b.h;
        const int main_d = sign * (vertical ? bcy - acy : bcx - acx);
        if (main_d <= 0) continue;
        // Поперечное расстояние между отрезками: 0 при перекрытии.
        int lo_a, hi_a, lo_b, hi_b;
        if (vertical) lo_a = a.x, hi_a = a.x + a.w, lo_b = b.x, hi_b = b.x + b.w;
        else lo_a = a.y, hi_a = a.y + a.h, lo_b = b.y, hi_b = b.y + b.h;
        int gap = 0;
        if (hi_b <= lo_a) gap = lo_a - hi_b + 1;
        else if (hi_a <= lo_b) gap = lo_b - hi_a + 1;
        const int cross_center = vertical ? (bcx > acx ? bcx - acx : acx - bcx)
                                          : (bcy > acy ? bcy - acy : acy - bcy);
        // Перекрывающиеся — всегда раньше неперекрывающихся; дальше — ближе по главной оси.
        const long score = (gap > 0 ? 1000000L + 8L * gap : 0) + 4L * main_d + cross_center;
        if (best == from || score < best_score) {
            best = i;
            best_score = score;
        }
    }
    return best;
}

}  // namespace ui::internal

namespace ui {

int format_money(int32_t rubles, char* buf, int cap) {
    if (!buf || cap <= 0) return 0;
    long long v = rubles;
    const bool negative = v < 0;
    if (negative) v = -v;
    char digits[16];
    int n = 0;
    do {
        digits[n++] = static_cast<char>('0' + v % 10);
        v /= 10;
    } while (v > 0);
    char tmp[40];
    int len = 0;
    if (negative) tmp[len++] = '-';
    for (int i = n - 1; i >= 0; --i) {
        tmp[len++] = digits[i];
        if (i > 0 && i % 3 == 0) tmp[len++] = ' ';
    }
    static const char kRuble[] = " \xE2\x82\xBD";  // " ₽"
    std::memcpy(tmp + len, kRuble, sizeof(kRuble) - 1);
    len += static_cast<int>(sizeof(kRuble) - 1);
    tmp[len] = 0;
    // Обрезка по cap — по целым UTF-8 символам.
    int out = len < cap - 1 ? len : cap - 1;
    while (out > 0 && (static_cast<uint8_t>(tmp[out]) & 0xC0) == 0x80) --out;
    std::memcpy(buf, tmp, static_cast<size_t>(out));
    buf[out] = 0;
    return out;
}

}  // namespace ui
