// Программа ОС «Сеть» (T031). Правила программ — docs/formats/ui.md §3.
#include "ui/apps/network.h"

#include <cstdio>

namespace ui {
namespace {

enum : WidgetId { kReboot = 1, kClean, kPower, kInstall, kRepair, kGame, kDoInstall, kBack, kTileBase = 100 };

enum class Action : uint8_t { kNone, kReboot, kClean, kInstall, kRepair, kReplace };

constexpr int kPcStates = static_cast<int>(core::PcState::kBroken) + 1;
constexpr int kResults = static_cast<int>(core::CmdResult::kLimit) + 1;

// Ключи строк — по значению перечислений ядра; новое значение без строки не соберётся.
constexpr const char* kResultKeys[] = {
    "ui.network.result.ok",      "ui.network.result.invalid",    "ui.network.result.wrong_phase",
    "ui.network.result.no_money", "ui.network.result.no_stock",  "ui.network.result.no_license",
    "ui.network.result.no_space", "ui.network.result.busy",      "ui.network.result.failed",
    "ui.network.result.limit"};
static_assert(sizeof(kResultKeys) / sizeof(kResultKeys[0]) == kResults, "CmdResult → строка");
constexpr const char* kTileKeys[] = {"ui.network.tile.empty", "ui.network.tile.off", "ui.network.tile.idle",
                                     "ui.network.tile.busy", "ui.network.tile.hung", "ui.network.tile.broken"};
static_assert(sizeof(kTileKeys) / sizeof(kTileKeys[0]) == kPcStates, "PcState → строка");
constexpr const char* kStateKeys[] = {"ui.network.state.empty", "ui.network.state.off", "ui.network.state.idle",
                                      "ui.network.state.busy", "ui.network.state.hung", "ui.network.state.broken"};
static_assert(sizeof(kStateKeys) / sizeof(kStateKeys[0]) == kPcStates, "PcState → строка");
constexpr const char* kCompKeys[] = {"ui.network.comp.cpu", "ui.network.comp.gpu", "ui.network.comp.ram",
                                     "ui.network.comp.hdd", "ui.network.comp.psu", "ui.network.comp.monitor",
                                     "ui.network.comp.mouse", "ui.network.comp.keyboard",
                                     "ui.network.comp.headphones", "ui.network.comp.chair"};
static_assert(sizeof(kCompKeys) / sizeof(kCompKeys[0]) == core::kComponentCount, "Component → строка");

// Схема зала: 4 места в ряд, до 4 рядов (16 мест, GDD §5.3).
constexpr int kTileCols = 4;
constexpr int kTileW = 70, kTileH = 44, kTileGap = 4;
constexpr int kPanelX = 304, kPanelW = 168;

struct State {
    ServiceRunner runner;
    int selected = 0;
    bool installing = false;
    int game_choice = 0;
    core::Index installable[core::kMaxGames]{};
    int installable_count = 0;
    Action pending = Action::kNone;
    uint8_t pending_pc = 0;
    uint8_t pending_comp = 0;
    bool pending_pirate = false;
    core::Index pending_ref = core::kNone;
    uint32_t launches = 0;
    const char* status = "";
    char status_buf[96]{};

    const char* no_world = "";
    const char* pc_fmt = "";
    const char* tile[kPcStates]{};
    const char* state[kPcStates]{};
    const char* comp[core::kComponentCount]{};
    const char* broken_fmt = "";
    const char* who_fmt = "";
    const char* left_fmt = "";
    const char* dust_fmt = "";
    const char* wear_fmt = "";
    const char* wear_none = "";
    const char* disk_fmt = "";
    const char* virus = "";
    const char* reboot = "";
    const char* clean = "";
    const char* power_on = "";
    const char* power_off = "";
    const char* install = "";
    const char* repair = "";
    const char* replace = "";
    const char* install_title = "";
    const char* src_license = "";
    const char* src_pirate = "";
    const char* src_none = "";
    const char* size_fmt = "";
    const char* do_install = "";
    const char* back = "";
    const char* nothing = "";
    const char* no_game = "";
    const char* aborted = "";
    const char* result[kResults]{};
    const char* hint = "";
} g_state;

bool has_pirate(const core::World& w, core::Index g) { return (w.library.pirate[g / 32] >> (g % 32)) & 1u; }

// Детали на складе нужного типа для замены; kNone — нет.
core::Index stock_part(const core::World& w, uint8_t comp) {
    for (int h = 0; h < w.data->hardware_count; ++h)
        if (w.data->hardware[h].type == comp && w.inventory.hardware[h] > 0) return static_cast<core::Index>(h);
    return core::kNone;
}

int max_wear(const core::Pc& p, int* comp) {
    int best = 0;
    *comp = 0;
    for (int k = 0; k < core::kComponentCount; ++k) {
        if (p.component[k] == core::kNone) continue;
        const int wear = static_cast<int>(p.wear[k] + 0.5f);
        if (wear > best) { best = wear; *comp = k; }
    }
    return best;
}

const char* game_title(const core::World& w, core::Index g) {
    return core::data_string(*w.data, w.data->games[g].title);
}

void collect_installable(State& s, const core::World& w, uint8_t pc) {
    s.installable_count = 0;
    for (int g = 0; g < w.data->game_count; ++g) {
        const auto gi = static_cast<core::Index>(g);
        if (core::game_released(w, gi) && !core::pc_has_game(w, pc, gi)) s.installable[s.installable_count++] = gi;
    }
    s.game_choice = 0;
}

const char* installable_text(int i, char*, int, void* user) {
    return game_title(*static_cast<const Context*>(user)->world, g_state.installable[i]);
}

void set_status(State& s, const char* fmt, int value) {
    std::snprintf(s.status_buf, sizeof(s.status_buf), fmt, value);
    s.status = s.status_buf;
}

void launch(State& s, Action action, core::ServiceGame game, float difficulty, uint8_t pc, uint8_t variant = 0) {
    const uint32_t seed = 0x9E3779B9u * ++s.launches + pc;
    if (!s.runner.start || !s.runner.start(game, difficulty, variant, seed)) {
        s.status = s.no_game;
        return;
    }
    s.pending = action;
    s.pending_pc = pc;
    s.pending_comp = variant;
    s.status = "";
}

bool can_run(const State& s, core::ServiceGame game) { return s.runner.available && s.runner.available(game); }

// Мини-игра закончилась — применить её результат командой ядра.
void apply_pending(State& s, core::World& w) {
    float quality = 0.0f;
    bool aborted = false;
    if (s.pending == Action::kNone || !s.runner.finished || !s.runner.finished(&quality, &aborted)) return;
    const Action action = s.pending;
    s.pending = Action::kNone;
    if (aborted) {
        s.status = s.aborted;
        return;
    }
    const uint8_t pc = s.pending_pc;
    core::CmdResult r = core::CmdResult::kInvalid;
    switch (action) {
    case Action::kReboot: r = core::cmd_reboot(w, pc, quality); break;
    case Action::kClean: r = core::cmd_clean_dust(w, pc, quality); break;
    case Action::kInstall: r = core::cmd_install(w, pc, s.pending_ref, s.pending_pirate, quality); break;
    case Action::kRepair: r = core::cmd_repair(w, pc, static_cast<core::Component>(s.pending_comp), quality); break;
    case Action::kReplace: r = core::cmd_replace_component(w, pc, s.pending_ref, quality); break;
    case Action::kNone: return;
    }
    set_status(s, s.result[static_cast<int>(r)], static_cast<int>(quality * 100.0f + 0.5f));
}

Color tile_color(core::PcState st) {
    switch (st) {
    case core::PcState::kEmpty: return kColFace;
    case core::PcState::kOff: return kColDark;
    case core::PcState::kHung: return kColAccent;   // синий экран
    case core::PcState::kBroken: return kColBad;
    default: return kColField;
    }
}

void draw_tile(State& s, Context& ctx, const core::World& w, int i) {
    const core::Pc& p = w.pcs[i];
    const Rect r{static_cast<int16_t>(4 + (i % kTileCols) * (kTileW + kTileGap)),
                 static_cast<int16_t>(4 + (i / kTileCols) * (kTileH + kTileGap)), kTileW, kTileH};
    const WidgetId id = static_cast<WidgetId>(kTileBase + i);
    if (hotspot(ctx, id, r)) s.selected = i;  // выбор — только × по плитке или крестовиной (map_dpad)

    const bool dark = p.state == core::PcState::kOff || p.state == core::PcState::kHung ||
                      p.state == core::PcState::kBroken;
    const Color text = dark ? kColTextInv : p.state == core::PcState::kEmpty ? kColTextDim : kColText;
    fill(ctx, r, tile_color(p.state));
    if (p.state == core::PcState::kIdle || p.state == core::PcState::kBusy)  // полоска занятости
        fill(ctx, {r.x, r.y, r.w, 3}, p.state == core::PcState::kIdle ? kColGood : kColWarn);
    char buf[48];
    std::snprintf(buf, sizeof(buf), s.pc_fmt, i + 1);
    label(ctx, {static_cast<int16_t>(r.x + 3), static_cast<int16_t>(r.y + 4), 36, 12}, buf, text);
    const core::Visitor* v = p.visitor != core::kNoVisitor ? &w.visitors[p.visitor] : nullptr;
    if (v && p.state == core::PcState::kBusy) {
        std::snprintf(buf, sizeof(buf), "%d:%02d", v->paid_minutes_left / 60, v->paid_minutes_left % 60);
        label(ctx, {static_cast<int16_t>(r.x + 36), static_cast<int16_t>(r.y + 4), 31, 12}, buf, text,
              Font::kUi12, Align::kRight);
    }
    const char* line = s.tile[static_cast<int>(p.state)];
    if (p.state == core::PcState::kBusy && p.running_game != core::kNone) line = game_title(w, p.running_game);
    label(ctx, {static_cast<int16_t>(r.x + 3), static_cast<int16_t>(r.y + 18), kTileW - 6, 12}, line, text);
    if (p.state != core::PcState::kEmpty) {  // пыль
        const Rect bar{static_cast<int16_t>(r.x + 3), static_cast<int16_t>(r.y + r.h - 8), kTileW - 6, 4};
        fill(ctx, bar, kColShadow);
        const int w_dust = static_cast<int>(static_cast<float>(bar.w) * p.dust / 100.0f + 0.5f);
        if (w_dust > 0)
            fill(ctx, {bar.x, bar.y, static_cast<int16_t>(w_dust < bar.w ? w_dust : bar.w), bar.h},
                 p.dust >= 70.0f ? kColBad : p.dust >= 40.0f ? kColWarn : kColGood);
    }
    const bool focused = is_focused(ctx, id);
    if (focused || s.selected == i) {
        outline(ctx, r, focused ? kColAccent : kColAccentDim);
        outline(ctx, {static_cast<int16_t>(r.x + 1), static_cast<int16_t>(r.y + 1), static_cast<int16_t>(r.w - 2),
                      static_cast<int16_t>(r.h - 2)}, focused ? kColAccent : kColAccentDim);
    } else {
        outline(ctx, r, kColShadow);
    }
}

// Крестовина при фокусе на схеме: шаг по сетке мест и выбор этого ПК. Курсор выбор не меняет —
// иначе путь курсора к кнопкам панели сбивает ПК. С края схемы кнопка не берётся: фокус уйдёт к панели.
void map_dpad(State& s, Context& ctx, int seats) {
    int from = -1;
    for (int i = 0; i < seats && from < 0; ++i)
        if (is_focused(ctx, static_cast<WidgetId>(kTileBase + i))) from = i;
    if (from < 0) return;
    const uint32_t dpad = pressed(ctx);
    const int col = from % kTileCols;
    int to = -1;
    uint32_t button = 0;
    if ((dpad & platform::kButtonLeft) && col > 0) { to = from - 1; button = platform::kButtonLeft; }
    else if ((dpad & platform::kButtonRight) && col < kTileCols - 1 && from + 1 < seats) { to = from + 1; button = platform::kButtonRight; }
    else if ((dpad & platform::kButtonUp) && from >= kTileCols) { to = from - kTileCols; button = platform::kButtonUp; }
    else if ((dpad & platform::kButtonDown) && from + kTileCols < seats) { to = from + kTileCols; button = platform::kButtonDown; }
    if (to < 0) return;
    consume(ctx, button);
    s.selected = to;
    set_focus(ctx, static_cast<WidgetId>(kTileBase + to));
}

void install_panel(State& s, Context& ctx, core::World& w, uint8_t pc) {
    if (consume(ctx, platform::kButtonCircle)) {
        s.installing = false;
        return;
    }
    char buf[96];
    std::snprintf(buf, sizeof(buf), s.install_title, pc + 1);
    label(ctx, {kPanelX, 4, kPanelW, 16}, buf, kColText, Font::kUi14Bold);
    bool can = false;
    bool pirate = false;
    core::Index g = core::kNone;
    if (s.installable_count == 0) {
        label(ctx, {kPanelX, 22, kPanelW, 28}, s.nothing, kColTextDim);
    } else {
        if (s.game_choice >= s.installable_count) s.game_choice = s.installable_count - 1;
        choice(ctx, kGame, {kPanelX, 22, kPanelW, kButtonHeight}, &s.game_choice, s.installable_count,
               installable_text, &ctx);
        g = s.installable[s.game_choice];
        const int licenses = core::free_licenses(w, g);
        if (licenses > 0) {
            std::snprintf(buf, sizeof(buf), s.src_license, licenses);
            label(ctx, {kPanelX, 42, kPanelW, 14}, buf);
            can = true;
        } else if (has_pirate(w, g)) {
            label(ctx, {kPanelX, 42, kPanelW, 14}, s.src_pirate, kColWarn);
            can = pirate = true;
        } else {
            label(ctx, {kPanelX, 42, kPanelW, 14}, s.src_none, kColBad);
        }
        std::snprintf(buf, sizeof(buf), s.size_fmt, static_cast<int>(w.data->games[g].req_hdd_mb),
                      static_cast<int>(core::pc_free_hdd_mb(w, pc)));
        label(ctx, {kPanelX, 56, kPanelW, 14}, buf, kColTextDim);
    }
    can = can && can_run(s, core::kSvcInstall);
    if (button(ctx, kDoInstall, {kPanelX, 76, kPanelW, kButtonHeight}, s.do_install, can ? 0 : kWidgetDisabled)) {
        s.pending_ref = g;
        s.pending_pirate = pirate;
        s.installing = false;
        launch(s, Action::kInstall, core::kSvcInstall, 0.5f, pc);
    }
    if (button(ctx, kBack, {kPanelX, 96, kPanelW, kButtonHeight}, s.back)) s.installing = false;
}

void details_panel(State& s, Context& ctx, core::World& w, uint8_t pc) {
    const core::Pc& p = w.pcs[pc];
    char buf[96];
    std::snprintf(buf, sizeof(buf), s.pc_fmt, pc + 1);
    label(ctx, {kPanelX, 4, 48, 16}, buf, kColText, Font::kUi14Bold);
    if (p.state == core::PcState::kBroken && p.broken_component < core::kComponentCount) {
        std::snprintf(buf, sizeof(buf), s.broken_fmt, s.comp[p.broken_component]);
        label(ctx, {kPanelX + 52, 4, kPanelW - 52, 16}, buf, kColBad);
    } else {
        label(ctx, {kPanelX + 52, 4, kPanelW - 52, 16}, s.state[static_cast<int>(p.state)]);
    }
    int y = 22;
    if (p.visitor != core::kNoVisitor) {
        const core::Visitor& v = w.visitors[p.visitor];
        const core::GameData& d = *w.data;
        const char* who = v.regular != core::kNone ? core::data_string(d, d.regulars[v.regular].name)
                                                   : core::data_string(d, d.archetypes[v.archetype].name);
        std::snprintf(buf, sizeof(buf), s.who_fmt, who, v.game != core::kNone ? game_title(w, v.game) : "");
        label(ctx, {kPanelX, static_cast<int16_t>(y), kPanelW, 14}, buf);
        y += 14;
        std::snprintf(buf, sizeof(buf), s.left_fmt, v.paid_minutes_left / 60, v.paid_minutes_left % 60);
        label(ctx, {kPanelX, static_cast<int16_t>(y), kPanelW, 14}, buf);
        y += 14;
    }
    if (p.state != core::PcState::kEmpty) {
        std::snprintf(buf, sizeof(buf), s.dust_fmt, static_cast<int>(p.dust + 0.5f));
        label(ctx, {kPanelX, static_cast<int16_t>(y), kPanelW, 14}, buf, p.dust >= 70.0f ? kColBad : kColText);
        y += 14;
        int comp = 0;
        const int wear = max_wear(p, &comp);
        std::snprintf(buf, sizeof(buf), s.wear_fmt, wear, s.comp[comp]);
        label(ctx, {kPanelX, static_cast<int16_t>(y), kPanelW, 14}, wear > 0 ? buf : s.wear_none,
              wear >= 80 ? kColBad : kColText);
        y += 14;
        int games = 0;
        for (int g = 0; g < w.data->game_count; ++g)
            if (core::pc_has_game(w, pc, static_cast<core::Index>(g))) ++games;
        std::snprintf(buf, sizeof(buf), s.disk_fmt, static_cast<int>(core::pc_free_hdd_mb(w, pc)), games);
        label(ctx, {kPanelX, static_cast<int16_t>(y), kPanelW, 14}, buf, kColTextDim);
        y += 14;
        if (p.virus) label(ctx, {kPanelX, static_cast<int16_t>(y), kPanelW, 14}, s.virus, kColBad);
    }

    const bool busy = p.state == core::PcState::kBusy || p.state == core::PcState::kHung;
    const bool hung = p.state == core::PcState::kHung;
    if (button(ctx, kReboot, {kPanelX, 110, kPanelW, kButtonHeight}, s.reboot,
               hung && can_run(s, core::kSvcReboot) ? 0 : kWidgetDisabled))
        launch(s, Action::kReboot, core::kSvcReboot, 0.5f, pc);
    const bool cleanable = !busy && p.state != core::PcState::kEmpty && p.dust >= 1.0f;
    if (button(ctx, kClean, {kPanelX, 130, kPanelW, kButtonHeight}, s.clean,
               cleanable && can_run(s, core::kSvcClean) ? 0 : kWidgetDisabled))
        launch(s, Action::kClean, core::kSvcClean, p.dust / 100.0f, pc);
    const bool off = p.state == core::PcState::kOff;
    if (button(ctx, kPower, {kPanelX, 150, kPanelW, kButtonHeight}, off ? s.power_on : s.power_off,
               off || p.state == core::PcState::kIdle ? 0 : kWidgetDisabled)) {
        const core::CmdResult r = core::cmd_set_pc_power(w, pc, off);
        if (r != core::CmdResult::kOk) s.status = s.result[static_cast<int>(r)];
    }
    const bool installable = p.state != core::PcState::kEmpty && p.state != core::PcState::kBroken;
    if (button(ctx, kInstall, {kPanelX, 170, kPanelW, kButtonHeight}, s.install,
               installable && can_run(s, core::kSvcInstall) ? 0 : kWidgetDisabled)) {
        collect_installable(s, w, pc);
        s.installing = true;
        set_focus(ctx, s.installable_count > 0 ? kGame : kBack);
    }
    // Ремонт: износ < 100 — чиним деталь, иначе только замена со склада (§7.7).
    const bool broken = p.state == core::PcState::kBroken && p.broken_component < core::kComponentCount;
    const uint8_t comp = broken ? p.broken_component : 0;
    const bool worn_out = broken && p.wear[comp] >= 100.0f;
    const core::Index part = worn_out ? stock_part(w, comp) : core::kNone;
    const bool can_fix = broken && (!worn_out || part != core::kNone) && can_run(s, core::kSvcReplace);
    if (button(ctx, kRepair, {kPanelX, 190, kPanelW, kButtonHeight}, worn_out ? s.replace : s.repair,
               can_fix ? 0 : kWidgetDisabled)) {
        s.pending_ref = part;
        launch(s, worn_out ? Action::kReplace : Action::kRepair, core::kSvcReplace, p.wear[comp] / 100.0f, pc, comp);
    }
}

void on_open(Context&, void* user) {
    State& s = *static_cast<State*>(user);
    const ServiceRunner runner = s.runner;
    s = State{};
    s.runner = runner;
    s.no_world = str("ui.network.no_world");
    s.pc_fmt = str("ui.network.pc");
    for (int i = 0; i < kPcStates; ++i) {
        s.tile[i] = str(kTileKeys[i]);
        s.state[i] = str(kStateKeys[i]);
    }
    for (int i = 0; i < core::kComponentCount; ++i) s.comp[i] = str(kCompKeys[i]);
    s.broken_fmt = str("ui.network.broken");
    s.who_fmt = str("ui.network.who");
    s.left_fmt = str("ui.network.left");
    s.dust_fmt = str("ui.network.dust");
    s.wear_fmt = str("ui.network.wear");
    s.wear_none = str("ui.network.wear_none");
    s.disk_fmt = str("ui.network.disk");
    s.virus = str("ui.network.virus");
    s.reboot = str("ui.network.reboot");
    s.clean = str("ui.network.clean");
    s.power_on = str("ui.network.power_on");
    s.power_off = str("ui.network.power_off");
    s.install = str("ui.network.install");
    s.repair = str("ui.network.repair");
    s.replace = str("ui.network.replace");
    s.install_title = str("ui.network.install_title");
    s.src_license = str("ui.network.src_license");
    s.src_pirate = str("ui.network.src_pirate");
    s.src_none = str("ui.network.src_none");
    s.size_fmt = str("ui.network.size");
    s.do_install = str("ui.network.do_install");
    s.back = str("ui.network.back");
    s.nothing = str("ui.network.nothing");
    s.no_game = str("ui.network.no_game");
    s.aborted = str("ui.network.aborted");
    for (int i = 0; i < kResults; ++i) s.result[i] = str(kResultKeys[i]);
    s.hint = str("ui.network.hint");
}

void on_frame(Context& ctx, void* user) {
    State& s = *static_cast<State*>(user);
    if (!ctx.world || !ctx.world->data) {
        label(ctx, {4, 4, 468, 16}, s.no_world, kColTextDim);
        return;
    }
    core::World& w = *ctx.world;
    apply_pending(s, w);
    const int seats = w.club.seats < core::kMaxPcs ? w.club.seats : core::kMaxPcs;
    if (seats == 0) {
        label(ctx, {4, 4, 468, 16}, s.no_world, kColTextDim);
        return;
    }
    if (s.selected >= seats) s.selected = seats - 1;
    if (s.installing && (s.pending != Action::kNone)) s.installing = false;
    const int before = s.selected;
    map_dpad(s, ctx, seats);
    for (int i = 0; i < seats; ++i) draw_tile(s, ctx, w, i);
    if (s.selected != before) s.installing = false;  // сменили ПК — подменю установки закрыть
    const auto pc = static_cast<uint8_t>(s.selected);
    if (s.installing) install_panel(s, ctx, w, pc);
    else details_panel(s, ctx, w, pc);
    label(ctx, {4, 208, 468, 14}, s.status);
    label(ctx, {4, 222, 468, 14}, s.hint, kColTextDim);
}

}  // namespace

AppId network_register(const ServiceRunner& runner) {
    g_state.runner = runner;
    AppDesc d{};
    d.id = "network";
    d.title_key = "ui.network.title";
    d.icon_text = "ui.network.icon";
    d.icon_color = render::rgba(0x30, 0x90, 0x90);
    d.flags = kAppOnDesktop | kAppMaximized;
    d.on_open = on_open;
    d.on_frame = on_frame;
    d.user = &g_state;
    return register_app(d);
}

}  // namespace ui
