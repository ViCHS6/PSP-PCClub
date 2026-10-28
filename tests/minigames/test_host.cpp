// Хост и реестр SDK мини-игр (T050) без графики: render и ui заменены заглушками ниже.
#include <doctest/doctest.h>

#include <cstring>

#include "minigames/example/example.h"
#include "minigames/sdk.h"

// ---------------------------------------------------------------------------
// Заглушки

namespace {
int g_rects = 0;
int g_last_rect[4] = {0, 0, 0, 0};
int g_sprites = 0;
}  // namespace

namespace render {
void draw_rect(int x, int y, int w, int h, Color) {
    ++g_rects;
    g_last_rect[0] = x;
    g_last_rect[1] = y;
    g_last_rect[2] = w;
    g_last_rect[3] = h;
}
void draw_sprites(TextureId, const Sprite*, int count) { g_sprites += count; }
}  // namespace render

namespace ui {
const char* str(const char* key) { return key; }
int line_height(Font) { return 12; }
int text_width(const char* utf8, Font) { return 6 * static_cast<int>(std::strlen(utf8)); }
int draw_text(int, int, const char* utf8, Color, Font, int) { return text_width(utf8, Font::kUi12); }
}  // namespace ui

// ---------------------------------------------------------------------------
// Тестовая игра: запоминает ввод, проверяет обнулённость арены, кончается по флагу.

namespace {

struct Probe {
    uint8_t junk[64];
    int frames;
};

minigame::Input g_last_input;
bool g_arena_was_zero = false;
bool g_finish_now = false;
int g_pauses = 0;

void probe_start(void* p, const minigame::StartParams&) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    g_arena_was_zero = true;
    for (size_t i = 0; i < sizeof(Probe); ++i)
        if (b[i] != 0) g_arena_was_zero = false;
    std::memset(p, 0xAB, sizeof(Probe::junk));  // испачкать — следующий start должен увидеть нули
}
minigame::Status probe_update(void* p, float, const minigame::Input& in) {
    ++static_cast<Probe*>(p)->frames;
    g_last_input = in;
    return g_finish_now ? minigame::Status::kFinished : minigame::Status::kRunning;
}
void probe_draw(const void*, const minigame::Canvas&) {}
void probe_result(const void* p, minigame::Result* out) {
    out->score = -5;      // хост обрезает до 0
    out->quality = 1.7f;  // и до 1
    out->outcome = minigame::Outcome::kCompleted;
    (void)p;
}
void probe_pause(void*, bool paused) { g_pauses += paused ? 1 : 0; }

minigame::Desc probe_desc() {
    minigame::Desc d;
    d.id = "probe";
    d.title_key = "mg.probe.name";
    d.kinds = minigame::kService;
    d.width = 100;
    d.height = 50;
    d.state_size = sizeof(Probe);
    d.start = probe_start;
    d.update = probe_update;
    d.draw = probe_draw;
    d.result = probe_result;
    d.on_pause = probe_pause;
    return d;
}

platform::InputState press(uint32_t buttons) {
    platform::InputState in;
    in.pressed = buttons;
    in.held = buttons;
    return in;
}

// Реестр общий на весь процесс — регистрируем один раз.
void ensure_registered() {
    static bool done = false;
    if (done) return;
    done = true;
    REQUIRE(minigame::register_game(minigame::example_desc()) != minigame::kNoGame);
    REQUIRE(minigame::register_game(probe_desc()) != minigame::kNoGame);
}

constexpr float kDt = 1.0f / 30.0f;

// Прогон «Примера»: × каждые press_every кадров. Возвращает результат.
minigame::Result run_example(uint32_t seed, uint8_t kind, int press_every, float difficulty) {
    minigame::StartParams p;
    p.seed = seed;
    p.kind = kind;
    p.difficulty = difficulty;
    REQUIRE(minigame::start(minigame::find_game("example"), p));
    int frame = 0;
    while (minigame::running() && frame < 30 * 60) {
        const bool tap = press_every > 0 && frame % press_every == press_every - 1;
        minigame::update(kDt, press(tap ? static_cast<uint32_t>(platform::kButtonCross) : 0u));
        minigame::draw();
        ++frame;
    }
    minigame::Result r;
    REQUIRE(minigame::take_result(&r));
    return r;
}

}  // namespace

TEST_CASE("minigame: register_game проверяет Desc") {
    ensure_registered();
    const int before = minigame::game_count();
    minigame::Desc d = probe_desc();
    CHECK(minigame::register_game(d) == minigame::kNoGame);  // id занят

    d.id = "bad1";
    d.start = nullptr;
    CHECK(minigame::register_game(d) == minigame::kNoGame);
    d = probe_desc();
    d.id = "bad2";
    d.state_size = minigame::kStateBudget + 1;
    CHECK(minigame::register_game(d) == minigame::kNoGame);
    d = probe_desc();
    d.id = "bad3";
    d.width = minigame::kMaxCanvasWidth + 1;
    CHECK(minigame::register_game(d) == minigame::kNoGame);
    d = probe_desc();
    d.id = "bad4";
    d.kinds = 0;
    CHECK(minigame::register_game(d) == minigame::kNoGame);
    d = probe_desc();
    d.id = "bad5";
    d.load = [] { return false; };
    CHECK(minigame::register_game(d) == minigame::kNoGame);

    CHECK(minigame::game_count() == before);
    CHECK(minigame::find_game("nope") == minigame::kNoGame);
    CHECK(minigame::game(minigame::kNoGame) == nullptr);
    CHECK(std::strcmp(minigame::game(minigame::find_game("example"))->id, "example") == 0);
}

TEST_CASE("minigame: start проверяет вид и занятость хоста") {
    ensure_registered();
    const minigame::GameIndex probe = minigame::find_game("probe");
    minigame::StartParams p;
    p.kind = minigame::kPlayable;  // probe — только kService
    CHECK_FALSE(minigame::start(probe, p));
    p.kind = minigame::kPlayable | minigame::kService;  // не один бит
    CHECK_FALSE(minigame::start(minigame::find_game("example"), p));
    p.kind = minigame::kService;
    CHECK_FALSE(minigame::start(minigame::kNoGame, p));
    REQUIRE(minigame::start(probe, p));
    CHECK(minigame::current() == probe);
    CHECK_FALSE(minigame::start(probe, p));  // уже идёт
    minigame::abort();
    CHECK_FALSE(minigame::running());
    minigame::Result r;
    CHECK(minigame::take_result(&r));
    CHECK(r.outcome == minigame::Outcome::kAborted);
    CHECK_FALSE(minigame::take_result(&r));  // только один раз
}

TEST_CASE("minigame: арена обнуляется, ввод фильтруется, результат обрезается") {
    ensure_registered();
    minigame::StartParams p;
    p.kind = minigame::kService;
    const minigame::GameIndex probe = minigame::find_game("probe");
    for (int attempt = 0; attempt < 2; ++attempt) {
        g_finish_now = false;
        REQUIRE(minigame::start(probe, p));
        CHECK(g_arena_was_zero);

        platform::InputState in = press(platform::kButtonCross | platform::kButtonL |
                                        platform::kButtonR | platform::kButtonSelect);
        in.stick_x = 0.5f;
        minigame::update(kDt, in);
        CHECK(g_last_input.pressed == platform::kButtonCross);
        CHECK(g_last_input.held == platform::kButtonCross);
        CHECK(g_last_input.stick_x == doctest::Approx(0.5f));

        g_finish_now = true;
        minigame::update(0.5f, press(0));  // dt обрезается до 0,1
        CHECK_FALSE(minigame::running());
        minigame::Result r;
        REQUIRE(minigame::take_result(&r));
        CHECK(r.outcome == minigame::Outcome::kCompleted);
        CHECK(r.score == 0);
        CHECK(r.quality == doctest::Approx(1.0f));
        CHECK(r.duration_ms == doctest::Approx(1000.0f / 30.0f + 100.0f).epsilon(0.01));
    }
}

TEST_CASE("minigame: пауза — ○/START, время стоит, «Выйти» → kAborted") {
    ensure_registered();
    g_finish_now = false;
    g_pauses = 0;
    minigame::StartParams p;
    p.kind = minigame::kService;
    REQUIRE(minigame::start(minigame::find_game("probe"), p));

    minigame::update(kDt, press(platform::kButtonCircle));
    CHECK(minigame::state() == minigame::HostState::kPaused);
    CHECK(g_pauses == 1);
    g_last_input = minigame::Input{};
    for (int i = 0; i < 30; ++i) minigame::update(kDt, press(0));
    CHECK(g_last_input.pressed == 0);  // update игры не звался
    minigame::draw();

    minigame::update(kDt, press(platform::kButtonCross));  // «Продолжить»
    CHECK(minigame::state() == minigame::HostState::kRunning);

    minigame::update(kDt, press(platform::kButtonStart));
    CHECK(minigame::state() == minigame::HostState::kPaused);
    minigame::update(kDt, press(platform::kButtonDown));
    minigame::update(kDt, press(platform::kButtonCross));  // «Выйти»
    CHECK_FALSE(minigame::running());
    minigame::Result r;
    REQUIRE(minigame::take_result(&r));
    CHECK(r.outcome == minigame::Outcome::kAborted);
    CHECK(r.duration_ms <= 40u);  // только кадр игры, пауза не считается
}

TEST_CASE("minigame: «Пример» — таймер, quality, провал, детерминизм") {
    ensure_registered();
    // Без нажатий — конец по таймеру.
    minigame::Result idle = run_example(7, minigame::kPlayable, 0, 0.5f);
    CHECK(idle.outcome == minigame::Outcome::kCompleted);
    CHECK(idle.score == 0);
    CHECK(idle.duration_ms == doctest::Approx(minigame::kExampleTimeLimit * 1000.0f).epsilon(0.01));
    minigame::Result svc = run_example(7, minigame::kService, 0, 0.5f);
    CHECK(svc.outcome == minigame::Outcome::kFailed);
    CHECK(svc.quality == 0.0f);

    // С нажатиями — 5 раундов до таймера, в границах 10–40 с не выходит за 20 с.
    for (uint32_t seed = 1; seed <= 20; ++seed) {
        const minigame::Result a = run_example(seed, minigame::kService, 13, 0.3f);
        const minigame::Result b = run_example(seed, minigame::kService, 13, 0.3f);
        CHECK(a.score == b.score);
        CHECK(a.quality == b.quality);
        CHECK(a.duration_ms == b.duration_ms);
        CHECK(a.score % 100 == 0);
        CHECK(a.quality == doctest::Approx(a.score / 500.0f));
        CHECK(a.duration_ms < 20000u);
    }
}

TEST_CASE("minigame: помощники обрезают по холсту") {
    minigame::Canvas c;
    c.x = 100;
    c.y = 50;
    c.w = 40;
    c.h = 20;
    g_rects = 0;
    minigame::fill(c, -5, -5, 20, 10, 0);
    CHECK(g_last_rect[0] == 100);
    CHECK(g_last_rect[1] == 50);
    CHECK(g_last_rect[2] == 15);
    CHECK(g_last_rect[3] == 5);
    minigame::fill(c, 30, 15, 50, 50, 0);
    CHECK(g_last_rect[2] == 10);
    CHECK(g_last_rect[3] == 5);
    minigame::fill(c, 40, 0, 5, 5, 0);  // целиком вне
    CHECK(g_rects == 2);

    render::Sprite s[2] = {{0, 0, 8, 8, 0, 0, 8, 8, 0}, {36, 0, 8, 8, 0, 0, 8, 8, 0}};
    g_sprites = 0;
    minigame::sprites(c, 1, s, 2);
    CHECK(g_sprites == 1);  // второй вылезает за правый край

    CHECK(minigame::text(c, 0, 8, "ab", 0) == 12);
    CHECK(minigame::text(c, 0, 12, "ab", 0) == 0);  // строка не влезает по высоте
}
