// Хост и реестр мини-игр (T050). Контракт — sdk.h, пояснения — docs/formats/minigame.md.
#include "minigames/sdk.h"

#include <cstdio>
#include <cstring>

namespace minigame {

namespace {

Desc g_games[kMaxGames];
int g_count = 0;

alignas(16) uint8_t g_arena[kStateBudget];
HostState g_state = HostState::kIdle;
GameIndex g_current = kNoGame;
float g_elapsed = 0.0f;  // активное время попытки, с
int g_menu = 0;          // пункт меню паузы: 0 — «Продолжить», 1 — «Выйти»
Result g_result;
bool g_has_result = false;

const char* g_title = "";
const char* g_pause_title = nullptr;
const char* g_pause_resume = nullptr;
const char* g_pause_quit = nullptr;

constexpr float kMaxDt = 0.1f;
constexpr float kMinDt = 0.001f;

bool is_single_bit(uint8_t v) { return v != 0 && (v & (v - 1)) == 0; }

void finish(bool aborted) {
    const Desc& d = g_games[g_current];
    Result r;
    r.outcome = Outcome::kCompleted;
    d.result(g_arena, &r);
    if (aborted) r.outcome = Outcome::kAborted;
    if (r.score < 0) r.score = 0;
    if (r.quality < 0.0f) r.quality = 0.0f;
    if (r.quality > 1.0f) r.quality = 1.0f;
    if (r.outcome == Outcome::kFailed) r.quality = 0.0f;
    r.duration_ms = static_cast<uint32_t>(g_elapsed * 1000.0f + 0.5f);
    g_result = r;
    g_has_result = true;
    g_state = HostState::kIdle;
    g_current = kNoGame;
}

void set_paused(bool paused) {
    const Desc& d = g_games[g_current];
    g_state = paused ? HostState::kPaused : HostState::kRunning;
    g_menu = 0;
    if (d.on_pause) d.on_pause(g_arena, paused);
}

// Объёмная рамка как у окон ОС (ui_common: светлая сверху-слева, тёмная снизу-справа).
void draw_bevel(int x, int y, int w, int h) {
    render::draw_rect(x, y, w, 1, ui::kColLight);
    render::draw_rect(x, y, 1, h, ui::kColLight);
    render::draw_rect(x, y + h - 1, w, 1, ui::kColDark);
    render::draw_rect(x + w - 1, y, 1, h, ui::kColDark);
    render::draw_rect(x + 1, y + h - 2, w - 2, 1, ui::kColShadow);
    render::draw_rect(x + w - 2, y + 1, 1, h - 2, ui::kColShadow);
}

void draw_pause(const Canvas& c) {
    fill(c, 0, 0, c.w, c.h, render::rgba(0, 0, 0, 120));
    const int lh = ui::line_height(ui::Font::kUi12);
    const int w = 120;
    const int h = 8 + (lh + 4) * 3;
    const int x = c.x + (c.w - w) / 2;
    const int y = c.y + (c.h - h) / 2;
    render::draw_rect(x, y, w, h, ui::kColFace);
    draw_bevel(x, y, w, h);
    ui::draw_text(x + (w - ui::text_width(g_pause_title, ui::Font::kUi14Bold)) / 2, y + 4,
                  g_pause_title, ui::kColText, ui::Font::kUi14Bold);
    const char* items[2] = {g_pause_resume, g_pause_quit};
    for (int i = 0; i < 2; ++i) {
        const int iy = y + 4 + (lh + 4) * (i + 1);
        const bool sel = i == g_menu;
        if (sel) render::draw_rect(x + 6, iy - 1, w - 12, lh + 2, ui::kColAccent);
        ui::draw_text(x + (w - ui::text_width(items[i])) / 2, iy, items[i],
                      sel ? ui::kColTextInv : ui::kColText);
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Реестр

GameIndex register_game(const Desc& desc) {
    const char* id = desc.id ? desc.id : "(null)";
    if (g_count >= kMaxGames) {
        std::fprintf(stderr, "minigame %s: registry full\n", id);
        return kNoGame;
    }
    if (!desc.id || !desc.title_key || !desc.start || !desc.update || !desc.draw || !desc.result) {
        std::fprintf(stderr, "minigame %s: id, title_key, start/update/draw/result required\n", id);
        return kNoGame;
    }
    if (desc.kinds == 0 || (desc.kinds & ~(kPlayable | kService)) != 0) {
        std::fprintf(stderr, "minigame %s: bad kinds\n", id);
        return kNoGame;
    }
    if (desc.width <= 0 || desc.width > kMaxCanvasWidth || desc.height <= 0 ||
        desc.height > kMaxCanvasHeight) {
        std::fprintf(stderr, "minigame %s: canvas %dx%d out of %dx%d\n", id, desc.width,
                     desc.height, kMaxCanvasWidth, kMaxCanvasHeight);
        return kNoGame;
    }
    if (desc.state_size == 0 || desc.state_size > kStateBudget) {
        std::fprintf(stderr, "minigame %s: state %u bytes, budget %u\n", id,
                     static_cast<unsigned>(desc.state_size), static_cast<unsigned>(kStateBudget));
        return kNoGame;
    }
    if (find_game(desc.id) != kNoGame) {
        std::fprintf(stderr, "minigame %s: duplicate id\n", id);
        return kNoGame;
    }
    if (desc.load && !desc.load()) {
        std::fprintf(stderr, "minigame %s: load failed\n", id);
        return kNoGame;
    }
    g_games[g_count] = desc;
    return static_cast<GameIndex>(g_count++);
}

int game_count() { return g_count; }

const Desc* game(GameIndex index) {
    return index >= 0 && index < g_count ? &g_games[index] : nullptr;
}

GameIndex find_game(const char* id) {
    if (!id) return kNoGame;
    for (int i = 0; i < g_count; ++i)
        if (std::strcmp(g_games[i].id, id) == 0) return static_cast<GameIndex>(i);
    return kNoGame;
}

int validate(const core::GameData& data) {
    int missing = 0;
    for (int i = 0; i < g_count; ++i) {
        const Desc& d = g_games[i];
        if (!(d.kinds & kPlayable) || std::strcmp(d.id, "example") == 0) continue;
        if (core::data_find_minigame(data, d.id) == core::kNone) {
            std::fprintf(stderr, "minigame %s: no record in minigames.json\n", d.id);
            ++missing;
        }
    }
    for (int i = 0; i < data.minigame_count; ++i) {
        const char* id = core::data_name(data, data.minigames[i].id);
        if (find_game(id) == kNoGame) std::fprintf(stderr, "minigame %s: no code yet\n", id);
    }
    return missing;
}

// ---------------------------------------------------------------------------
// Хост

bool start(GameIndex index, const StartParams& p) {
    if (g_state != HostState::kIdle || index < 0 || index >= g_count) return false;
    const Desc& d = g_games[index];
    if (!is_single_bit(p.kind) || !(d.kinds & p.kind)) return false;
    if (!g_pause_title) {  // ui::init уже был — строки берём один раз
        g_pause_title = str("mg.pause.title");
        g_pause_resume = str("mg.pause.resume");
        g_pause_quit = str("mg.pause.quit");
    }
    g_title = str(d.title_key);
    std::memset(g_arena, 0, d.state_size);
    g_current = index;
    g_elapsed = 0.0f;
    g_menu = 0;
    g_has_result = false;
    g_state = HostState::kRunning;
    d.start(g_arena, p);
    return true;
}

HostState state() { return g_state; }
GameIndex current() { return g_current; }

void update(float dt, const platform::InputState& input) {
    if (g_state == HostState::kIdle) return;
    if (dt > kMaxDt) dt = kMaxDt;
    if (dt < kMinDt) dt = kMinDt;

    if (g_state == HostState::kPaused) {
        const uint32_t p = input.pressed;
        if (p & (platform::kButtonUp | platform::kButtonDown)) g_menu ^= 1;
        if (p & kPauseButtons) {
            set_paused(false);
        } else if (p & platform::kButtonCross) {
            if (g_menu == 0) set_paused(false);
            else finish(true);
        }
        return;
    }
    if (input.pressed & kPauseButtons) {
        set_paused(true);
        return;
    }
    Input in;
    in.held = input.held & kGameButtons;
    in.pressed = input.pressed & kGameButtons;
    in.released = input.released & kGameButtons;
    in.stick_x = input.stick_x;
    in.stick_y = input.stick_y;
    g_elapsed += dt;
    if (g_games[g_current].update(g_arena, dt, in) == Status::kFinished) finish(false);
}

void draw() {
    if (g_state == HostState::kIdle) return;
    const Desc& d = g_games[g_current];
    const int outer_w = d.width + 2 * ui::kBorder;
    const int outer_h = d.height + ui::kTitleHeight + 2 * ui::kBorder;
    const int x = (render::kScreenWidth - outer_w) / 2;
    const int y = (ui::kDesktopHeight - outer_h) / 2;
    render::draw_rect(x, y, outer_w, outer_h, ui::kColFace);
    draw_bevel(x, y, outer_w, outer_h);
    render::draw_rect(x + ui::kBorder, y + ui::kBorder, d.width, ui::kTitleHeight, ui::kColAccent);
    ui::draw_text(x + ui::kBorder + 3, y + ui::kBorder + 1, g_title, ui::kColTextInv,
                  ui::Font::kUi12, d.width - 6);

    Canvas c;
    c.x = static_cast<int16_t>(x + ui::kBorder);
    c.y = static_cast<int16_t>(y + ui::kBorder + ui::kTitleHeight);
    c.w = d.width;
    c.h = d.height;
    d.draw(g_arena, c);
    if (g_state == HostState::kPaused) draw_pause(c);
}

void abort() {
    if (g_state != HostState::kIdle) finish(true);
}

bool take_result(Result* out) {
    if (!g_has_result) return false;
    if (out) *out = g_result;
    g_has_result = false;
    return true;
}

// ---------------------------------------------------------------------------
// Помощники отрисовки

void fill(const Canvas& c, int x, int y, int w, int h, render::Color color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > c.w) w = c.w - x;
    if (y + h > c.h) h = c.h - y;
    if (w <= 0 || h <= 0) return;
    render::draw_rect(c.x + x, c.y + y, w, h, color);
}

void sprites(const Canvas& c, render::TextureId texture, const render::Sprite* list, int count) {
    constexpr int kMaxBatch = 64;
    render::Sprite batch[kMaxBatch];
    if (count > kMaxBatch) count = kMaxBatch;
    int n = 0;
    for (int i = 0; i < count; ++i) {
        const render::Sprite& s = list[i];
        if (s.x < 0 || s.y < 0 || s.x + s.w > c.w || s.y + s.h > c.h) continue;
        batch[n] = s;
        batch[n].x = static_cast<int16_t>(s.x + c.x);
        batch[n].y = static_cast<int16_t>(s.y + c.y);
        ++n;
    }
    if (n > 0) render::draw_sprites(texture, batch, n);
}

int text(const Canvas& c, int x, int y, const char* utf8, render::Color color, ui::Font font,
         int max_width) {
    if (!utf8 || x < 0 || y < 0 || x >= c.w || y + ui::line_height(font) > c.h) return 0;
    const int room = c.w - x;
    if (max_width <= 0 || max_width > room) max_width = room;
    return ui::draw_text(c.x + x, c.y + y, utf8, color, font, max_width);
}

}  // namespace minigame
