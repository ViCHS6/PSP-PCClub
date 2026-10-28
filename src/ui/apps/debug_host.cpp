// «Отладка» (DBG): сохранение через игру — game/autosave и platform/save_api.
#include "core/data_api.h"
#include "game/autosave.h"
#include "platform/save_api.h"
#include "ui/apps/debug.h"

namespace ui {
namespace {

bool save(const core::World& world) { return game::save_now(world, platform::SaveUi::kDialog); }

bool load(core::World& world) { return game::autosave_load(world, *world.data); }

// Удалять слот platform/save_api.h не умеет — пишем байты, которые load_world отвергнет
// (kTooSmall): при следующем запуске autosave_load вернёт false и начнётся новая игра.
bool erase() {
    static const uint8_t kEmpty[16] = {};
    platform::SaveMeta meta;
    meta.title = str("save.title");
    meta.detail = str("app.debug.erased_slot");
    return platform::save_write(kEmpty, sizeof(kEmpty), meta, platform::SaveUi::kDialog) == platform::SaveResult::kOk;
}

}  // namespace

DebugHost debug_game_host() {
    DebugHost h;
    h.save = save;
    h.load = load;
    h.erase = erase;
    return h;
}

}  // namespace ui
