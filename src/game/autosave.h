#pragma once
// T060: автосохранение (GDD §8.8) — загрузка при старте, запись утром и при закрытии.
// Связывает core/save_api.h (байты мира) и platform/save_api.h (слот на носителе).
// Вызывать между кадрами (см. platform/save_api.h): на PSP запись блокирует на время утилиты.
//
//     core::world_init(world, data, seed);
//     const bool loaded = game::autosave_load(world, data);   // нет/битое сохранение — false
//     ...
//     game::autosave_update(world);          // каждый кадр ДО cmd_end_day в kClosed
#include "core/core_api.h"
#include "platform/save_api.h"

namespace game {

// Читает слот и восстанавливает мир. false — сохранения нет или оно не читается (причина —
// в stderr); world тогда не меняется. Запоминает текущий день: сразу после загрузки
// autosave_update не перезаписывает то же утро.
bool autosave_load(core::World& world, const core::GameData& data);

// Утро нового дня (kMorning) и закрытие (kClosed) — по одному сохранению на каждое.
void autosave_update(const core::World& world);

// Сохранить сейчас (меню «Сохранить» — ui = kDialog). Не can_save → false без записи.
bool save_now(const core::World& world, platform::SaveUi ui);

}  // namespace game
