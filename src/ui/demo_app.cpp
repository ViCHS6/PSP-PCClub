// Демо ОС (T030): все виджеты в одном окне и простое окно «О системе».
#include "ui/demo_app.h"

#include <cstdio>

namespace ui {
namespace {

enum : WidgetId {
    kAccept = 1,
    kCancel,
    kDisabled,
    kTariff,
    kPcList,
    kSessions,
    kAbout,
};

constexpr int kPcCount = 12;
constexpr int kStateCount = 4;
constexpr int kTariffCount = 3;
constexpr int kSessionCount = 10;

struct Strings {
    const char* accept;
    const char* cancel;
    const char* disabled;
    const char* about;
    const char* buttons;
    const char* clicks_fmt;
    const char* pc_fmt;
    const char* states[kStateCount];
    const char* tariffs[kTariffCount];
    const char* dust;
    const char* install_fmt;
    const char* col_seat;
    const char* col_game;
    const char* col_left;
};

struct DemoState {
    Strings s;
    int clicks = 0;
    int tariff = 0;
    float time = 0.0f;
    ListState pcs;
    ListState sessions;
    AppId about = kNoApp;
};

DemoState g_demo;

const char* pc_text(int i, char* buf, int cap, void* user) {
    const Strings& s = static_cast<DemoState*>(user)->s;
    std::snprintf(buf, static_cast<size_t>(cap), s.pc_fmt, i + 1, s.states[(i * 7 + 1) % kStateCount]);
    return buf;
}

const char* tariff_text(int i, char*, int, void* user) { return static_cast<DemoState*>(user)->s.tariffs[i]; }

const char* session_cell(int row, int col, char* buf, int cap, void* user) {
    const Context& ctx = *static_cast<const Context*>(user);
    switch (col) {
    case 0:
        std::snprintf(buf, static_cast<size_t>(cap), "%d", row + 1);
        return buf;
    case 1:
        if (ctx.data && ctx.data->game_count > 0)
            return core::data_string(*ctx.data, ctx.data->games[row % ctx.data->game_count].title);
        return "";
    default: {
        const int minutes = 15 + (row * 37) % 180;
        std::snprintf(buf, static_cast<size_t>(cap), "%d:%02d", minutes / 60, minutes % 60);
        return buf;
    }
    }
}

void demo_open(Context&, void* user) {
    DemoState& d = *static_cast<DemoState*>(user);
    Strings& s = d.s;
    s.accept = str("ui.demo.accept");
    s.cancel = str("ui.demo.cancel");
    s.disabled = str("ui.demo.disabled");
    s.about = str("ui.demo.about");
    s.buttons = str("ui.demo.buttons");
    s.clicks_fmt = str("ui.demo.clicks");
    s.pc_fmt = str("ui.demo.pc");
    static const char* const kStates[kStateCount] = {"ui.demo.pc_state.free", "ui.demo.pc_state.busy",
                                                     "ui.demo.pc_state.hung", "ui.demo.pc_state.off"};
    for (int i = 0; i < kStateCount; ++i) s.states[i] = str(kStates[i]);
    static const char* const kTariffs[kTariffCount] = {"ui.demo.tariff.hour", "ui.demo.tariff.three",
                                                       "ui.demo.tariff.night"};
    for (int i = 0; i < kTariffCount; ++i) s.tariffs[i] = str(kTariffs[i]);
    s.dust = str("ui.demo.dust");
    s.install_fmt = str("ui.demo.install");
    s.col_seat = str("ui.demo.col.seat");
    s.col_game = str("ui.demo.col.game");
    s.col_left = str("ui.demo.col.left");
    d.clicks = 0;
    d.time = 0.0f;
}

void demo_frame(Context& ctx, void* user) {
    DemoState& d = *static_cast<DemoState*>(user);
    const Strings& s = d.s;
    d.time += ctx.dt;

    group(ctx, {4, 2, 170, 62}, s.buttons);
    if (button(ctx, kAccept, {10, 16, 76, kButtonHeight}, s.accept)) ++d.clicks;
    if (button(ctx, kCancel, {92, 16, 76, kButtonHeight}, s.cancel)) --d.clicks;
    button(ctx, kDisabled, {10, 38, 76, kButtonHeight}, s.disabled, kWidgetDisabled);
    if (button(ctx, kAbout, {92, 38, 76, kButtonHeight}, s.about) && d.about != kNoApp) open_app(d.about);

    char buf[64];
    std::snprintf(buf, sizeof(buf), s.clicks_fmt, d.clicks);
    label(ctx, {182, 4, 100, 14}, buf);
    char money[24];
    format_money(d.clicks * 1500, money, sizeof(money));
    label(ctx, {282, 4, 74, 14}, money, d.clicks < 0 ? kColBad : kColText, Font::kUi12, Align::kRight);

    choice(ctx, kTariff, {180, 22, 176, kButtonHeight}, &d.tariff, kTariffCount, tariff_text, &d);

    const float t = d.time * 0.25f;
    const float dust = t - static_cast<float>(static_cast<int>(t));
    label(ctx, {180, 42, 40, 12}, s.dust);
    progress(ctx, {222, 42, 134, 12}, dust, dust > 0.8f ? kColBad : kColWarn);
    const int pct = static_cast<int>(dust * 100.0f);
    std::snprintf(buf, sizeof(buf), s.install_fmt, pct);
    progress(ctx, {180, 56, 176, 14}, dust, kColGood, buf);

    list(ctx, kPcList, {4, 72, 128, 130}, d.pcs, kPcCount, pc_text, &d);

    const Column cols[] = {{s.col_seat, 38, Align::kRight}, {s.col_game, 114, Align::kLeft},
                           {s.col_left, 0, Align::kRight}};
    table(ctx, kSessions, {136, 72, 220, 130}, d.sessions, cols, 3, kSessionCount, session_cell, &ctx);
}

// «О системе»: подпись, подсказка по управлению и кнопка «OK».
struct AboutState {
    const char* lines[4];
    const char* ok;
};
AboutState g_about;

void about_open(Context&, void* user) {
    AboutState& a = *static_cast<AboutState*>(user);
    a.lines[0] = str("ui.os.name");
    a.lines[1] = str("ui.about.version");
    a.lines[2] = str("ui.about.controls1");
    a.lines[3] = str("ui.about.controls2");
    a.ok = str("ui.about.ok");
}

void about_frame(Context& ctx, void* user) {
    const AboutState& a = *static_cast<AboutState*>(user);
    label(ctx, {6, 4, 248, 16}, a.lines[0], kColAccent, Font::kUi14Bold);
    label(ctx, {6, 20, 248, 14}, a.lines[1], kColTextDim);
    label(ctx, {6, 38, 248, 14}, a.lines[2]);
    label(ctx, {6, 52, 248, 14}, a.lines[3]);
    if (button(ctx, 1, {190, 72, 64, kButtonHeight}, a.ok)) close_app(ctx.app);
}

}  // namespace

AppId demo_register() {
    AppDesc about{};
    about.id = "about";
    about.title_key = "ui.about.title";
    about.icon_text = "ui.about.icon";
    about.icon_color = render::rgba(0x60, 0x60, 0x70);
    about.width = 260;
    about.height = 92;
    about.on_open = about_open;
    about.on_frame = about_frame;
    about.user = &g_about;
    g_demo.about = register_app(about);

    AppDesc demo{};
    demo.id = "demo";
    demo.title_key = "ui.demo.title";
    demo.icon_text = "ui.demo.icon";
    demo.icon_color = kColGood;
    demo.width = 360;
    demo.height = 206;
    demo.on_open = demo_open;
    demo.on_frame = demo_frame;
    demo.user = &g_demo;
    return register_app(demo);
}

}  // namespace ui
