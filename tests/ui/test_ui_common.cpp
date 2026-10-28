#include <doctest/doctest.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "ui/ui_internal.h"

using namespace ui::internal;

namespace {

std::vector<uint8_t> read_font(const char* name) {
    char path[512];
    std::snprintf(path, sizeof(path), "%s/%s.fnt", PCCLUB_FONTS_DIR, name);
    std::vector<uint8_t> out;
    if (FILE* f = std::fopen(path, "rb")) {
        uint8_t buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.insert(out.end(), buf, buf + n);
        std::fclose(f);
    }
    return out;
}

void put16(std::vector<uint8_t>& b, size_t at, uint16_t v) {
    b[at] = static_cast<uint8_t>(v);
    b[at + 1] = static_cast<uint8_t>(v >> 8);
}

void put32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<uint8_t>(v >> (8 * i));
}

// Минимальный корректный FNT1: пробел, '?', 'A'.
std::vector<uint8_t> tiny_font() {
    std::vector<uint8_t> b(32 + 16 * 3, 0);
    std::memcpy(b.data(), "FNT1", 4);
    put16(b, 4, 1);
    put16(b, 8, 3);
    put16(b, 10, 1);  // fallback — '?'
    b[12] = 12;
    b[13] = 10;
    b[14] = 6;
    b[15] = 3;
    put16(b, 16, 16);
    put16(b, 18, 16);
    std::memcpy(b.data() + 20, "tiny", 4);
    const uint32_t cps[3] = {' ', '?', 'A'};
    for (int i = 0; i < 3; ++i) {
        const size_t g = 32 + 16 * i;
        put32(b, g, cps[i]);
        if (cps[i] != ' ') {
            put16(b, g + 4, static_cast<uint16_t>(1 + 6 * (i - 1)));
            put16(b, g + 6, 1);
            b[g + 8] = 5;
            b[g + 9] = 12;
        }
        b[g + 12] = cps[i] == ' ' ? 3 : 6;
    }
    return b;
}

bool parses(const std::vector<uint8_t>& b) {
    static FontData f;
    const char* error = nullptr;
    const bool ok = font_parse(b.data(), b.size(), &f, &error);
    if (!ok) CHECK(error != nullptr);
    return ok;
}

}  // namespace

TEST_CASE("ui: настоящий font_ui12 разбирается, ширины по font.md") {
    const std::vector<uint8_t> b = read_font("font_ui12");
    REQUIRE(!b.empty());
    static FontData f;
    const char* error = nullptr;
    REQUIRE(font_parse(b.data(), b.size(), &f, &error));
    CHECK(f.glyph_count == 185);
    CHECK(f.line_height == 12);
    CHECK(f.baseline == 10);
    CHECK(std::strcmp(f.atlas_name, "font_ui12") == 0);
    // Пример из font.md: «1 500 ₽» = 4×6 + 2×3 + 6 = 36.
    CHECK(font_text_width(f, "1 500 \xE2\x82\xBD") == 36);
    CHECK(font_text_width(f, "9999") == 24);
    CHECK(font_text_width(f, "1111") == 24);
    CHECK(font_text_width(f, "") == 0);
    CHECK(font_text_width(f, nullptr) == 0);
    // Многострочный — максимум строк.
    CHECK(font_text_width(f, "99\n9999\n9") == 24);
    // Неизвестный символ — ширина '?'.
    CHECK(font_text_width(f, "\xE2\x98\x83") == font_glyph(f, '?').advance);
    CHECK(font_glyph(f, 0x0401).codepoint == 0x0401);  // Ё
}

TEST_CASE("ui: все четыре шрифта UI разбираются") {
    for (const char* name : {"font_ui12", "font_mono12", "font_ui14", "font_ui14b"}) {
        CAPTURE(name);
        const std::vector<uint8_t> b = read_font(name);
        REQUIRE(!b.empty());
        CHECK(parses(b));
    }
}

TEST_CASE("ui: font_parse отвергает испорченный FNT1") {
    REQUIRE(parses(tiny_font()));

    std::vector<uint8_t> b = tiny_font();
    b[0] = 'X';
    CHECK_FALSE(parses(b));

    b = tiny_font();
    put16(b, 4, 2);  // версия
    CHECK_FALSE(parses(b));

    b = tiny_font();
    put16(b, 6, 2);  // неизвестный флаг
    CHECK_FALSE(parses(b));

    b = tiny_font();
    b.push_back(0);  // хвост
    CHECK_FALSE(parses(b));

    b = tiny_font();
    put16(b, 10, 2);  // fallback на 'A'
    CHECK_FALSE(parses(b));

    b = tiny_font();
    put16(b, 10, 3);  // fallback вне таблицы
    CHECK_FALSE(parses(b));

    b = tiny_font();
    put32(b, 32 + 16 * 2, '0');  // 'A' → '0' < '?': нарушена сортировка
    CHECK_FALSE(parses(b));

    b = tiny_font();
    b[32 + 16 * 2 + 13] = 1;  // reserved
    CHECK_FALSE(parses(b));

    b = tiny_font();
    put16(b, 32 + 16 * 2 + 4, 12);  // 'A' вылезает за атлас 16×16
    CHECK_FALSE(parses(b));

    b = tiny_font();
    put16(b, 16, 24);  // ширина атласа не степень двойки
    CHECK_FALSE(parses(b));

    b = tiny_font();
    b[20] = '-';  // недопустимый символ имени
    CHECK_FALSE(parses(b));

    b = tiny_font();
    b[31] = 'x';  // имя без NUL
    CHECK_FALSE(parses(b));

    b = tiny_font();
    put32(b, 32 + 16 * 2, 0xD800);  // суррогат
    CHECK_FALSE(parses(b));

    b = tiny_font();
    b[32 + 16 * 2 + 12] = 0;  // нулевой шаг
    CHECK_FALSE(parses(b));

    CHECK_FALSE(parses(std::vector<uint8_t>(16, 0)));
}

TEST_CASE("ui: utf8_next") {
    const char* s = "A\xD0\x81\xE2\x82\xBD";  // A Ё ₽
    CHECK(utf8_next(&s) == 'A');
    CHECK(utf8_next(&s) == 0x0401);
    CHECK(utf8_next(&s) == 0x20BD);
    CHECK(*s == 0);

    const char* bad = "\xFF" "B";
    CHECK(utf8_next(&bad) == 0xFFFD);
    CHECK(*bad == 'B');

    const char* cut = "\xD0";  // оборвано на NUL: не читаем дальше конца
    CHECK(utf8_next(&cut) == 0xFFFD);
    CHECK(*cut == 0);

    const char* overlong = "\xC0\xAF";
    CHECK(utf8_next(&overlong) == 0xFFFD);
    CHECK(*overlong == '\xAF');

    const char* surrogate = "\xED\xA0\x80";
    CHECK(utf8_next(&surrogate) == 0xFFFD);
}

TEST_CASE("ui: format_money") {
    char buf[32];
    const int len = ui::format_money(0, buf, sizeof(buf));
    CHECK(len == static_cast<int>(std::strlen(buf)));
    CHECK(std::strcmp(buf, "0 \xE2\x82\xBD") == 0);
    ui::format_money(1500, buf, sizeof(buf));
    CHECK(std::strcmp(buf, "1 500 \xE2\x82\xBD") == 0);
    ui::format_money(999, buf, sizeof(buf));
    CHECK(std::strcmp(buf, "999 \xE2\x82\xBD") == 0);
    ui::format_money(-200, buf, sizeof(buf));
    CHECK(std::strcmp(buf, "-200 \xE2\x82\xBD") == 0);
    ui::format_money(1234567, buf, sizeof(buf));
    CHECK(std::strcmp(buf, "1 234 567 \xE2\x82\xBD") == 0);
    ui::format_money(std::numeric_limits<int32_t>::min(), buf, sizeof(buf));
    CHECK(std::strcmp(buf, "-2 147 483 648 \xE2\x82\xBD") == 0);

    // Обрезка не рвёт UTF-8: "5 ₽" = 5 байт, в 4 байта (3 + NUL) влезает только "5 ".
    char small[4];
    CHECK(ui::format_money(5, small, sizeof(small)) == 2);
    CHECK(std::strcmp(small, "5 ") == 0);
    CHECK(ui::format_money(5, nullptr, 0) == 0);
}

TEST_CASE("ui: навигация крестовиной") {
    // Сетка 2×2 и широкая кнопка снизу.
    //   [0] [1]
    //   [2] [3]
    //   [   4   ]
    const ui::Rect r[] = {{0, 0, 40, 16}, {50, 0, 40, 16}, {0, 20, 40, 16}, {50, 20, 40, 16}, {0, 40, 90, 16}};
    CHECK(nav_pick(r, 5, 0, Dir::kRight) == 1);
    CHECK(nav_pick(r, 5, 0, Dir::kDown) == 2);
    CHECK(nav_pick(r, 5, 0, Dir::kLeft) == 0);   // некуда — остаёмся
    CHECK(nav_pick(r, 5, 0, Dir::kUp) == 0);
    CHECK(nav_pick(r, 5, 1, Dir::kDown) == 3);
    CHECK(nav_pick(r, 5, 3, Dir::kLeft) == 2);
    CHECK(nav_pick(r, 5, 3, Dir::kDown) == 4);
    CHECK(nav_pick(r, 5, 4, Dir::kUp) == 2);     // 2 и 3 равноудалены — берётся первый
    CHECK(nav_pick(r, 5, 2, Dir::kNone) == 2);
    CHECK(nav_pick(r, 5, 7, Dir::kUp) == 7);

    // Перекрытие по поперечной оси важнее близости: справа вниз-наискосок ближе, но в ряду — дальше.
    const ui::Rect s[] = {{0, 0, 20, 16}, {30, 30, 20, 16}, {200, 0, 20, 16}};
    CHECK(nav_pick(s, 3, 0, Dir::kRight) == 2);
    CHECK(nav_pick(s, 3, 0, Dir::kDown) == 1);  // единственный ниже
}
