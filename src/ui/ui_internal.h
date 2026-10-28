#pragma once
// Внутреннее устройство UI (T030): то, что не зависит от render/platform и проверяется тестами.
#include <cstddef>
#include <cstdint>

#include "ui/ui_api.h"

namespace ui::internal {

// ---------------------------------------------------------------------------
// Шрифт FNT1 (docs/formats/font.md)

constexpr int kMaxGlyphs = 256;

struct Glyph {
    uint32_t codepoint;
    uint16_t x, y;
    uint8_t w, h;
    int8_t x_offset, y_offset;
    uint8_t advance;
};

struct FontData {
    Glyph glyphs[kMaxGlyphs];
    uint16_t glyph_count = 0;
    uint16_t fallback = 0;
    uint8_t line_height = 0;
    uint8_t baseline = 0;
    uint16_t atlas_width = 0;
    uint16_t atlas_height = 0;
    char atlas_name[12] = {};
    render::TextureId texture = render::kNoTexture;
};

// Разбор и проверка FNT1 по font.md («При загрузке проверить…»). false — ошибка, причина в
// *error (ASCII, для stderr). Глифов больше kMaxGlyphs — ошибка.
bool font_parse(const void* data, size_t size, FontData* out, const char** error);

// Глиф по кодпоинту (двоичный поиск) или fallback '?'.
const Glyph& font_glyph(const FontData& font, uint32_t codepoint);

// Ширина для раскладки (сумма advance, максимум по строкам).
int font_text_width(const FontData& font, const char* utf8);

// Декодирует один скаляр UTF-8, сдвигает *s. Неправильная последовательность — один байт и
// 0xFFFD (рисуется fallback). На NUL не вызывать.
uint32_t utf8_next(const char** s);

// ---------------------------------------------------------------------------
// Навигация фокуса крестовиной

enum class Dir : uint8_t { kNone, kUp, kDown, kLeft, kRight };

// Индекс прямоугольника, ближайшего к rects[from] в направлении dir; from — если некуда.
// Кандидат должен лежать по ту сторону центра; перекрытие по поперечной оси предпочтительно.
int nav_pick(const Rect* rects, int count, int from, Dir dir);

}  // namespace ui::internal
