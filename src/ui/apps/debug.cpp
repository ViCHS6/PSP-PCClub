// Программа ОС «Отладка» (DBG) — см. debug.h. Правила программ — docs/formats/ui.md §3.
#include "ui/apps/debug.h"

#include <cstdio>

#include "core/debug_cmds.h"

namespace ui {
namespace {

enum : WidgetId {
    kMoney1k = 1, kMoney10k, kToOpen, kToClose, kToMorning, kSpeed,
    kVisitor, kInstallAll, kPc, kBreak, kHang, kDust, kFix,
    kSave, kLoad, kErase, kEvent, kFire,
};

enum class Op : uint8_t { kNone, kSave, kLoad, kErase };

constexpr int kSpeeds[] = {1, 4, 16};
constexpr int kSpeedCount = sizeof(kSpeeds) / sizeof(kSpeeds[0]);
constexpr int kPhases = static_cast<int>(core::Phase::kGameOver) + 1;
constexpr int kResults = static_cast<int>(core::CmdResult::kLimit) + 1;

constexpr const char* kPhaseKeys[] = {"app.debug.phase.morning", "app.debug.phase.open", "app.debug.phase.closed",
                                      "app.debug.phase.night", "app.debug.phase.game_over"};
static_assert(sizeof(kPhaseKeys) / sizeof(kPhaseKeys[0]) == kPhases, "Phase → строка");
constexpr const char* kResultKeys[] = {
    "app.debug.result.ok",       "app.debug.result.invalid",  "app.debug.result.wrong_phase",
    "app.debug.result.no_money", "app.debug.result.no_stock", "app.debug.result.no_license",
    "app.debug.result.no_space", "app.debug.result.busy",     "app.debug.result.failed",
    "app.debug.result.limit"};
static_assert(sizeof(kResultKeys) / sizeof(kResultKeys[0]) == kResults, "CmdResult → строка");
constexpr const char* kSpeedKeys[] = {"app.debug.speed_1", "app.debug.speed_4", "app.debug.speed_16"};
static_assert(sizeof(kSpeedKeys) / sizeof(kSpeedKeys[0]) == kSpeedCount, "скорость → строка");

// Три столбца групп по 152 пикселя.
constexpr int kColW = 152, kBtnW = 136;
constexpr uint8_t kOff = kWidgetDisabled;  // uint8_t: в ?: с флагами без -Wextra
constexpr int16_t kCol[] = {4, 162, 320};

struct State {
    DebugHost host;
    int speed = 0;           // индекс в kSpeeds; переживает закрытие окна
    int pc = 0;
    int event = 0;
    bool erase_armed = false;  // «Стереть» — со второго нажатия
    Op pending = Op::kNone;
    const char* status = "";
    char status_buf[96]{};

    const char* no_world = "";
    const char* g_money = "";
    const char* g_time = "";
    const char* g_club = "";
    const char* g_pc = "";
    const char* g_save = "";
    const char* g_event = "";
    const char* money_1k = "";
    const char* money_10k = "";
    const char* to_open = "";
    const char* to_close = "";
    const char* to_morning = "";
    const char* speed_text[kSpeedCount]{};
    const char* visitor = "";
    const char* install_all = "";
    const char* pc_fmt = "";
    const char* brk = "";
    const char* hang = "";
    const char* dust = "";
    const char* fix = "";
    const char* save = "";
    const char* load = "";
    const char* erase = "";
    const char* erase_confirm = "";
    const char* fire = "";
    const char* no_events = "";
    const char* info_fmt = "";
    const char* phase[kPhases]{};
    const char* result[kResults]{};
    const char* installed_fmt = "";
    const char* fired_fmt = "";
    const char* working = "";
    const char* saved = "";
    const char* not_saved = "";
    const char* failed_code = "";
    const char* cant_save = "";
    const char* loaded_fmt = "";
    const char* not_loaded = "";
    const char* erased = "";
    const char* not_erased = "";
    const char* hint = "";
} g_state;

const char* speed_text(int i, char*, int, void*) { return g_state.speed_text[i]; }

const char* pc_text(int i, char* buf, int cap, void*) {
    std::snprintf(buf, static_cast<size_t>(cap), g_state.pc_fmt, i + 1);
    return buf;
}

const char* event_text(int i, char*, int, void* user) {
    const core::GameData& d = *static_cast<const Context*>(user)->data;
    return core::data_name(d, d.events[i].id);
}

void set_result(State& s, core::CmdResult r) { s.status = s.result[static_cast<int>(r)]; }

void set_status(State& s, const char* fmt, int value) {
    std::snprintf(s.status_buf, sizeof(s.status_buf), fmt, value);
    s.status = s.status_buf;
}

// Неудача записи/чтения/стирания: с сырым кодом носителя, если он есть.
void set_failed(State& s, const char* text) {
    const int code = s.host.last_code ? s.host.last_code() : 0;
    if (code == 0) {
        s.status = text;
        return;
    }
    std::snprintf(s.status_buf, sizeof(s.status_buf), s.failed_code, text, static_cast<unsigned>(code));
    s.status = s.status_buf;
}

void set_status(State& s, const char* fmt, const char* value) {
    std::snprintf(s.status_buf, sizeof(s.status_buf), fmt, value);
    s.status = s.status_buf;
}

Rect at(int col, int y, int w = kBtnW, int h = kButtonHeight) {
    return {static_cast<int16_t>(kCol[col] + (kColW - kBtnW) / 2), static_cast<int16_t>(y), static_cast<int16_t>(w),
            static_cast<int16_t>(h)};
}

void info_line(State& s, Context& ctx, const core::World& w) {
    char money[32];
    format_money(w.economy.money, money, sizeof(money));
    char buf[128];
    std::snprintf(buf, sizeof(buf), s.info_fmt, static_cast<int>(w.clock.day_index), w.clock.minute / 60,
                  w.clock.minute % 60, s.phase[static_cast<int>(w.clock.phase)], money, static_cast<int>(w.queue_len));
    label(ctx, {4, 180, 468, 14}, buf);
}

void on_open(Context&, void* user) {
    State& s = *static_cast<State*>(user);
    const DebugHost host = s.host;
    const int speed = s.speed;
    const Op pending = s.pending;
    s = State{};
    s.host = host;
    s.speed = speed;
    s.pending = pending;
    s.no_world = str("app.debug.no_world");
    s.g_money = str("app.debug.group.money");
    s.g_time = str("app.debug.group.time");
    s.g_club = str("app.debug.group.club");
    s.g_pc = str("app.debug.group.pc");
    s.g_save = str("app.debug.group.save");
    s.g_event = str("app.debug.group.event");
    s.money_1k = str("app.debug.money_1k");
    s.money_10k = str("app.debug.money_10k");
    s.to_open = str("app.debug.to_open");
    s.to_close = str("app.debug.to_close");
    s.to_morning = str("app.debug.to_morning");
    for (int i = 0; i < kSpeedCount; ++i) s.speed_text[i] = str(kSpeedKeys[i]);
    s.visitor = str("app.debug.visitor");
    s.install_all = str("app.debug.install_all");
    s.pc_fmt = str("app.debug.pc");
    s.brk = str("app.debug.break");
    s.hang = str("app.debug.hang");
    s.dust = str("app.debug.dust");
    s.fix = str("app.debug.fix");
    s.save = str("app.debug.save");
    s.load = str("app.debug.load");
    s.erase = str("app.debug.erase");
    s.erase_confirm = str("app.debug.erase_confirm");
    s.fire = str("app.debug.fire");
    s.no_events = str("app.debug.no_events");
    s.info_fmt = str("app.debug.info");
    for (int i = 0; i < kPhases; ++i) s.phase[i] = str(kPhaseKeys[i]);
    for (int i = 0; i < kResults; ++i) s.result[i] = str(kResultKeys[i]);
    s.installed_fmt = str("app.debug.status.installed");
    s.fired_fmt = str("app.debug.status.fired");
    s.working = str("app.debug.status.working");
    s.saved = str("app.debug.status.saved");
    s.not_saved = str("app.debug.status.not_saved");
    s.failed_code = str("app.debug.status.failed_code");
    s.cant_save = str("app.debug.status.cant_save");
    s.loaded_fmt = str("app.debug.status.loaded");
    s.not_loaded = str("app.debug.status.not_loaded");
    s.erased = str("app.debug.status.erased");
    s.not_erased = str("app.debug.status.not_erased");
    s.hint = str("app.debug.hint");
}

void time_group(State& s, Context& ctx, core::World& w) {
    group(ctx, {kCol[0], 62, kColW, 92}, s.g_time);
    if (button(ctx, kToOpen, at(0, 72), s.to_open)) set_result(s, core::debug_skip(w, core::DebugSkip::kToOpen));
    if (button(ctx, kToClose, at(0, 92), s.to_close)) set_result(s, core::debug_skip(w, core::DebugSkip::kToClose));
    if (button(ctx, kToMorning, at(0, 112), s.to_morning))
        set_result(s, core::debug_skip(w, core::DebugSkip::kToMorning));
    choice(ctx, kSpeed, at(0, 132), &s.speed, kSpeedCount, speed_text, nullptr);
}

void pc_group(State& s, Context& ctx, core::World& w) {
    group(ctx, {kCol[1], 62, kColW, 112}, s.g_pc);
    const int seats = w.club.seats < core::kMaxPcs ? w.club.seats : core::kMaxPcs;
    if (s.pc >= seats) s.pc = seats > 0 ? seats - 1 : 0;
    const uint8_t no = seats > 0 ? 0 : kOff;
    if (seats > 0) choice(ctx, kPc, at(1, 72), &s.pc, seats, pc_text, nullptr);
    const auto pc = static_cast<uint8_t>(s.pc);
    if (button(ctx, kBreak, at(1, 92), s.brk, no)) set_result(s, core::debug_break_pc(w, pc));
    if (button(ctx, kHang, at(1, 112), s.hang, no)) set_result(s, core::debug_hang_pc(w, pc));
    if (button(ctx, kDust, at(1, 132), s.dust, no)) set_result(s, core::debug_dust_pc(w, pc));
    if (button(ctx, kFix, at(1, 152), s.fix, no)) set_result(s, core::debug_fix_pc(w, pc));
}

void save_group(State& s, Context& ctx, const core::World& w) {
    group(ctx, {kCol[2], 4, kColW, 72}, s.g_save);
    const uint8_t busy = s.pending != Op::kNone ? kOff : 0;
    if (button(ctx, kSave, at(2, 14), s.save, s.host.save ? busy : kOff)) {
        if (!core::can_save(w)) {
            s.status = s.cant_save;
        } else {
            s.pending = Op::kSave;
            s.status = s.working;
        }
    }
    if (button(ctx, kLoad, at(2, 34), s.load, s.host.load ? busy : kOff)) {
        s.pending = Op::kLoad;
        s.status = s.working;
    }
    const bool armed = s.erase_armed;
    if (button(ctx, kErase, at(2, 54), armed ? s.erase_confirm : s.erase, s.host.erase ? busy : kOff)) {
        if (armed) {
            s.pending = Op::kErase;
            s.status = s.working;
        }
        s.erase_armed = !armed;
    }
}

void event_group(State& s, Context& ctx, core::World& w) {
    group(ctx, {kCol[2], 82, kColW, 52}, s.g_event);
    const int count = w.data->event_count;
    if (count == 0) {
        label(ctx, at(2, 92), s.no_events, kColTextDim);
        return;
    }
    if (s.event >= count) s.event = count - 1;
    choice(ctx, kEvent, at(2, 92), &s.event, count, event_text, &ctx);
    if (button(ctx, kFire, at(2, 112), s.fire)) {
        const core::CmdResult r = core::debug_fire_event(w, static_cast<core::Index>(s.event));
        if (r == core::CmdResult::kOk) set_status(s, s.fired_fmt, event_text(s.event, nullptr, 0, &ctx));
        else set_result(s, r);
    }
}

void on_frame(Context& ctx, void* user) {
    State& s = *static_cast<State*>(user);
    if (!ctx.world || !ctx.world->data) {
        label(ctx, {4, 4, 468, 16}, s.no_world, kColTextDim);
        return;
    }
    core::World& w = *ctx.world;

    group(ctx, {kCol[0], 4, kColW, 52}, s.g_money);
    if (button(ctx, kMoney1k, at(0, 14), s.money_1k)) set_result(s, core::debug_add_money(w, 1000));
    if (button(ctx, kMoney10k, at(0, 34), s.money_10k)) set_result(s, core::debug_add_money(w, 10000));
    time_group(s, ctx, w);

    group(ctx, {kCol[1], 4, kColW, 52}, s.g_club);
    if (button(ctx, kVisitor, at(1, 14), s.visitor)) set_result(s, core::debug_spawn_visitor(w));
    if (button(ctx, kInstallAll, at(1, 34), s.install_all)) set_status(s, s.installed_fmt, core::debug_install_all(w));
    pc_group(s, ctx, w);

    save_group(s, ctx, w);
    // Фокус ушёл с кнопки — «Точно стереть?» снимается.
    if (s.erase_armed && !is_focused(ctx, kErase)) s.erase_armed = false;
    event_group(s, ctx, w);

    info_line(s, ctx, w);
    label(ctx, {4, 196, 468, 14}, s.status);
    label(ctx, {4, 216, 468, 14}, s.hint, kColTextDim);
}

}  // namespace

AppId debug_register(const DebugHost& host) {
    g_state.host = host;
    AppDesc d{};
    d.id = "debug";
    d.title_key = "app.debug.title";
    d.icon_text = "app.debug.icon";
    d.icon_color = render::rgba(0x70, 0x70, 0x70);
    d.flags = kAppOnDesktop | kAppMaximized;
    d.on_open = on_open;
    d.on_frame = on_frame;
    d.user = &g_state;
    return register_app(d);
}

void debug_set_host(const DebugHost& host) { g_state.host = host; }

int debug_speed() { return kSpeeds[g_state.speed]; }

void debug_after_frame(core::World* world) {
    State& s = g_state;
    const Op op = s.pending;
    if (op == Op::kNone) return;
    s.pending = Op::kNone;
    if (!world || !world->data) return;
    switch (op) {
    case Op::kSave:
        if (s.host.save && s.host.save(*world)) s.status = s.saved;
        else set_failed(s, s.not_saved);
        break;
    case Op::kLoad:
        if (s.host.load && s.host.load(*world)) set_status(s, s.loaded_fmt, static_cast<int>(world->clock.day_index));
        else set_failed(s, s.not_loaded);
        break;
    case Op::kErase:
        if (s.host.erase && s.host.erase()) s.status = s.erased;
        else set_failed(s, s.not_erased);
        break;
    case Op::kNone: break;
    }
}

}  // namespace ui
