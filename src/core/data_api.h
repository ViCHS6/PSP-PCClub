#pragma once
// КОНТРАКТ (T020). Доступ к данным игры из data.pak (формат — docs/formats/data_pak.md,
// записи — core/data_types.h). Реализация — src/core/data.cpp (T022).
//
// Владение памятью: ядро НЕ читает файлы и НЕ выделяет память. Вызывающий (игра — через
// platform::file_read, balance-runner и тесты — как удобно) загружает data.pak целиком
// в свой буфер, выровненный на 16 байт, и отдаёт его data_load(). GameData хранит указатели
// внутрь этого буфера: буфер должен жить, пока жив GameData и все World, созданные из него.
// Буфер только читается.
//
// Вызывается при загрузке, не в игровом цикле. Все функции, кроме data_load, — O(1),
// кроме data_find_* (поиск; для инструментов, тестов и кэширования при загрузке).
//
//     alignas(16) static uint8_t pak[1 << 20];       // GDD §10.3: data.pak ≤ 1 МиБ
//     size_t size = 0;
//     platform::file_read("data.pak", pak, sizeof(pak), &size);
//     static core::GameData data;
//     const core::DataError err = core::data_load(pak, size, &data);
//     if (err != core::DataError::kOk) { /* показать core::data_error_text(err), выйти */ }
//     const char* title = core::data_string(data, data.games[0].title);
#include <cstddef>
#include <cstdint>

#include "core/data_types.h"

namespace core {

// Представление data.pak: массивы записей прямо в буфере файла.
// Пустая таблица — указатель nullptr и счётчик 0.
struct GameData {
    const GameRec* games = nullptr;
    const ArchetypeRec* archetypes = nullptr;
    const RegularRec* regulars = nullptr;
    const HardwareRec* hardware = nullptr;
    const GoodsRec* goods = nullptr;
    const UpgradeRec* upgrades = nullptr;
    const MinigameRec* minigames = nullptr;
    const EventRec* events = nullptr;
    const ChoiceRec* choices = nullptr;
    const EffectRec* effects = nullptr;
    const CondRec* conds = nullptr;
    const ChanceTermRec* chance_terms = nullptr;
    const uint16_t* lists = nullptr;      // общие списки индексов (event_pool, start_upgrades)
    const PeriodRec* periods = nullptr;
    const MonthDay* holidays = nullptr;
    const NameId* flags = nullptr;        // имя флага по его индексу
    const EconomyRec* economy = nullptr;  // всегда не nullptr после успешной загрузки

    uint16_t game_count = 0;
    uint16_t archetype_count = 0;
    uint16_t regular_count = 0;
    uint16_t hardware_count = 0;
    uint16_t goods_count = 0;
    uint16_t upgrade_count = 0;
    uint16_t minigame_count = 0;
    uint16_t event_count = 0;
    uint16_t choice_count = 0;
    uint16_t effect_count = 0;
    uint16_t cond_count = 0;
    uint16_t chance_term_count = 0;
    uint16_t list_count = 0;
    uint16_t period_count = 0;
    uint16_t holiday_count = 0;
    uint16_t flag_count = 0;

    // Строковые секции — внутреннее устройство для data_string/data_name.
    const uint8_t* strings = nullptr;  // начало секции STRS
    const uint8_t* names = nullptr;    // начало секции NAME
    const StringKeyRec* string_keys = nullptr;
    uint16_t string_count = 0;
    uint16_t name_count = 0;
    uint16_t string_key_count = 0;
};

enum class DataError : uint8_t {
    kOk,
    kTooSmall,         // размер меньше заголовка или file_size не совпадает
    kBadMagic,
    kBadVersion,       // version != kPakVersion
    kBadCrc,
    kBadSection,       // секция выходит за файл, не выровнена или rec_size != sizeof(T)
    kMissingSection,   // нет обязательной секции (ECON, STRS, NAME, STRK)
    kLimitExceeded,    // число записей больше kMax* из data_types.h
    kBadAlignment,     // буфер не выровнен на 16
};

// Проверяет заголовок, CRC, каталог секций (границы, выравнивание, rec_size, лимиты)
// и заполняет *out указателями в buffer. Ссылки между записями НЕ проверяет —
// это гарантирует build_data.py. Неизвестные секции пропускает (совместимость вперёд).
// При ошибке *out остаётся пустым (как GameData{}).
DataError data_load(const void* buffer, size_t size, GameData* out);

// Короткое описание ошибки на латинице для лога/оверлея (не строка игрока).
const char* data_error_text(DataError err);

// Текст игрока (UTF-8) по индексу. kNone или индекс вне диапазона → "".
const char* data_string(const GameData& data, StrId id);

// Служебное имя (ASCII): id записи, модель, текстура, флаг. kNone или вне диапазона → "".
const char* data_name(const GameData& data, NameId id);

// Строка игрока по ключу из strings_ru.json ("ui.cashier.title") — бинарный поиск по STRK.
// Нет ключа → kNone. Для UI: искать один раз при загрузке и кэшировать StrId.
StrId data_find_string(const GameData& data, const char* key);

// Поиск записи по id ("school", "kontr_udar_13") — линейный. Нет → kNone.
Index data_find_game(const GameData& data, const char* id);
Index data_find_archetype(const GameData& data, const char* id);
Index data_find_regular(const GameData& data, const char* id);
Index data_find_hardware(const GameData& data, const char* id);
Index data_find_goods(const GameData& data, const char* id);
Index data_find_upgrade(const GameData& data, const char* id);
Index data_find_minigame(const GameData& data, const char* id);
Index data_find_event(const GameData& data, const char* id);
Index data_find_flag(const GameData& data, const char* id);

}  // namespace core
