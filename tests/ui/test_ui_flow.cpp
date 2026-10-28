// Поведение ОС (T030) без графики: render/platform/audio заменены заглушками ниже.
#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>

#include "ui/ui_api.h"

// ---------------------------------------------------------------------------
// Заглушки

namespace {
int g_rects = 0;
int g_clicks_played = 0;
}  // namespace

namespace render {
void draw_rect(int, int, int, int, Color) { ++g_rects; }
void draw_sprites(TextureId, const Sprite*, int) {}
TextureId texture_find(const char*) { return kNoTexture; }
TextureId texture_load_file(const char*) { return 1; }
TextureId texture_create(int, int, TextureFormat, const void*) { return 2; }
}  // namespace render

namespace platform {
bool file_read(const char* path, void* dst, size_t capacity, size_t* out_read) {
    const char* base = std::strrchr(path, '/');
    char full[512];
    std::snprintf(full, sizeof(full), "%s/%s", PCCLUB_FONTS_DIR, base ? base + 1 : path);
    FILE* f = std::fopen(full, "rb");
    if (!f) return false;
    const size_t n = std::fread(dst, 1, capacity, f);
    const bool more = std::fgetc(f) != EOF;
    std::fclose(f);
    if (more) return false;
    *out_read = n;
    return true;
}
}  // namespace platform

namespace audio {
VoiceId play(SoundId, float, float, bool) {
    ++g_clicks_played;
    return 0;
}
}  // namespace audio

// ---------------------------------------------------------------------------
// Тестовая программа: две кнопки в ряд, под ними список.

namespace {

struct TestApp {
    int a = 0, b = 0;
    int activated = -1;
    int opened = 0, closed = 0;
    bool eat_circle = false;
    ui::ListState list;
    ui::Rect client;
};
TestApp g_app;

const char* item(int i, char* buf, int cap, void*) {
    std::snprintf(buf, static_cast<size_t>(cap), "%d", i);
    return buf;
}

void app_open(ui::Context&, void* u) { ++static_cast<TestApp*>(u)->opened; }
void app_close(void* u) { ++static_cast<TestApp*>(u)->closed; }
void app_frame(ui::Context& ctx, void* u) {
    TestApp& t = *static_cast<TestApp*>(u);
    t.client = ctx.client;
    if (t.eat_circle) ui::consume(ctx, platform::kButtonCircle);
    if (ui::button(ctx, 1, {4, 4, 60, 16}, "A")) ++t.a;
    if (ui::button(ctx, 2, {70, 4, 60, 16}, "B")) ++t.b;
    if (ui::list(ctx, 3, {4, 24, 126, 4 + 3 * ui::kRowHeight}, t.list, 5, item, nullptr) ==
        ui::ListEvent::kActivated)
        t.activated = t.list.selected;
}

void other_frame(ui::Context& ctx, void*) { ui::button(ctx, 1, {4, 4, 60, 16}, "C"); }

// Кадр: update + draw с заданными нажатиями.
void frame(uint32_t pressed = 0, float sx = 0.0f, float sy = 0.0f) {
    platform::InputState in;
    in.pressed = pressed;
    in.stick_x = sx;
    in.stick_y = sy;
    ui::update(1.0f / 30.0f, in);
    ui::draw();
}

struct Fixture {
    ui::AppId app = ui::kNoApp, other = ui::kNoApp;
    Fixture() {
        g_app = TestApp{};
        ui::InitParams params;
        params.click_sound = 0;  // любой id: play() — заглушка
        REQUIRE(ui::init(params));
        ui::AppDesc d{};
        d.id = "test";
        d.width = 140;
        d.height = 80;
        d.on_open = app_open;
        d.on_frame = app_frame;
        d.on_close = app_close;
        d.user = &g_app;
        app = ui::register_app(d);
        ui::AppDesc o{};
        o.id = "other";
        o.on_frame = other_frame;
        other = ui::register_app(o);
        REQUIRE(app != ui::kNoApp);
        REQUIRE(other != ui::kNoApp);
        ui::set_active(true);
    }
    ~Fixture() { ui::shutdown(); }
};

}  // namespace

TEST_CASE_FIXTURE(Fixture, "ui: регистрация и строки без data.pak") {
    CHECK(ui::find_app("test") == app);
    CHECK(ui::find_app("nope") == ui::kNoApp);
    ui::AppDesc dup{};
    dup.id = "test";
    dup.on_frame = other_frame;
    CHECK(ui::register_app(dup) == ui::kNoApp);   // id занят
    ui::AppDesc no_frame{};
    no_frame.id = "x";
    CHECK(ui::register_app(no_frame) == ui::kNoApp);
    CHECK(std::strcmp(ui::str("ui.missing.key"), "ui.missing.key") == 0);
    CHECK(ui::line_height(ui::Font::kUi12) == 12);
    CHECK(ui::text_width("9999") == 24);
}

TEST_CASE_FIXTURE(Fixture, "ui: открытие, фокус по умолчанию и × по кнопке") {
    CHECK(ui::active_app() == ui::kNoApp);
    REQUIRE(ui::open_app(app));
    CHECK(ui::active_app() == app);
    CHECK(g_app.opened == 0);  // on_open — в первом draw
    frame();
    CHECK(g_app.opened == 1);
    // Первый объявленный виджет получил фокус, курсор прыгнул на него: × нажимает A.
    frame(platform::kButtonCross);
    CHECK(g_app.a == 1);
    CHECK(g_app.b == 0);
    CHECK(g_clicks_played > 0);
}

TEST_CASE_FIXTURE(Fixture, "ui: крестовина переводит фокус, список берёт ↑/↓") {
    ui::open_app(app);
    frame();
    frame(platform::kButtonRight);  // A → B
    frame(platform::kButtonCross);
    CHECK(g_app.b == 1);
    frame(platform::kButtonDown);  // B → список (фокус со следующего кадра)
    frame();
    CHECK(g_app.list.selected == 0);
    frame(platform::kButtonDown);
    frame(platform::kButtonDown);
    frame(platform::kButtonDown);  // прокрутка: видно 3 строки
    CHECK(g_app.list.selected == 3);
    CHECK(g_app.list.scroll == 1);
    frame(platform::kButtonCross);  // курсор прыгнул на выделенную строку
    CHECK(g_app.activated == 3);
    frame(platform::kButtonDown);
    frame(platform::kButtonDown);  // на краю (4 из 5) — остаётся, ниже никого нет
    CHECK(g_app.list.selected == 4);
    // ↑ до верха, затем ещё ↑ — фокус уходит из списка на кнопку.
    for (int i = 0; i < 5; ++i) frame(platform::kButtonUp);
    CHECK(g_app.list.selected == 0);
    frame(platform::kButtonCross);
    CHECK(g_app.a + g_app.b == 2);  // нажата одна из кнопок над списком
}

TEST_CASE_FIXTURE(Fixture, "ui: курсор на стике выбирает строку под собой") {
    ui::open_app(app);
    frame();
    // Ведём курсор вниз, пока он не окажется над списком (строки начинаются с client.y + 26).
    for (int i = 0; i < 60 && g_app.list.selected < 1; ++i) frame(0, 0.0f, -0.3f);
    CHECK(g_app.list.selected >= 1);
}

TEST_CASE_FIXTURE(Fixture, "ui: ○ закрывает окно, программа может его перехватить") {
    ui::open_app(app);
    frame();
    g_app.eat_circle = true;
    frame(platform::kButtonCircle);
    CHECK(ui::is_open(app));
    g_app.eat_circle = false;
    frame(platform::kButtonCircle);
    CHECK_FALSE(ui::is_open(app));
    CHECK(g_app.closed == 1);
    CHECK(ui::active());
    frame(platform::kButtonCircle);  // ○ на рабочем столе — выход из ОС
    CHECK_FALSE(ui::active());
    const int before = g_rects;
    frame();
    CHECK(g_rects == before);  // выключенная ОС не рисует
}

TEST_CASE_FIXTURE(Fixture, "ui: L/R по кругу: стол → окна в порядке открытия") {
    ui::open_app(app);
    ui::open_app(other);
    frame();
    CHECK(ui::active_app() == other);
    frame(platform::kButtonR);  // после последнего — рабочий стол
    CHECK(ui::active_app() == ui::kNoApp);
    frame(platform::kButtonR);
    CHECK(ui::active_app() == app);
    frame(platform::kButtonL);
    CHECK(ui::active_app() == ui::kNoApp);
    frame(platform::kButtonL);
    CHECK(ui::active_app() == other);
    // Неактивное окно ввода не получает: × уходит в other.
    frame(platform::kButtonCross);
    CHECK(g_app.a == 0);
}

TEST_CASE_FIXTURE(Fixture, "ui: set_focus и close_app") {
    ui::open_app(app);
    frame();
    struct Hook {
        static void frame_with_focus(ui::Context& ctx, void* u) {
            app_frame(ctx, u);
            static bool once = false;
            if (!once) {
                ui::set_focus(ctx, 2);
                once = true;
            }
        }
    };
    // set_focus через другой путь: вызываем из своей программы.
    ui::AppDesc d{};
    d.id = "focus";
    d.on_frame = Hook::frame_with_focus;
    d.user = &g_app;
    const ui::AppId f = ui::register_app(d);
    REQUIRE(f != ui::kNoApp);
    ui::open_app(f);
    frame();
    frame(platform::kButtonCross);
    CHECK(g_app.b == 1);
    ui::close_app(app);
    CHECK(g_app.closed == 1);
    CHECK(ui::active_app() == f);
}
