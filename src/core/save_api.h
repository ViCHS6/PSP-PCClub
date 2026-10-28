#pragma once
// КОНТРАКТ (T060). Сохранение мира: World ↔ байты файла сохранения. Формат —
// docs/formats/save.md, источник схемы — GDD §8.8. Реализация — src/core/save.cpp.
// Менять — только задачей «меняет контракт»; изменился формат → kSaveVersion + save.md.
//
// КТО ВЫЗЫВАЕТ
//   Игра (main/game): утром (kMorning) и при закрытии (kClosed) — save_world, затем байты
//   уходят в platform::save_write (platform/save_api.h). При старте — platform::save_read,
//   затем load_world. Тесты — что угодно. Ядро файлов не трогает.
//
// ПАМЯТЬ
//   Без аллокаций: байты — в буфере вызывающего (kSaveMaxSize хватает всегда). Внутри —
//   один статический World-черновик, поэтому функции не реентерабельны (один поток, как
//   всё ядро). Вызываются между тиками, не каждый кадр.
//
// ЧТО СОХРАНЯЕТСЯ (GDD §8.8)
//   Всё состояние World, кроме посетителей: они «уходят» в момент сохранения — очередь
//   пуста, занятые ПК свободны, постоянные клиенты не в клубе, у ActiveEvent нет посетителя.
//   Уведомления не сохраняются. ГПСЧ сохраняется: загрузка утром и дальнейшие тики дают
//   тот же мир, что без сохранения (бит в бит).
//
// ПРИМЕР
//
//     alignas(4) static uint8_t buf[core::kSaveMaxSize];
//     size_t size = 0;
//     if (core::save_world(world, buf, sizeof(buf), &size) == core::SaveError::kOk)
//         platform::save_write(buf, size, meta, platform::SaveUi::kSilent);
//     ...
//     if (platform::save_read(buf, sizeof(buf), &size, platform::SaveUi::kSilent) == platform::SaveResult::kOk &&
//         core::load_world(world, data, buf, size) == core::SaveError::kOk) { /* продолжаем */ }
//     else core::world_init(world, data, seed);
#include <cstddef>
#include <cstdint>

#include "core/core_api.h"

namespace core {

constexpr uint32_t kSaveMagic = fourcc('P', 'C', 'C', 'B');
constexpr uint16_t kSaveVersion = 1;
constexpr size_t kSaveMaxSize = 64 * 1024;  // GDD §8.8: «меньше 64 КБ»

enum class SaveError : uint8_t {
    kOk,
    kWrongPhase,    // save_world не в kMorning/kClosed (can_save == false)
    kNoSpace,       // буфер вызывающего меньше нужного
    kTooSmall,      // меньше заголовка
    kBadMagic,
    kBadVersion,    // version != kSaveVersion (чужая или будущая версия)
    kBadSize,       // file_size в заголовке не равен размеру данных или больше kSaveMaxSize
    kBadCrc,        // файл повреждён
    kBadSection,    // секции не в том порядке, выходят за файл, размер не сошёлся
    kDataMismatch,  // сохранение от другого data.pak (таблицы переставлены или укорочены)
    kBadValue,      // индекс или перечисление вне диапазона, фаза не kMorning/kClosed
};

// Короткое описание ошибки на латинице для лога/оверлея (не строка игрока).
const char* save_error_text(SaveError err);

// Сериализует world в dst. Только при can_save(world), иначе kWrongPhase.
// kOk — *out_size = число записанных байт (≤ kSaveMaxSize). При ошибке *out_size = 0.
// world не меняется (посетители «уходят» только в файле).
SaveError save_world(const World& world, void* dst, size_t capacity, size_t* out_size);

// Восстанавливает мир из байтов save_world. data — тот же data.pak, что при сохранении,
// или его продолжение: таблицы, дописанные в конец после сохранения, получают значения
// как в world_init. Проверяет всё (заголовок, CRC, секции, диапазоны) ДО изменения world:
// при ошибке world не меняется. kOk — world.data = &data, посетителей и уведомлений нет.
SaveError load_world(World& world, const GameData& data, const void* src, size_t size);

// Сводка для меню/диалога загрузки — из заголовка, без разбора секций (CRC проверяется).
struct SaveInfo {
    DayNum date = 0;
    int32_t day_index = 0;
    uint16_t minute = 0;
    Phase phase = Phase::kMorning;
};
SaveError save_peek(const void* src, size_t size, SaveInfo* out);

}  // namespace core
