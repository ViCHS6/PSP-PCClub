// Программа ОС «Касса» (T031). Правила программ — docs/formats/ui.md §3.
#include "ui/apps/cashier.h"

#include <cstdio>

#include "core/formulas_api.h"

namespace ui {
namespace {

enum : WidgetId { kTariff = 1, kSeats, kServe, kRefuse, kSessions };

constexpr int kServeResults = static_cast<int>(core::ServeResult::kInvalid) + 1;
constexpr int kRequests = static_cast<int>(core::Request::kHelp) + 1;
constexpr int kPcStates = static_cast<int>(core::PcState::kBroken) + 1;

// Ключи строк — по значению перечислений ядра; новое значение без строки не соберётся.
constexpr const char* kServeKeys[] = {
    "ui.cashier.seated", "ui.cashier.seated_alt", "ui.cashier.left_no_game", "ui.cashier.left_price",
    "ui.cashier.wrong_pc", "ui.cashier.pc_unavailable", "ui.cashier.invalid"};
static_assert(sizeof(kServeKeys) / sizeof(kServeKeys[0]) == kServeResults, "ServeResult → строка");
constexpr const char* kRequestKeys[] = {"", "ui.cashier.req.hang", "ui.cashier.req.broken",
                                        "ui.cashier.req.food", "ui.cashier.req.game", "ui.cashier.req.help"};
static_assert(sizeof(kRequestKeys) / sizeof(kRequestKeys[0]) == kRequests, "Request → строка");
constexpr const char* kPcStateKeys[] = {"ui.cashier.pc.empty", "ui.cashier.pc.off", "ui.cashier.pc.idle",
                                        "ui.cashier.pc.busy", "ui.cashier.pc.hung", "ui.cashier.pc.broken"};
static_assert(sizeof(kPcStateKeys) / sizeof(kPcStateKeys[0]) == kPcStates, "PcState → строка");

struct State {
    int tariff = 0;                    // 0 — почасовой, 1 — ночь
    ListState seats, sessions;
    uint16_t serial = 0;               // кто у стойки: сменился — место и тариф по умолчанию
    uint8_t rows[core::kMaxPcs]{};     // сеансы: индексы посетителей по возрастанию места
    int row_count = 0;
    const char* status = "";
    char status_buf[128]{};

    const char* no_world = "";
    const char* queue_fmt = "";
    const char* empty = "";
    const char* closed = "";
    const char* want_fmt = "";
    const char* want_any = "";
    const char* patience = "";
    const char* tariff_fmt[2]{};
    const char* seat_fmt = "";
    const char* pc_state[kPcStates]{};
    const char* fit[3]{};
    const char* serve = "";
    const char* refuse = "";
    const char* serve_result[kServeResults]{};
    const char* night_money = "";
    const char* refused = "";
    const char* sessions_title = "";
    const char* col[4]{};
    const char* request[kRequests]{};
    const char* resolved = "";
    const char* no_stock = "";
    const char* req_elsewhere = "";
    const char* hint = "";
    const char* sessions_hint = "";
} g_state;

bool night_available(const core::World& w) {
    const core::GameData& d = *w.data;
    for (int u = 0; u < d.upgrade_count; ++u)
        if (d.upgrades[u].kind == core::kUpgNightTariff && core::upgrade_owned(w, static_cast<core::Index>(u)))
            return true;
    return false;
}

bool runs(const core::World& w, uint8_t pc, core::Index game) {
    return core::pc_has_game(w, pc, game) && core::formula::perf_runs(core::pc_perf(w, pc, game));
}

// Подходит ли место посетителю (GDD §7.3.1): 0 — есть его игра, 1 — только другая, 2 — ничего.
int fit_of(const core::World& w, const core::Visitor& v, uint8_t pc) {
    if (v.want_game != core::kNone && runs(w, pc, v.want_game)) return 0;
    for (int g = 0; g < w.data->game_count; ++g)
        if (runs(w, pc, static_cast<core::Index>(g))) return 1;
    return 2;
}

int desk_visitor(const core::World& w) { return w.queue_len > 0 ? w.queue[0] : -1; }

const char* who_of(const core::World& w, const core::Visitor& v) {
    const core::GameData& d = *w.data;
    if (v.regular != core::kNone) return core::data_string(d, d.regulars[v.regular].name);
    return core::data_string(d, d.archetypes[v.archetype].name);
}

const char* tariff_text(int i, char* buf, int cap, void* user) {
    const core::World& w = *static_cast<const Context*>(user)->world;
    char money[32];
    format_money(i == 0 ? w.economy.price_hour : w.economy.price_night, money, sizeof(money));
    std::snprintf(buf, static_cast<size_t>(cap), g_state.tariff_fmt[i], money);
    return buf;
}

const char* seat_text(int i, char* buf, int cap, void* user) {
    const core::World& w = *static_cast<const Context*>(user)->world;
    const core::Pc& p = w.pcs[i];
    const int vi = desk_visitor(w);
    const char* what = g_state.pc_state[static_cast<int>(p.state)];
    if (p.state == core::PcState::kIdle && vi >= 0)
        what = g_state.fit[fit_of(w, w.visitors[vi], static_cast<uint8_t>(i))];
    std::snprintf(buf, static_cast<size_t>(cap), g_state.seat_fmt, i + 1, what);
    return buf;
}

const char* session_cell(int row, int col, char* buf, int cap, void* user) {
    const core::World& w = *static_cast<const Context*>(user)->world;
    const core::Visitor& v = w.visitors[g_state.rows[row]];
    switch (col) {
    case 0: std::snprintf(buf, static_cast<size_t>(cap), "%d", v.pc + 1); return buf;
    case 1: return v.game == core::kNone ? "" : core::data_string(*w.data, w.data->games[v.game].title);
    case 2:
        std::snprintf(buf, static_cast<size_t>(cap), "%d:%02d", v.paid_minutes_left / 60, v.paid_minutes_left % 60);
        return buf;
    default: return g_state.request[static_cast<int>(v.request)];
    }
}

// Сеансы — посетители на местах, по номеру места.
void collect_sessions(State& s, const core::World& w) {
    s.row_count = 0;
    for (int pc = 0; pc < w.club.seats && pc < core::kMaxPcs; ++pc)
        for (int i = 0; i < core::kMaxVisitors; ++i) {
            const core::Visitor& v = w.visitors[i];
            if (v.pc == pc && (v.state == core::VisitorState::kToSeat || v.state == core::VisitorState::kPlaying)) {
                s.rows[s.row_count++] = static_cast<uint8_t>(i);
                break;
            }
        }
}

void serve(State& s, core::World& w, uint8_t vi) {
    if (s.seats.selected < 0 || s.seats.selected >= w.club.seats) return;
    const auto pc = static_cast<uint8_t>(s.seats.selected);
    const bool night = s.tariff == 1;
    if (night && w.visitors[vi].money < w.economy.price_night) {
        s.status = s.night_money;
        return;
    }
    const core::ServeResult r = core::cmd_serve(w, vi, pc, night ? core::Tariff::kNight : core::Tariff::kHourly);
    const char* fmt = s.serve_result[static_cast<int>(r)];
    s.status = s.status_buf;
    if (r == core::ServeResult::kSeated || r == core::ServeResult::kSeatedAlt) {
        char money[32];
        format_money(w.visitors[vi].paid_total, money, sizeof(money));
        std::snprintf(s.status_buf, sizeof(s.status_buf), fmt, pc + 1, money);
    } else if (r == core::ServeResult::kWrongPc) {
        int other = 0;
        for (int o = 0; o < w.club.seats; ++o)
            if (o != pc && w.pcs[o].state == core::PcState::kIdle && runs(w, static_cast<uint8_t>(o), w.visitors[vi].want_game)) {
                other = o;
                break;
            }
        std::snprintf(s.status_buf, sizeof(s.status_buf), fmt, other + 1);
    } else {
        s.status = fmt;
    }
}

void refuse(State& s, core::World& w, uint8_t vi) {
    s.status = core::cmd_refuse(w, vi) == core::CmdResult::kOk ? s.refused : s.serve_result[kServeResults - 1];
}

void session_activated(State& s, core::World& w) {
    if (s.sessions.selected < 0 || s.sessions.selected >= s.row_count) return;
    const uint8_t vi = s.rows[s.sessions.selected];
    const core::Request r = w.visitors[vi].request;
    if (r == core::Request::kNone) return;
    if (r != core::Request::kFood && r != core::Request::kHelp) {
        s.status = s.req_elsewhere;
        return;
    }
    s.status = core::cmd_resolve_request(w, vi) == core::CmdResult::kOk ? s.resolved : s.no_stock;
}

void on_open(Context&, void* user) {
    State& s = *static_cast<State*>(user);
    s = State{};
    s.no_world = str("ui.cashier.no_world");
    s.queue_fmt = str("ui.cashier.queue");
    s.empty = str("ui.cashier.empty");
    s.closed = str("ui.cashier.closed");
    s.want_fmt = str("ui.cashier.want");
    s.want_any = str("ui.cashier.want_any");
    s.patience = str("ui.cashier.patience");
    s.tariff_fmt[0] = str("ui.cashier.tariff_hour");
    s.tariff_fmt[1] = str("ui.cashier.tariff_night");
    s.seat_fmt = str("ui.cashier.seat");
    for (int i = 0; i < kPcStates; ++i) s.pc_state[i] = str(kPcStateKeys[i]);
    s.fit[0] = str("ui.cashier.fit.want");
    s.fit[1] = str("ui.cashier.fit.alt");
    s.fit[2] = str("ui.cashier.fit.none");
    s.serve = str("ui.cashier.serve");
    s.refuse = str("ui.cashier.refuse");
    for (int i = 0; i < kServeResults; ++i) s.serve_result[i] = str(kServeKeys[i]);
    s.night_money = str("ui.cashier.night_money");
    s.refused = str("ui.cashier.refused");
    s.sessions_title = str("ui.cashier.sessions");
    s.col[0] = str("ui.cashier.col.pc");
    s.col[1] = str("ui.cashier.col.game");
    s.col[2] = str("ui.cashier.col.left");
    s.col[3] = str("ui.cashier.col.note");
    s.request[0] = "";
    for (int i = 1; i < kRequests; ++i) s.request[i] = str(kRequestKeys[i]);
    s.resolved = str("ui.cashier.resolved");
    s.no_stock = str("ui.cashier.no_stock");
    s.req_elsewhere = str("ui.cashier.req_elsewhere");
    s.hint = str("ui.cashier.hint");
    s.sessions_hint = str("ui.cashier.sessions_hint");
}

void on_frame(Context& ctx, void* user) {
    State& s = *static_cast<State*>(user);
    if (!ctx.world || !ctx.world->data) {
        label(ctx, {4, 4, 468, 16}, s.no_world, kColTextDim);
        return;
    }
    core::World& w = *ctx.world;
    const int vi = desk_visitor(w);
    const core::Visitor* v = vi >= 0 ? &w.visitors[vi] : nullptr;
    const bool night_ok = night_available(w);
    if (!night_ok) s.tariff = 0;
    if (v && v->serial != s.serial) {  // новый посетитель у стойки: лучшее место, почасовой
        s.serial = v->serial;
        s.tariff = 0;
        const uint8_t best = core::best_pc_for(w, static_cast<uint8_t>(vi));
        s.seats.selected = static_cast<int16_t>(best == core::kNoPc ? 0 : best);
        set_focus(ctx, kSeats);
    }

    // Левая колонка — посетитель у стойки.
    char buf[128];
    std::snprintf(buf, sizeof(buf), s.queue_fmt, static_cast<int>(w.queue_len));
    label(ctx, {164, 4, 72, 16}, buf, kColTextDim, Font::kUi12, Align::kRight);
    if (v) {
        label(ctx, {4, 4, 156, 16}, who_of(w, *v), kColText, Font::kUi14Bold);
        if (v->want_game != core::kNone) {
            std::snprintf(buf, sizeof(buf), s.want_fmt, core::data_string(*w.data, w.data->games[v->want_game].title));
            label(ctx, {4, 22, 232, 14}, buf);
        } else {
            label(ctx, {4, 22, 232, 14}, s.want_any);
        }
        label(ctx, {4, 36, 80, 14}, s.patience, kColTextDim);
        const float max_patience = static_cast<float>(w.data->archetypes[v->archetype].patience_max);
        const float left = max_patience > 0.0f ? static_cast<float>(v->patience) / max_patience : 0.0f;
        progress(ctx, {88, 38, 148, 10}, left, left > 0.5f ? kColGood : left > 0.25f ? kColWarn : kColBad);
    } else {
        label(ctx, {4, 4, 156, 16}, w.clock.phase == core::Phase::kOpen ? s.empty : s.closed, kColTextDim);
    }

    // Быстрый сценарий: со списка мест ←/→ меняют тариф, □ — отказ.
    if (v && is_focused(ctx, kSeats)) {
        const uint32_t lr = consume(ctx, platform::kButtonLeft | platform::kButtonRight);
        if (lr & platform::kButtonLeft) s.tariff = 0;
        if ((lr & platform::kButtonRight) && night_ok) s.tariff = 1;
    }
    choice(ctx, kTariff, {4, 52, 232, kButtonHeight}, &s.tariff, night_ok ? 2 : 1, tariff_text, &ctx,
           v ? 0 : kWidgetDisabled);
    const ListEvent seat_event = list(ctx, kSeats, {4, 72, 232, 112}, s.seats, w.club.seats, seat_text, &ctx);
    const bool can_serve = v && w.clock.phase == core::Phase::kOpen && s.seats.selected >= 0;
    const bool serve_pressed = button(ctx, kServe, {4, 188, 114, kButtonHeight}, s.serve, can_serve ? 0 : kWidgetDisabled);
    const bool refuse_pressed = button(ctx, kRefuse, {122, 188, 114, kButtonHeight}, s.refuse, v ? 0 : kWidgetDisabled);
    const bool square = v && consume(ctx, platform::kButtonSquare) != 0;
    if (can_serve && (serve_pressed || seat_event == ListEvent::kActivated)) serve(s, w, static_cast<uint8_t>(vi));
    else if (v && (refuse_pressed || square)) refuse(s, w, static_cast<uint8_t>(vi));

    // Правая колонка — сеансы.
    label(ctx, {244, 4, 228, 16}, s.sessions_title);
    collect_sessions(s, w);
    const Column cols[] = {{s.col[0], 24, Align::kRight}, {s.col[1], 104, Align::kLeft},
                           {s.col[2], 40, Align::kRight}, {s.col[3], 0, Align::kLeft}};
    if (table(ctx, kSessions, {244, 22, 228, 162}, s.sessions, cols, 4, s.row_count, session_cell, &ctx) ==
        ListEvent::kActivated)
        session_activated(s, w);
    label(ctx, {244, 188, 228, 16}, s.sessions_hint, kColTextDim);

    label(ctx, {4, 208, 468, 14}, s.status);
    label(ctx, {4, 222, 468, 14}, s.hint, kColTextDim);
}

}  // namespace

AppId cashier_register() {
    AppDesc d{};
    d.id = "cashier";
    d.title_key = "ui.cashier.title";
    d.icon_text = "ui.cashier.icon";
    d.icon_color = render::rgba(0xC0, 0x80, 0x20);
    d.flags = kAppOnDesktop | kAppMaximized | kAppNoClose;  // ○ — выход из ОС (game/admin_desk.h)
    d.on_open = on_open;
    d.on_frame = on_frame;
    d.user = &g_state;
    return register_app(d);
}

}  // namespace ui
