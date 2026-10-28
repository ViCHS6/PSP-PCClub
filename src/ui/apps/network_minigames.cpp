// Сеть ↔ SDK мини-игр (T031): мини-игры обслуживания по id "svc_<игра>" (minigame.md §7).
#include "minigames/sdk.h"
#include "ui/apps/network.h"

namespace ui {
namespace {

constexpr const char* kIds[] = {"svc_reboot", "svc_clean", "svc_replace", "svc_crimp",
                                "svc_install", "svc_virus", "svc_network"};
static_assert(sizeof(kIds) / sizeof(kIds[0]) == core::kServiceGameCount, "ServiceGame → id");

bool g_launched = false;  // попытку запустила Сеть — результат её (Игры берут свои)

minigame::GameIndex find(core::ServiceGame game) {
    return game < core::kServiceGameCount ? minigame::find_game(kIds[game]) : minigame::kNoGame;
}

bool available(core::ServiceGame game) {
    const minigame::Desc* d = minigame::game(find(game));
    return d && (d->kinds & minigame::kService);
}

bool start(core::ServiceGame game, float difficulty, uint8_t variant, uint32_t seed) {
    if (!available(game) || minigame::running()) return false;
    minigame::StartParams p;
    p.seed = seed;
    p.difficulty = difficulty < 0.0f ? 0.0f : difficulty > 1.0f ? 1.0f : difficulty;
    p.kind = minigame::kService;
    p.variant = variant;
    g_launched = minigame::start(find(game), p);
    return g_launched;
}

bool finished(float* quality, bool* aborted) {
    if (!g_launched || minigame::running()) return false;
    g_launched = false;
    minigame::Result r;
    const bool has = minigame::take_result(&r);
    *aborted = !has || r.outcome == minigame::Outcome::kAborted;
    *quality = has && r.outcome == minigame::Outcome::kCompleted ? r.quality : 0.0f;
    return true;
}

}  // namespace

ServiceRunner network_minigame_runner() {
    ServiceRunner r;
    r.available = available;
    r.start = start;
    r.finished = finished;
    return r;
}

}  // namespace ui
