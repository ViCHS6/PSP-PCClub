// Программа ОС «Игры» (T050). Правила программ — docs/formats/ui.md §3.
#include "minigames/games_app.h"

#include <cstdio>

#include "minigames/sdk.h"

namespace minigame {

namespace {

enum : ui::WidgetId { kIdList = 1, kIdPlay };

struct GamesState {
    ui::ListState list;
    GameIndex playable[kMaxGames];  // индексы реестра с kPlayable
    int count;
    bool launched;     // игру запустили отсюда — результат наш
    bool has_last;
    Result last;
    uint32_t seed;
};
GamesState g_games;

const char* g_play = "";
const char* g_last_fmt = "";
const char* g_outcome[3] = {"", "", ""};

const char* item_text(int i, char*, int, void* user) {
    const GamesState& s = *static_cast<const GamesState*>(user);
    return str(game(s.playable[i])->title_key);
}

void games_open(ui::Context&, void* user) {
    GamesState& s = *static_cast<GamesState*>(user);
    g_play = str("ui.games.play");
    g_last_fmt = str("ui.games.last");
    g_outcome[0] = str("ui.games.outcome.completed");
    g_outcome[1] = str("ui.games.outcome.failed");
    g_outcome[2] = str("ui.games.outcome.aborted");
    s.count = 0;
    for (int i = 0; i < game_count(); ++i)
        if (game(static_cast<GameIndex>(i))->kinds & kPlayable)
            s.playable[s.count++] = static_cast<GameIndex>(i);
}

void games_frame(ui::Context& ctx, void* user) {
    GamesState& s = *static_cast<GamesState*>(user);
    if (s.launched && !running()) {
        s.launched = false;
        s.has_last = take_result(&s.last);
    }
    const int w = ctx.client.w;
    const ui::ListEvent ev =
        ui::list(ctx, kIdList, {4, 4, static_cast<int16_t>(w - 8), 100}, s.list, s.count,
                 item_text, &s);
    const bool play = ui::button(ctx, kIdPlay, {4, 110, 90, ui::kButtonHeight}, g_play,
                                 s.list.selected < 0 ? ui::kWidgetDisabled : 0);
    if ((play || ev == ui::ListEvent::kActivated) && s.list.selected >= 0 && !running()) {
        StartParams p;
        p.seed = ++s.seed;
        p.kind = kPlayable;
        if (s.has_last && s.last.outcome == Outcome::kCompleted) {  // до T053 — лучший за сеанс
            p.has_best = 1;
            p.best_score = s.last.score;
        }
        s.launched = start(s.playable[s.list.selected], p);
    }
    if (s.has_last) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), g_last_fmt, g_outcome[static_cast<int>(s.last.outcome)],
                      static_cast<int>(s.last.score),
                      static_cast<int>(s.last.quality * 100.0f + 0.5f));
        ui::label(ctx, {4, 130, static_cast<int16_t>(w - 8), 14}, buf, ui::kColTextDim);
    }
}

}  // namespace

ui::AppId games_app_register() {
    ui::AppDesc d;
    d.id = "games";
    d.title_key = "ui.games.title";
    d.width = 220;
    d.height = 148;
    d.icon_text = "ui.games.icon";
    d.icon_color = ui::kColGood;
    d.on_open = games_open;
    d.on_frame = games_frame;
    d.user = &g_games;
    return ui::register_app(d);
}

}  // namespace minigame
