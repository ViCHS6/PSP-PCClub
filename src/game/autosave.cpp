// T060: автосохранение — см. autosave.h.
#include "game/autosave.h"

#include <cstdio>

#include "core/data_api.h"
#include "core/save_api.h"

namespace game {

namespace {

static_assert(core::kSaveMaxSize <= platform::kSaveCapacity, "слот меньше сохранения");

alignas(4) uint8_t g_buf[core::kSaveMaxSize];
int32_t g_morning_day = -1;  // день, утро которого уже сохранено
int32_t g_closed_day = -1;   // день, закрытие которого уже сохранено

void remember(const core::World& world) {
    if (world.clock.phase == core::Phase::kMorning) g_morning_day = world.clock.day_index;
    if (world.clock.phase == core::Phase::kClosed) g_closed_day = world.clock.day_index;
}

}  // namespace

bool autosave_load(core::World& world, const core::GameData& data) {
    size_t size = 0;
    bool ok = false;
    const platform::SaveResult r = platform::save_read(g_buf, sizeof(g_buf), &size, platform::SaveUi::kSilent);
    if (r == platform::SaveResult::kOk) {
        const core::SaveError err = core::load_world(world, data, g_buf, size);
        ok = err == core::SaveError::kOk;
        if (!ok) std::fprintf(stderr, "save: %s\n", core::save_error_text(err));
    } else if (r != platform::SaveResult::kNoSave) {
        std::fprintf(stderr, "save: read failed (%d)\n", static_cast<int>(r));
    }
    remember(world);
    return ok;
}

bool save_now(const core::World& world, platform::SaveUi ui) {
    size_t size = 0;
    const core::SaveError err = core::save_world(world, g_buf, sizeof(g_buf), &size);
    if (err != core::SaveError::kOk) {
        std::fprintf(stderr, "save: %s\n", core::save_error_text(err));
        return false;
    }
    // Подписи слота в системном списке PSP (strings_ru.json).
    const core::GameData& data = *world.data;
    const core::Ymd ymd = core::ymd_from_date(world.clock.date);
    char detail[128];
    std::snprintf(detail, sizeof(detail), core::data_string(data, core::data_find_string(data, "save.detail")),
                  static_cast<int>(world.clock.day_index), ymd.day, ymd.month, ymd.year,
                  static_cast<int>(world.economy.money));
    platform::SaveMeta meta;
    meta.title = core::data_string(data, core::data_find_string(data, "save.title"));
    meta.detail = detail;
    const platform::SaveResult r = platform::save_write(g_buf, size, meta, ui);
    if (r != platform::SaveResult::kOk) {
        if (r != platform::SaveResult::kCancelled) std::fprintf(stderr, "save: write failed (%d)\n", static_cast<int>(r));
        return false;
    }
    remember(world);
    return true;
}

void autosave_update(const core::World& world) {
    const core::Clock& c = world.clock;
    if ((c.phase == core::Phase::kMorning && c.day_index != g_morning_day) ||
        (c.phase == core::Phase::kClosed && c.day_index != g_closed_day)) {
        // Не вышло — не повторяем каждый кадр: следующая попытка в следующую точку сохранения.
        if (!save_now(world, platform::SaveUi::kSilent)) remember(world);
    }
}

}  // namespace game
