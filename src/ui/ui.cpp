// ОС администратора (T030): окна, фокус, курсор, текст, виджеты. Контракт — ui_api.h,
// пояснения — docs/formats/ui.md. Вся память — статическая.
#include <cstdio>
#include <cstring>

#include "ui/ui_internal.h"

namespace ui {
namespace {

using internal::Dir;
using internal::FontData;
using internal::Glyph;

// ---------------------------------------------------------------------------
// Состояние

constexpr int kDesktop = kMaxApps;  // индекс псевдоокна «рабочий стол» в g_windows
constexpr WidgetId kNoWidget = 0;
constexpr int kCloseSize = 12;      // кнопка закрытия в заголовке
constexpr int kCascadeSteps = 5;
constexpr float kFlashSeconds = 0.12f;  // сколько нажатая кнопка рисуется вдавленной

struct Focusable {
    WidgetId id;
    Rect r;              // экранные координаты
    int16_t snap_x, snap_y;  // куда прыгает курсор при фокусе крестовиной
};

struct FocusList {
    Focusable items[kMaxFocusables];
    int count = 0;
};

struct Window {
    AppDesc desc;
    const char* title = "";
    const char* icon = "";
    bool open = false;
    bool started = false;       // on_open уже вызван
    Rect frame;                 // окно целиком, экранные
    Rect client;
    WidgetId focus = kNoWidget;
    WidgetId pending_focus = kNoWidget;  // set_focus()
    bool snap = false;          // после кадра перенести курсор на фокус
    bool click = false;         // в этом кадре × по виджету click_id
    WidgetId click_id = kNoWidget;
    WidgetId flash_id = kNoWidget;
    float flash = 0.0f;
    uint32_t avail = 0;         // кнопки, ещё не взятые в этом кадре
    FocusList lists[2];
    int cur = 0;                // lists[cur] — этот кадр, lists[cur ^ 1] — прошлый
};

struct State {
    bool initialized = false;
    bool active = false;
    const core::GameData* data = nullptr;
    core::World* world = nullptr;
    audio::SoundId click_sound = audio::kNoSound;
    render::TextureId cursor_tex = render::kNoTexture;
    const char* os_name = "";

    int app_count = 0;
    AppId z[kMaxApps];          // открытые окна снизу вверх
    int z_count = 0;
    AppId order[kMaxApps];      // открытые окна в порядке открытия (панель задач, L/R)
    int order_count = 0;
    bool desktop_active = true;
    int cascade = 0;

    float cx = render::kScreenWidth / 2.0f, cy = kDesktopHeight / 2.0f;  // курсор
    bool cursor_moved = false;
    float dt = 0.0f;
};

State g;
Window g_windows[kMaxApps + 1];
FontData g_fonts[static_cast<int>(Font::kCount)];

const char* const kFontFiles[] = {"assets/font_ui12.fnt", "assets/font_mono12.fnt",
                                  "assets/font_ui14.fnt", "assets/font_ui14b.fnt"};
static_assert(sizeof(kFontFiles) / sizeof(kFontFiles[0]) == static_cast<size_t>(Font::kCount), "");

constexpr int kTextBatch = 128;
render::Sprite g_sprites[kTextBatch];
uint8_t g_fnt_buffer[32 + 16 * internal::kMaxGlyphs];

// ---------------------------------------------------------------------------
// Вспомогательное

bool contains(const Rect& r, float x, float y) {
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

Rect offset(const Context& ctx, Rect r) {
    r.x = static_cast<int16_t>(r.x + ctx.client.x);
    r.y = static_cast<int16_t>(r.y + ctx.client.y);
    return r;
}

Rect inset(Rect r, int d) {
    return {static_cast<int16_t>(r.x + d), static_cast<int16_t>(r.y + d),
            static_cast<int16_t>(r.w - 2 * d), static_cast<int16_t>(r.h - 2 * d)};
}

Window& win(const Context& ctx) { return *static_cast<Window*>(ctx.detail_); }

int active_index() {
    if (g.desktop_active || g.z_count == 0) return kDesktop;
    return g.z[g.z_count - 1];
}

const FontData& font(Font f) { return g_fonts[static_cast<int>(f)]; }

void rect(int x, int y, int w, int h, Color c) {
    if (w > 0 && h > 0) render::draw_rect(x, y, w, h, c);
}

void bevel_abs(Rect r, bool sunken) {
    const Color hi = sunken ? kColShadow : kColLight;
    const Color lo = sunken ? kColLight : kColDark;
    const Color lo2 = sunken ? kColFace : kColShadow;
    rect(r.x, r.y, r.w - 1, 1, hi);
    rect(r.x, r.y + 1, 1, r.h - 2, hi);
    rect(r.x, r.y + r.h - 1, r.w, 1, lo);
    rect(r.x + r.w - 1, r.y, 1, r.h - 1, lo);
    // Внутренняя полутень
    rect(r.x + 1, r.y + r.h - 2, r.w - 2, 1, lo2);
    rect(r.x + r.w - 2, r.y + 1, 1, r.h - 3, lo2);
    if (sunken) {
        rect(r.x + 1, r.y + 1, r.w - 3, 1, kColDark);
        rect(r.x + 1, r.y + 2, 1, r.h - 4, kColDark);
    }
}

void outline_abs(Rect r, Color c) {
    rect(r.x, r.y, r.w, 1, c);
    rect(r.x, r.y + r.h - 1, r.w, 1, c);
    rect(r.x, r.y + 1, 1, r.h - 2, c);
    rect(r.x + r.w - 1, r.y + 1, 1, r.h - 2, c);
}

void play_click() {
    if (g.click_sound != audio::kNoSound) audio::play(g.click_sound, 0.6f);
}

// Регистрирует фокусируемый виджет этого кадра. nullptr — список полон.
Focusable* add_focusable(Window& w, WidgetId id, Rect abs) {
    FocusList& list = w.lists[w.cur];
    if (list.count >= kMaxFocusables) return nullptr;
    Focusable& f = list.items[list.count++];
    f.id = id;
    f.r = abs;
    f.snap_x = static_cast<int16_t>(abs.x + abs.w / 2);
    f.snap_y = static_cast<int16_t>(abs.y + abs.h / 2);
    return &f;
}

const Focusable* hit(const FocusList& list, float x, float y) {
    for (int i = list.count - 1; i >= 0; --i)
        if (contains(list.items[i].r, x, y)) return &list.items[i];
    return nullptr;
}

// Нажали ли виджет id в этом кадре (забирает клик).
bool take_click(const Context& ctx, Window& w, WidgetId id) {
    if (!ctx.is_active || !w.click || w.click_id != id) return false;
    w.click = false;
    return true;
}

Rect close_rect(const Window& w) {
    return {static_cast<int16_t>(w.frame.x + w.frame.w - kBorder - kCloseSize - 1),
            static_cast<int16_t>(w.frame.y + kBorder + 1), kCloseSize, kCloseSize - 2};
}

// ---------------------------------------------------------------------------
// Окна

void z_remove(AppId app) {
    int j = 0;
    for (int i = 0; i < g.z_count; ++i)
        if (g.z[i] != app) g.z[j++] = g.z[i];
    g.z_count = j;
    j = 0;
    for (int i = 0; i < g.order_count; ++i)
        if (g.order[i] != app) g.order[j++] = g.order[i];
    g.order_count = j;
}

void bring_to_front(AppId app) {
    int j = 0;
    for (int i = 0; i < g.z_count; ++i)
        if (g.z[i] != app) g.z[j++] = g.z[i];
    g.z[j++] = app;
    g.z_count = j;
    g.desktop_active = false;
}

void activate(int index, bool snap) {
    if (index == kDesktop) g.desktop_active = true;
    else bring_to_front(static_cast<AppId>(index));
    if (snap) g_windows[index].snap = true;
}

void layout_window(Window& w) {
    int cw = w.desc.width, ch = w.desc.height;
    if (w.desc.flags & kAppMaximized) cw = kMaxClientWidth, ch = kMaxClientHeight;
    if (cw > kMaxClientWidth) cw = kMaxClientWidth;
    if (ch > kMaxClientHeight) ch = kMaxClientHeight;
    if (cw < 40) cw = 40;
    if (ch < 8) ch = 8;
    const int fw = cw + 2 * kBorder, fh = ch + kTitleHeight + 2 * kBorder;
    int x = (render::kScreenWidth - fw) / 2, y = (kDesktopHeight - fh) / 2;
    if (!(w.desc.flags & kAppMaximized)) {
        const int step = 12 * (g.cascade % kCascadeSteps);
        x += step;
        y += step;
        ++g.cascade;
    }
    if (x + fw > render::kScreenWidth) x = render::kScreenWidth - fw;
    if (y + fh > kDesktopHeight) y = kDesktopHeight - fh;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    w.frame = {static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(fw),
               static_cast<int16_t>(fh)};
    w.client = {static_cast<int16_t>(x + kBorder), static_cast<int16_t>(y + kBorder + kTitleHeight),
                static_cast<int16_t>(cw), static_cast<int16_t>(ch)};
}

void close_window(AppId app) {
    Window& w = g_windows[app];
    if (!w.open) return;
    w.open = false;
    z_remove(app);
    if (w.started && w.desc.on_close) w.desc.on_close(w.desc.user);
    w.started = false;
    const int now = active_index();
    g_windows[now].snap = true;
}

// Окно под точкой экрана (сверху вниз), иначе рабочий стол.
int window_at(float x, float y) {
    for (int i = g.z_count - 1; i >= 0; --i)
        if (contains(g_windows[g.z[i]].frame, x, y)) return g.z[i];
    return kDesktop;
}

// ---------------------------------------------------------------------------
// Текст

int draw_text_impl(int x, int y, const char* utf8, Color color, const FontData& f, int max_width) {
    if (!utf8 || f.glyph_count == 0) return 0;
    const Glyph& ellipsis = internal::font_glyph(f, 0x2026);
    int n = 0;
    int widest = 0;
    int top = y;
    const char* s = utf8;
    auto emit = [&](const Glyph& gl, int pen) {
        if (gl.w == 0 || pen > 2048 || pen < -2048 || top > 2048 || top < -2048) return;
        render::Sprite& sp = g_sprites[n++];
        sp.x = static_cast<int16_t>(pen + gl.x_offset);
        sp.y = static_cast<int16_t>(top + gl.y_offset);
        sp.w = gl.w;
        sp.h = gl.h;
        sp.u = static_cast<int16_t>(gl.x);
        sp.v = static_cast<int16_t>(gl.y);
        sp.uw = gl.w;
        sp.vh = gl.h;
        sp.color = color;
        if (n == kTextBatch) {
            render::draw_sprites(f.texture, g_sprites, n);
            n = 0;
        }
    };
    while (*s) {
        // Одна строка: ширина до '\n', при переполнении — обрезка с «…».
        int line_width = 0;
        for (const char* t = s; *t && *t != '\n';) line_width += internal::font_glyph(f, internal::utf8_next(&t)).advance;
        const bool cut = max_width > 0 && line_width > max_width;
        const int limit = cut ? max_width - ellipsis.advance : 1 << 30;
        int pen = x;
        while (*s && *s != '\n') {
            const Glyph& gl = internal::font_glyph(f, internal::utf8_next(&s));
            if (pen + gl.advance - x > limit) {
                while (*s && *s != '\n') ++s;
                break;
            }
            emit(gl, pen);
            pen += gl.advance;
        }
        if (cut && limit >= 0) {
            emit(ellipsis, pen);
            pen += ellipsis.advance;
        }
        if (pen - x > widest) widest = pen - x;
        if (*s == '\n') {
            ++s;
            top += f.line_height;
        }
    }
    if (n > 0) render::draw_sprites(f.texture, g_sprites, n);
    return widest;
}

void text_in_rect(Rect r, const char* utf8, Color color, Font fnt, Align align) {
    const FontData& f = font(fnt);
    int w = internal::font_text_width(f, utf8);
    if (w > r.w) w = r.w;
    int x = r.x;
    if (align == Align::kCenter) x += (r.w - w) / 2;
    else if (align == Align::kRight) x += r.w - w;
    draw_text_impl(x, r.y + (r.h - f.line_height) / 2, utf8, color, f, r.w);
}

// ---------------------------------------------------------------------------
// Курсор: текстура 16×16 из битовой карты, один спрайт.

const char* const kCursorBitmap[] = {
    "X...........",
    "XX..........",
    "X#X.........",
    "X##X........",
    "X###X.......",
    "X####X......",
    "X#####X.....",
    "X######X....",
    "X#######X...",
    "X####XXXXX..",
    "X##X##X.....",
    "X#X.X##X....",
    "XX..X##X....",
    "X....X##X...",
    ".....X##X...",
    "......XX....",
};

void create_cursor() {
    if (g.cursor_tex != render::kNoTexture) return;
    static uint32_t pixels[16 * 16];
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            const char c = x < 12 ? kCursorBitmap[y][x] : '.';
            pixels[y * 16 + x] = c == 'X' ? render::kBlack : c == '#' ? render::kWhite : 0u;
        }
    g.cursor_tex = render::texture_create(16, 16, render::TextureFormat::kRgba8888, pixels);
}

// ---------------------------------------------------------------------------
// Рабочий стол — псевдоокно kDesktop со своим on_frame.

constexpr int kIconCellW = 56, kIconCellH = 52;
constexpr WidgetId kTaskbarBase = 200;

void desktop_frame(Context& ctx, void*) {
    Window& self = win(ctx);
    rect(0, 0, render::kScreenWidth, kDesktopHeight, kColDesktop);
    {
        const char* os = g.os_name;
        const FontData& f = font(Font::kUi14Bold);
        const int w = internal::font_text_width(f, os);
        draw_text_impl(render::kScreenWidth - w - 8, kDesktopHeight - f.line_height - 6, os,
                       render::rgba(0x5A, 0x8E, 0xAA), f, 0);
    }

    // Значки — столбцами сверху вниз.
    int slot = 0;
    const int rows = (kDesktopHeight - 8) / kIconCellH;
    for (int i = 0; i < g.app_count; ++i) {
        Window& w = g_windows[i];
        if (!(w.desc.flags & kAppOnDesktop)) continue;
        const Rect cell{static_cast<int16_t>(4 + (slot / rows) * kIconCellW),
                        static_cast<int16_t>(6 + (slot % rows) * kIconCellH), kIconCellW, kIconCellH};
        ++slot;
        const WidgetId id = static_cast<WidgetId>(1 + i);
        add_focusable(self, id, cell);
        const bool focused = ctx.is_active && self.focus == id;
        const Rect box{static_cast<int16_t>(cell.x + (kIconCellW - 30) / 2), static_cast<int16_t>(cell.y + 2), 30, 26};
        rect(box.x, box.y, box.w, box.h, w.desc.icon_color);
        bevel_abs(box, false);
        text_in_rect(inset(box, 2), w.icon, kColTextInv, Font::kUi14Bold, Align::kCenter);
        const Rect cap{cell.x, static_cast<int16_t>(cell.y + 31), cell.w, 14};
        const FontData& f = font(Font::kUi12);
        int tw = internal::font_text_width(f, w.title);
        if (tw > cap.w - 2) tw = cap.w - 2;
        if (focused) rect(cap.x + (cap.w - tw) / 2 - 1, cap.y, tw + 2, cap.h, kColAccent);
        text_in_rect(cap, w.title, kColTextInv, Font::kUi12, Align::kCenter);
        if (take_click(ctx, self, id)) {
            play_click();
            open_app(static_cast<AppId>(i));
        }
    }

    // Панель задач
    const int ty = kDesktopHeight;
    rect(0, ty, render::kScreenWidth, kTaskbarHeight, kColFace);
    rect(0, ty, render::kScreenWidth, 1, kColLight);

    // Справа: деньги и часы.
    char clock[48];
    char money[24] = "";
    if (g.world) {
        format_money(g.world->economy.money, money, sizeof(money));
        std::snprintf(clock, sizeof(clock), "%s  %02d:%02d", money, g.world->clock.minute / 60,
                      g.world->clock.minute % 60);
    } else {
        std::snprintf(clock, sizeof(clock), "--:--");
    }
    const int cw = internal::font_text_width(font(Font::kUi12), clock) + 10;
    const Rect tray{static_cast<int16_t>(render::kScreenWidth - cw - 2), static_cast<int16_t>(ty + 2),
                    static_cast<int16_t>(cw), kTaskbarHeight - 4};
    bevel_abs(tray, true);
    text_in_rect(inset(tray, 2), clock, kColText, Font::kUi12, Align::kCenter);

    // Кнопки открытых окон.
    if (g.order_count > 0) {
        const int avail = tray.x - 4 - 2;
        int bw = avail / g.order_count - 2;
        if (bw > 100) bw = 100;
        for (int i = 0; i < g.order_count; ++i) {
            const AppId app = g.order[i];
            const Rect b{static_cast<int16_t>(2 + i * (bw + 2)), static_cast<int16_t>(ty + 2),
                         static_cast<int16_t>(bw), kTaskbarHeight - 4};
            const WidgetId id = static_cast<WidgetId>(kTaskbarBase + app);
            add_focusable(self, id, b);
            const bool on = active_index() == app;
            if (on) rect(b.x + 1, b.y + 1, b.w - 2, b.h - 2, kColLight);
            bevel_abs(b, on);
            if (ctx.is_active && self.focus == id) outline_abs(inset(b, 2), kColAccent);
            text_in_rect(inset(b, 3), g_windows[app].title, kColText, Font::kUi12, Align::kLeft);
            if (take_click(ctx, self, id)) {
                play_click();
                activate(app, true);
            }
        }
    }
}

void draw_window_frame(const Window& w, bool active) {
    const Rect f = w.frame;
    rect(f.x, f.y, f.w, f.h, kColFace);
    bevel_abs(f, false);
    const Rect title{static_cast<int16_t>(f.x + kBorder), static_cast<int16_t>(f.y + kBorder),
                     static_cast<int16_t>(f.w - 2 * kBorder), kTitleHeight - 1};
    rect(title.x, title.y, title.w, title.h, active ? kColAccent : kColAccentDim);
    const Rect cr = close_rect(w);
    Rect text_r = title;
    text_r.x = static_cast<int16_t>(text_r.x + 3);
    text_r.w = static_cast<int16_t>(cr.x - text_r.x - 2);
    text_in_rect(text_r, w.title, kColTextInv, Font::kUi12, Align::kLeft);
    if (!(w.desc.flags & kAppNoClose)) {
        rect(cr.x, cr.y, cr.w, cr.h, kColFace);
        bevel_abs(cr, false);
        text_in_rect(cr, "\xC3\x97", kColText, Font::kUi12, Align::kCenter);  // ×
    }
}

void run_window(int index, bool is_active) {
    Window& w = g_windows[index];
    Context ctx;
    ctx.app = index == kDesktop ? kNoApp : static_cast<AppId>(index);
    ctx.client = w.client;
    ctx.is_active = is_active;
    ctx.dt = g.dt;
    ctx.world = g.world;
    ctx.data = g.data;
    ctx.detail_ = &w;
    if (!is_active) {
        w.avail = 0;
        w.click = false;
    }
    if (w.flash > 0.0f) w.flash -= g.dt;

    if (index != kDesktop) {
        draw_window_frame(w, is_active);
        if (!w.started) {
            w.started = true;
            if (w.desc.on_open) w.desc.on_open(ctx, w.desc.user);
        }
    }
    w.lists[w.cur].count = 0;
    w.desc.on_frame(ctx, w.desc.user);
    const FocusList& list = w.lists[w.cur];

    if (is_active && list.count > 0) {
        int idx = -1;
        if (w.pending_focus != kNoWidget) {
            for (int i = 0; i < list.count; ++i)
                if (list.items[i].id == w.pending_focus) idx = i;
            w.pending_focus = kNoWidget;
            if (idx >= 0) w.snap = true;
        }
        if (idx < 0)
            for (int i = 0; i < list.count; ++i)
                if (list.items[i].id == w.focus) idx = i;
        if (idx < 0) {
            idx = 0;
            w.snap = true;
        }
        // Оставшаяся крестовина — навигация.
        Dir dir = Dir::kNone;
        if (w.avail & platform::kButtonUp) dir = Dir::kUp;
        else if (w.avail & platform::kButtonDown) dir = Dir::kDown;
        else if (w.avail & platform::kButtonLeft) dir = Dir::kLeft;
        else if (w.avail & platform::kButtonRight) dir = Dir::kRight;
        if (dir != Dir::kNone) {
            static Rect rects[kMaxFocusables];
            for (int i = 0; i < list.count; ++i) rects[i] = list.items[i].r;
            const int next = internal::nav_pick(rects, list.count, idx, dir);
            if (next != idx) {
                idx = next;
                w.snap = true;
            }
        }
        w.focus = list.items[idx].id;
        if (w.snap) {
            g.cx = list.items[idx].snap_x;
            g.cy = list.items[idx].snap_y;
        }
    }
    if (is_active) w.snap = false;

    if (is_active && (w.avail & platform::kButtonCircle)) {
        if (index == kDesktop) g.active = false;
        else if (!(w.desc.flags & kAppNoClose)) close_window(static_cast<AppId>(index));
    }
    w.avail = 0;
    w.click = false;
    w.cur ^= 1;
}

}  // namespace

// ---------------------------------------------------------------------------
// Жизненный цикл

bool init(const InitParams& params) {
    g = State{};
    g.data = params.data;
    g.click_sound = params.click_sound;
    for (int i = 0; i < static_cast<int>(Font::kCount); ++i) {
        FontData& f = g_fonts[i];
        f = FontData{};
        size_t size = 0;
        if (!platform::file_read(kFontFiles[i], g_fnt_buffer, sizeof(g_fnt_buffer), &size)) {
            std::fprintf(stderr, "ui: cannot read %s\n", kFontFiles[i]);
            return false;
        }
        const char* error = "";
        if (!internal::font_parse(g_fnt_buffer, size, &f, &error)) {
            std::fprintf(stderr, "ui: %s: %s\n", kFontFiles[i], error);
            f = FontData{};
            return false;
        }
        f.texture = render::texture_find(f.atlas_name);
        if (f.texture == render::kNoTexture) {
            char path[48];
            std::snprintf(path, sizeof(path), "assets/%s.tex", f.atlas_name);
            f.texture = render::texture_load_file(path);
        }
        if (f.texture == render::kNoTexture) {
            std::fprintf(stderr, "ui: cannot load atlas for %s\n", kFontFiles[i]);
            return false;
        }
    }
    create_cursor();
    g.os_name = str("ui.os.name");

    Window& d = g_windows[kDesktop];
    d = Window{};
    d.desc.on_frame = desktop_frame;
    d.open = true;
    d.started = true;
    d.frame = {0, 0, render::kScreenWidth, render::kScreenHeight};
    d.client = d.frame;
    g.initialized = true;
    return true;
}

void shutdown() {
    if (!g.initialized) return;
    while (g.z_count > 0) close_window(g.z[g.z_count - 1]);
    for (int i = 0; i < g.app_count; ++i) g_windows[i] = Window{};
    const render::TextureId cursor = g.cursor_tex;
    g = State{};
    g.cursor_tex = cursor;  // текстуры живут до render::shutdown()
}

void set_world(core::World* world) { g.world = world; }

void set_active(bool active) {
    if (active && !g.active) g_windows[active_index()].snap = true;
    g.active = active && g.initialized;
}

bool active() { return g.active; }

void update(float dt, const platform::InputState& in) {
    if (!g.active) return;
    g.dt = dt;

    // Курсор на стике: скорость растёт с отклонением.
    const float ax = in.stick_x < 0 ? -in.stick_x : in.stick_x;
    const float ay = in.stick_y < 0 ? -in.stick_y : in.stick_y;
    const float mag = ax > ay ? ax : ay;
    g.cursor_moved = mag > 0.0f;
    if (g.cursor_moved) {
        const float speed = 80.0f + 320.0f * mag;
        g.cx += in.stick_x * speed * dt;
        g.cy -= in.stick_y * speed * dt;
        if (g.cx < 0) g.cx = 0;
        if (g.cy < 0) g.cy = 0;
        if (g.cx > render::kScreenWidth - 1) g.cx = render::kScreenWidth - 1;
        if (g.cy > render::kScreenHeight - 1) g.cy = render::kScreenHeight - 1;
    }

    // L/R — по кругу: рабочий стол, затем окна в порядке открытия.
    const uint32_t lr = in.pressed & (platform::kButtonL | platform::kButtonR);
    if (lr == platform::kButtonL || lr == platform::kButtonR) {
        const int n = g.order_count + 1;
        int pos = 0;
        const int now = active_index();
        for (int i = 0; i < g.order_count; ++i)
            if (g.order[i] == now) pos = i + 1;
        pos = (pos + (lr == platform::kButtonR ? 1 : n - 1)) % n;
        activate(pos == 0 ? kDesktop : g.order[pos - 1], true);
    }

    uint32_t avail = in.pressed & ~(platform::kButtonL | platform::kButtonR | platform::kButtonStart |
                                    platform::kButtonSelect);
    int target = active_index();

    if (avail & platform::kButtonCross) {
        const int under = window_at(g.cx, g.cy);
        if (under != target) {
            activate(under, false);
            target = under;
        }
        Window& w = g_windows[target];
        if (target != kDesktop && !(w.desc.flags & kAppNoClose) && contains(close_rect(w), g.cx, g.cy)) {
            play_click();
            close_window(static_cast<AppId>(target));
            avail &= ~platform::kButtonCross;
            target = active_index();
        } else if (const Focusable* f = hit(w.lists[w.cur ^ 1], g.cx, g.cy)) {
            w.focus = f->id;
            w.click = true;
            w.click_id = f->id;
            avail &= ~platform::kButtonCross;
        }
    } else if (g.cursor_moved) {
        Window& w = g_windows[target];
        if (window_at(g.cx, g.cy) == target)
            if (const Focusable* f = hit(w.lists[w.cur ^ 1], g.cx, g.cy)) w.focus = f->id;
    }
    g_windows[target].avail = avail;
}

void draw() {
    if (!g.active) return;
    const int act = active_index();
    run_window(kDesktop, act == kDesktop);
    // Снимок стопки: on_frame может открывать и закрывать окна.
    AppId z[kMaxApps];
    const int z_count = g.z_count;
    std::memcpy(z, g.z, sizeof(z));
    for (int i = 0; i < z_count; ++i)
        if (g_windows[z[i]].open) run_window(z[i], act == z[i]);

    if (g.cursor_tex != render::kNoTexture) {
        const render::Sprite s{static_cast<int16_t>(g.cx), static_cast<int16_t>(g.cy), 16, 16, 0, 0, 16, 16,
                               render::kWhite};
        render::draw_sprites(g.cursor_tex, &s, 1);
    }
}

// ---------------------------------------------------------------------------
// Строки

const char* str(const char* key) {
    if (!key) return "";
    if (g.data) {
        const core::StrId id = core::data_find_string(*g.data, key);
        if (id != core::kNone) return core::data_string(*g.data, id);
    }
    std::fprintf(stderr, "ui: no string '%s'\n", key);
    return key;
}

// ---------------------------------------------------------------------------
// Программы

AppId register_app(const AppDesc& desc) {
    if (!desc.id || !desc.on_frame || g.app_count >= kMaxApps || find_app(desc.id) != kNoApp) {
        std::fprintf(stderr, "ui: cannot register app '%s'\n", desc.id ? desc.id : "(null)");
        return kNoApp;
    }
    const AppId app = static_cast<AppId>(g.app_count++);
    Window& w = g_windows[app];
    w = Window{};
    w.desc = desc;
    w.title = desc.title_key ? str(desc.title_key) : desc.id;
    w.icon = desc.icon_text ? str(desc.icon_text) : "";
    return app;
}

AppId find_app(const char* id) {
    if (!id) return kNoApp;
    for (int i = 0; i < g.app_count; ++i)
        if (std::strcmp(g_windows[i].desc.id, id) == 0) return static_cast<AppId>(i);
    return kNoApp;
}

bool open_app(AppId app) {
    if (app >= g.app_count) return false;
    Window& w = g_windows[app];
    if (!w.open) {
        layout_window(w);
        w.open = true;
        w.started = false;
        w.focus = kNoWidget;
        w.lists[0].count = w.lists[1].count = 0;
        g.order[g.order_count++] = app;
    }
    activate(app, true);
    return true;
}

void close_app(AppId app) {
    if (app < g.app_count) close_window(app);
}

bool is_open(AppId app) { return app < g.app_count && g_windows[app].open; }

AppId active_app() {
    const int a = active_index();
    return a == kDesktop ? kNoApp : static_cast<AppId>(a);
}

// ---------------------------------------------------------------------------
// Ввод и фокус

uint32_t pressed(const Context& ctx) { return ctx.is_active ? win(ctx).avail : 0u; }

uint32_t consume(Context& ctx, uint32_t buttons) {
    if (!ctx.is_active) return 0;
    Window& w = win(ctx);
    const uint32_t got = w.avail & buttons;
    w.avail &= ~got;
    return got;
}

void set_focus(Context& ctx, WidgetId id) { win(ctx).pending_focus = id; }

bool is_focused(const Context& ctx, WidgetId id) { return ctx.is_active && win(ctx).focus == id; }

// ---------------------------------------------------------------------------
// Текст

int line_height(Font f) { return font(f).line_height; }

int text_width(const char* utf8, Font f) { return internal::font_text_width(font(f), utf8); }

int draw_text(int x, int y, const char* utf8, Color color, Font f, int max_width) {
    return draw_text_impl(x, y, utf8, color, font(f), max_width);
}

void label(Context& ctx, Rect r, const char* utf8, Color color, Font f, Align align) {
    text_in_rect(offset(ctx, r), utf8, color, f, align);
}

// ---------------------------------------------------------------------------
// Примитивы

void fill(Context& ctx, Rect r, Color color) {
    r = offset(ctx, r);
    rect(r.x, r.y, r.w, r.h, color);
}

void outline(Context& ctx, Rect r, Color color) { outline_abs(offset(ctx, r), color); }

void bevel(Context& ctx, Rect r, bool sunken) { bevel_abs(offset(ctx, r), sunken); }

void group(Context& ctx, Rect r, const char* caption) {
    const Rect a = offset(ctx, r);
    const int top = a.y + 6;
    // Гравированная рамка: тень + свет со сдвигом.
    outline_abs({a.x, static_cast<int16_t>(top), static_cast<int16_t>(a.w - 1), static_cast<int16_t>(a.h - 7)},
                kColShadow);
    outline_abs({static_cast<int16_t>(a.x + 1), static_cast<int16_t>(top + 1), static_cast<int16_t>(a.w - 1),
                 static_cast<int16_t>(a.h - 7)},
                kColLight);
    if (caption && *caption) {
        const FontData& f = font(Font::kUi12);
        int w = internal::font_text_width(f, caption);
        if (w > a.w - 16) w = a.w - 16;
        rect(a.x + 6, a.y, w + 4, f.line_height, kColFace);
        draw_text_impl(a.x + 8, a.y, caption, kColText, f, a.w - 16);
    }
}

// ---------------------------------------------------------------------------
// Виджеты

bool button(Context& ctx, WidgetId id, Rect r, const char* text, uint8_t flags) {
    Window& w = win(ctx);
    const Rect a = offset(ctx, r);
    const bool disabled = flags & kWidgetDisabled;
    if (!disabled) add_focusable(w, id, a);
    const bool clicked = !disabled && take_click(ctx, w, id);
    if (clicked) {
        play_click();
        w.flash_id = id;
        w.flash = kFlashSeconds;
    }
    const bool down = w.flash > 0.0f && w.flash_id == id;
    rect(a.x, a.y, a.w, a.h, kColFace);
    bevel_abs(a, down);
    if (!disabled && ctx.is_active && w.focus == id) outline_abs(inset(a, 3), kColAccent);
    Rect t = inset(a, 3);
    if (down) t.x = static_cast<int16_t>(t.x + 1), t.y = static_cast<int16_t>(t.y + 1);
    if (disabled) {
        Rect sh = t;
        sh.x = static_cast<int16_t>(sh.x + 1), sh.y = static_cast<int16_t>(sh.y + 1);
        text_in_rect(sh, text, kColLight, Font::kUi12, Align::kCenter);
    }
    text_in_rect(t, text, disabled ? kColShadow : kColText, Font::kUi12, Align::kCenter);
    return clicked;
}

bool hotspot(Context& ctx, WidgetId id, Rect r, uint8_t flags) {
    if (flags & kWidgetDisabled) return false;
    Window& w = win(ctx);
    add_focusable(w, id, offset(ctx, r));
    const bool clicked = take_click(ctx, w, id);
    if (clicked) play_click();
    return clicked;
}

bool choice(Context& ctx, WidgetId id, Rect r, int* index, int count, ItemTextFn text, void* user,
            uint8_t flags) {
    Window& w = win(ctx);
    const Rect a = offset(ctx, r);
    const bool disabled = (flags & kWidgetDisabled) || !index || count <= 0;
    int value = index ? *index : 0;
    if (value >= count) value = count - 1;
    if (value < 0) value = 0;
    const int before = index ? *index : 0;
    if (!disabled) {
        add_focusable(w, id, a);
        if (ctx.is_active && w.focus == id) {
            const uint32_t got = w.avail & (platform::kButtonLeft | platform::kButtonRight);
            w.avail &= ~got;
            if ((got & platform::kButtonLeft) && value > 0) --value;
            if ((got & platform::kButtonRight) && value < count - 1) ++value;
        }
        if (take_click(ctx, w, id)) {
            if (g.cx < a.x + a.w / 2.0f) {
                if (value > 0) --value;
            } else if (value < count - 1) {
                ++value;
            }
        }
        *index = value;
    }
    const bool changed = !disabled && value != before;
    if (changed) play_click();

    rect(a.x, a.y, a.w, a.h, kColField);
    bevel_abs(a, true);
    const bool focused = !disabled && ctx.is_active && w.focus == id;
    if (focused) outline_abs(inset(a, 3), kColAccent);
    const Color arrow_on = disabled ? kColShadow : kColText;
    const Rect inner = inset(a, 2);
    text_in_rect({static_cast<int16_t>(inner.x + 2), inner.y, 10, inner.h}, "\xE2\x86\x90",  // ←
                 value > 0 ? arrow_on : kColShadow, Font::kUi12, Align::kLeft);
    text_in_rect({static_cast<int16_t>(inner.x + inner.w - 12), inner.y, 10, inner.h}, "\xE2\x86\x92",  // →
                 value < count - 1 ? arrow_on : kColShadow, Font::kUi12, Align::kRight);
    if (count > 0 && text) {
        char buf[96];
        buf[0] = 0;
        const char* s = text(value, buf, sizeof(buf), user);
        text_in_rect({static_cast<int16_t>(inner.x + 14), inner.y, static_cast<int16_t>(inner.w - 28), inner.h}, s,
                     disabled ? kColShadow : kColText, Font::kUi12, Align::kCenter);
    }
    return changed;
}

void progress(Context& ctx, Rect r, float value, Color fill_color, const char* caption) {
    const Rect a = offset(ctx, r);
    if (!(value >= 0.0f)) value = 0.0f;  // и NaN
    if (value > 1.0f) value = 1.0f;
    rect(a.x, a.y, a.w, a.h, kColField);
    bevel_abs(a, true);
    const Rect inner = inset(a, 2);
    rect(inner.x, inner.y, static_cast<int>(static_cast<float>(inner.w) * value + 0.5f), inner.h, fill_color);
    if (caption && *caption) {
        // Тёмный текст с обводкой цветом поля — читается и на заливке, и на пустой части.
        const FontData& f = font(Font::kUi12);
        int w = internal::font_text_width(f, caption);
        if (w > inner.w) w = inner.w;
        const int x = inner.x + (inner.w - w) / 2;
        const int y = inner.y + (inner.h - f.line_height) / 2;
        static const int8_t kOff[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
        for (const auto& o : kOff) draw_text_impl(x + o[0], y + o[1], caption, kColField, f, inner.w);
        draw_text_impl(x, y, caption, kColText, f, inner.w);
    }
}

namespace {

// Общая логика list/table: rows — область строк (экранная), full — весь виджет.
ListEvent list_logic(Context& ctx, WidgetId id, Rect full, Rect rows, ListState& st, int count, int visible) {
    Window& w = win(ctx);
    ListEvent ev = ListEvent::kNone;
    if (count <= 0) {
        st.selected = -1;
        st.scroll = 0;
    } else if (st.selected >= count) {
        st.selected = static_cast<int16_t>(count - 1);
    }
    Focusable* f = add_focusable(w, id, full);
    const bool focused = ctx.is_active && w.focus == id;
    if (focused && count > 0) {
        if (st.selected < 0) st.selected = 0;
        if ((w.avail & platform::kButtonUp) && st.selected > 0) {
            w.avail &= ~platform::kButtonUp;
            --st.selected;
            ev = ListEvent::kChanged;
            w.snap = true;
        } else if ((w.avail & platform::kButtonDown) && st.selected < count - 1) {
            w.avail &= ~platform::kButtonDown;
            ++st.selected;
            ev = ListEvent::kChanged;
            w.snap = true;
        }
    }
    const bool over = contains(rows, g.cx, g.cy);
    const int row_under = over ? st.scroll + static_cast<int>((g.cy - rows.y) / kRowHeight) : -1;
    if (ctx.is_active && g.cursor_moved && over && row_under < count && row_under != st.selected) {
        st.selected = static_cast<int16_t>(row_under);
        ev = ListEvent::kChanged;
    }
    if (take_click(ctx, w, id)) {
        if (over && row_under < count) {
            if (row_under != st.selected) st.selected = static_cast<int16_t>(row_under);
            ev = ListEvent::kActivated;
            play_click();
        } else if (!over && st.selected >= 0) {
            ev = ListEvent::kActivated;  // клик по рамке/заголовку — как × по выделенному
            play_click();
        }
    }
    // Выделение — в видимой части.
    if (st.selected >= 0) {
        if (st.selected < st.scroll) st.scroll = st.selected;
        if (st.selected >= st.scroll + visible) st.scroll = static_cast<int16_t>(st.selected - visible + 1);
    }
    const int max_scroll = count > visible ? count - visible : 0;
    if (st.scroll > max_scroll) st.scroll = static_cast<int16_t>(max_scroll);
    if (st.scroll < 0) st.scroll = 0;
    if (f && st.selected >= 0) {
        f->snap_x = static_cast<int16_t>(rows.x + rows.w - 16);
        f->snap_y = static_cast<int16_t>(rows.y + (st.selected - st.scroll) * kRowHeight + kRowHeight / 2);
    }
    return ev;
}

constexpr int kScrollbarWidth = 8;

void draw_scrollbar(Rect area, int count, int visible, int scroll) {
    rect(area.x, area.y, area.w, area.h, kColLight);
    const int track = area.h;
    int thumb = track * visible / count;
    if (thumb < 6) thumb = 6;
    const int max_scroll = count - visible;
    const int y = area.y + (max_scroll > 0 ? (track - thumb) * scroll / max_scroll : 0);
    const Rect t{area.x, static_cast<int16_t>(y), area.w, static_cast<int16_t>(thumb)};
    rect(t.x, t.y, t.w, t.h, kColFace);
    bevel_abs(t, false);
}

// Фон строки: выделенная — акцент (фокус) или серо-синяя (без фокуса).
Color row_text_color(bool selected) { return selected ? kColTextInv : kColText; }

}  // namespace

ListEvent list(Context& ctx, WidgetId id, Rect r, ListState& st, int count, ItemTextFn text, void* user) {
    const Rect a = offset(ctx, r);
    rect(a.x, a.y, a.w, a.h, kColField);
    bevel_abs(a, true);
    Rect rows = inset(a, 2);
    const int visible = rows.h / kRowHeight > 0 ? rows.h / kRowHeight : 1;
    const bool bar = count > visible;
    if (bar) rows.w = static_cast<int16_t>(rows.w - kScrollbarWidth);
    const ListEvent ev = list_logic(ctx, id, a, rows, st, count, visible);
    const bool focused = ctx.is_active && win(ctx).focus == id;
    char buf[96];
    for (int i = 0; i < visible && st.scroll + i < count; ++i) {
        const int item = st.scroll + i;
        const int y = rows.y + i * kRowHeight;
        const bool sel = item == st.selected;
        if (sel) rect(rows.x, y, rows.w, kRowHeight, focused ? kColAccent : kColAccentDim);
        buf[0] = 0;
        const char* s = text ? text(item, buf, sizeof(buf), user) : "";
        text_in_rect({static_cast<int16_t>(rows.x + 3), static_cast<int16_t>(y), static_cast<int16_t>(rows.w - 5),
                      kRowHeight},
                     s, row_text_color(sel), Font::kUi12, Align::kLeft);
    }
    if (bar)
        draw_scrollbar({static_cast<int16_t>(rows.x + rows.w), rows.y, kScrollbarWidth, rows.h}, count, visible,
                       st.scroll);
    return ev;
}

ListEvent table(Context& ctx, WidgetId id, Rect r, ListState& st, const Column* columns, int column_count,
                int row_count, CellTextFn cell, void* user) {
    if (column_count > kMaxColumns) column_count = kMaxColumns;
    if (column_count < 0 || !columns) column_count = 0;
    const Rect a = offset(ctx, r);
    rect(a.x, a.y, a.w, a.h, kColField);
    bevel_abs(a, true);
    const Rect inner = inset(a, 2);
    Rect rows{inner.x, static_cast<int16_t>(inner.y + kRowHeight), inner.w,
              static_cast<int16_t>(inner.h - kRowHeight)};
    const int visible = rows.h / kRowHeight > 0 ? rows.h / kRowHeight : 1;
    const bool bar = row_count > visible;
    if (bar) rows.w = static_cast<int16_t>(rows.w - kScrollbarWidth);
    const ListEvent ev = list_logic(ctx, id, a, rows, st, row_count, visible);
    const bool focused = ctx.is_active && win(ctx).focus == id;

    // Границы столбцов.
    int16_t col_x[kMaxColumns + 1];
    int x = rows.x;
    for (int c = 0; c < column_count; ++c) {
        col_x[c] = static_cast<int16_t>(x);
        int cw = columns[c].width;
        if (c == column_count - 1 && cw <= 0) cw = rows.x + rows.w - x;
        x += cw;
        if (x > rows.x + rows.w) x = rows.x + rows.w;
    }
    col_x[column_count] = static_cast<int16_t>(x);

    // Заголовок
    for (int c = 0; c < column_count; ++c) {
        const Rect h{col_x[c], inner.y, static_cast<int16_t>(col_x[c + 1] - col_x[c]), kRowHeight};
        if (h.w <= 0) continue;
        rect(h.x, h.y, h.w, h.h, kColFace);
        bevel_abs(h, false);
        text_in_rect(inset(h, 2), columns[c].header ? columns[c].header : "", kColText, Font::kUi12,
                     columns[c].align);
    }
    if (bar) rect(rows.x + rows.w, inner.y, kScrollbarWidth, kRowHeight, kColFace);

    char buf[96];
    for (int i = 0; i < visible && st.scroll + i < row_count; ++i) {
        const int row = st.scroll + i;
        const int y = rows.y + i * kRowHeight;
        const bool sel = row == st.selected;
        if (sel) rect(rows.x, y, rows.w, kRowHeight, focused ? kColAccent : kColAccentDim);
        for (int c = 0; c < column_count; ++c) {
            const int cw = col_x[c + 1] - col_x[c];
            if (cw <= 6) continue;
            buf[0] = 0;
            const char* s = cell ? cell(row, c, buf, sizeof(buf), user) : "";
            text_in_rect({static_cast<int16_t>(col_x[c] + 3), static_cast<int16_t>(y), static_cast<int16_t>(cw - 6),
                          kRowHeight},
                         s, row_text_color(sel), Font::kUi12, columns[c].align);
        }
    }
    if (bar)
        draw_scrollbar({static_cast<int16_t>(rows.x + rows.w), rows.y, kScrollbarWidth, rows.h}, row_count, visible,
                       st.scroll);
    return ev;
}

}  // namespace ui
