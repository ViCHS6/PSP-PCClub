#include "ui/apps/records.h"

#include <cstdio>

namespace ui {
namespace {
enum : WidgetId { kPage = 1, kGame, kBoard };
struct State {
    int page = 0, game = 0;
    ListState board;
    const char* pages[2]{};
    const char* unavailable = nullptr;
    const char* no_games = nullptr;
    const char* empty = nullptr;
    const char* tournaments = nullptr;
    const char* tournament_hint = nullptr;
    const char* player = nullptr;
    const char* visitor = nullptr;
    const char* higher = nullptr;
    const char* lower = nullptr;
    const char* date_fmt = nullptr;
    const char* columns[4]{};
} g_state;

const char* page_text(int i, char*, int, void* user) { return static_cast<State*>(user)->pages[i]; }
const char* game_text(int i, char*, int, void* user) {
    const core::GameData& d = *static_cast<Context*>(user)->world->data;
    return core::data_string(d, d.minigames[i].name);
}

const char* record_cell(int row, int col, char* buf, int cap, void* user) {
    const Context& ctx = *static_cast<Context*>(user);
    const core::RecordEntry& entry = ctx.world->records[g_state.game].top[row];
    const core::GameData& d = *ctx.world->data;
    switch (col) {
    case 0: std::snprintf(buf, static_cast<size_t>(cap), "%d", row + 1); break;
    case 1:
        if (entry.who == core::kRecordPlayer) return g_state.player;
        if (entry.who < d.regular_count) return core::data_string(d, d.regulars[entry.who].name);
        if (entry.archetype < d.archetype_count) return core::data_string(d, d.archetypes[entry.archetype].name);
        return g_state.visitor;
    case 2: std::snprintf(buf, static_cast<size_t>(cap), "%ld", static_cast<long>(entry.score)); break;
    default: {
        const core::Ymd date = core::ymd_from_date(entry.date);
        std::snprintf(buf, static_cast<size_t>(cap), g_state.date_fmt, date.day, date.month, date.year);
        break;
    }
    }
    return buf;
}

void on_open(Context&, void* user) {
    State& s = *static_cast<State*>(user);
    s = State{};
    s.pages[0] = str("os.records.board");
    s.pages[1] = str("os.records.tournaments");
    s.unavailable = str("os.records.unavailable");
    s.no_games = str("os.records.no_games");
    s.empty = str("os.records.empty");
    s.tournaments = str("os.records.no_tournaments");
    s.tournament_hint = str("os.records.tournament_hint");
    s.player = str("os.records.player");
    s.visitor = str("os.records.visitor");
    s.higher = str("os.records.higher");
    s.lower = str("os.records.lower");
    s.date_fmt = str("os.records.date");
    const char* columns[] = {"os.records.rank", "os.records.name", "os.records.score", "os.records.date_title"};
    for (int i = 0; i < 4; ++i) s.columns[i] = str(columns[i]);
}

void on_frame(Context& ctx, void* user) {
    State& s = *static_cast<State*>(user);
    choice(ctx, kPage, {4, 4, 468, 16}, &s.page, 2, page_text, &s);
    if (!ctx.world || !ctx.world->data) { label(ctx, {4, 30, 468, 28}, s.unavailable); return; }
    if (s.page == 1) {
        label(ctx, {4, 38, 468, 28}, s.tournaments, kColTextDim);
        label(ctx, {4, 76, 468, 28}, s.tournament_hint, kColTextDim);
        return;
    }
    const core::GameData& d = *ctx.world->data;
    if (d.minigame_count == 0) { label(ctx, {4, 36, 468, 28}, s.no_games, kColTextDim); return; }
    if (s.game >= d.minigame_count) { s.game = 0; s.board = ListState{}; }
    if (choice(ctx, kGame, {4, 28, 468, 16}, &s.game, d.minigame_count, game_text, &ctx))
        s.board = ListState{};
    const Column cols[] = {{s.columns[0], 30, Align::kRight}, {s.columns[1], 220, Align::kLeft},
                           {s.columns[2], 100, Align::kRight}, {s.columns[3], 0, Align::kRight}};
    const core::RecordBoard& board = ctx.world->records[s.game];
    table(ctx, kBoard, {4, 52, 468, 158}, s.board, cols, 4, board.count, record_cell, &ctx);
    if (board.count == 0) label(ctx, {8, 88, 456, 28}, s.empty, kColTextDim);
    label(ctx, {4, 216, 468, 16}, d.minigames[s.game].invert ? s.lower : s.higher, kColTextDim);
}
}  // namespace

AppId records_register() {
    AppDesc d{};
    d.id = "records";
    d.title_key = "os.records.title";
    d.icon_text = "os.records.icon";
    d.icon_color = kColBad;
    d.flags = kAppOnDesktop | kAppMaximized;
    d.on_open = on_open;
    d.on_frame = on_frame;
    d.user = &g_state;
    return register_app(d);
}
}  // namespace ui
