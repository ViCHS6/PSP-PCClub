// Загрузка data.pak (контракт — data_api.h, формат — docs/formats/data_pak.md).
#include "core/data_api.h"

#include <cstring>

namespace core {

namespace {

// CRC-32 (полином zlib 0xEDB88320), таблица на полбайта: только при загрузке.
uint32_t crc32(const uint8_t* p, size_t n) {
    static constexpr uint32_t kNibble[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u,
        0x4DB26158u, 0x5005713Cu, 0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
        0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c ^= p[i];
        c = (c >> 4) ^ kNibble[c & 15u];
        c = (c >> 4) ^ kNibble[c & 15u];
    }
    return c ^ 0xFFFFFFFFu;
}

// Табличная секция: размер записи, лимит, границы.
template <typename T>
DataError take_table(const uint8_t* base, const PakSection* s, uint32_t limit, const T** ptr,
                     uint16_t* count) {
    if (s == nullptr) return DataError::kOk;  // нет секции — пустая таблица
    if (s->rec_size != sizeof(T)) return DataError::kBadSection;
    if (static_cast<uint64_t>(s->count) * sizeof(T) != s->size) return DataError::kBadSection;
    if (s->count > limit) return DataError::kLimitExceeded;
    *ptr = s->count > 0 ? reinterpret_cast<const T*>(base + s->offset) : nullptr;
    *count = static_cast<uint16_t>(s->count);
    return DataError::kOk;
}

// Строковая секция: u32 offsets[count] + байты строк; каждая строка внутри секции и
// последний байт — 0, поэтому data_string/data_name не выходят за буфер.
DataError take_strings(const uint8_t* base, const PakSection* s, const uint8_t** ptr,
                       uint16_t* count) {
    if (s->rec_size != 1) return DataError::kBadSection;
    if (s->count > 65535u) return DataError::kLimitExceeded;
    if (static_cast<uint64_t>(s->count) * 4u > s->size) return DataError::kBadSection;
    const uint8_t* p = base + s->offset;
    if (s->count > 0) {
        if (p[s->size - 1] != 0) return DataError::kBadSection;
        for (uint32_t i = 0; i < s->count; ++i) {
            uint32_t off;
            std::memcpy(&off, p + i * 4u, 4);
            if (off < s->count * 4u || off >= s->size) return DataError::kBadSection;
        }
    }
    *ptr = p;
    *count = static_cast<uint16_t>(s->count);
    return DataError::kOk;
}

const char* string_at(const uint8_t* section, uint16_t count, uint16_t id) {
    if (section == nullptr || id == kNone || id >= count) return "";
    uint32_t off;
    std::memcpy(&off, section + static_cast<size_t>(id) * 4u, 4);
    return reinterpret_cast<const char*>(section + off);
}

DataError load(const uint8_t* base, size_t size, GameData* out) {
    if (reinterpret_cast<uintptr_t>(base) % 16u != 0) return DataError::kBadAlignment;
    if (size < sizeof(PakHeader)) return DataError::kTooSmall;
    const PakHeader* h = reinterpret_cast<const PakHeader*>(base);
    if (h->magic != kPakMagic) return DataError::kBadMagic;
    if (h->version != kPakVersion) return DataError::kBadVersion;
    if (h->file_size != size) return DataError::kTooSmall;
    const uint64_t dir_end = sizeof(PakHeader) + static_cast<uint64_t>(h->section_count) * sizeof(PakSection);
    if (dir_end > size) return DataError::kTooSmall;
    if (crc32(base + 16, size - 16) != h->crc32) return DataError::kBadCrc;

    const PakSection* dir = reinterpret_cast<const PakSection*>(base + sizeof(PakHeader));
    static constexpr uint32_t kTags[] = {kSecGames,   kSecArchetypes, kSecRegulars, kSecHardware,
                                         kSecGoods,   kSecUpgrades,   kSecMinigames, kSecEvents,
                                         kSecChoices, kSecEffects,    kSecConds,    kSecChance,
                                         kSecLists,   kSecPeriods,    kSecHolidays, kSecFlags,
                                         kSecEconomy, kSecStrings,    kSecStringKeys, kSecNames};
    constexpr int kTagCount = sizeof(kTags) / sizeof(kTags[0]);
    const PakSection* found[kTagCount] = {};
    for (uint32_t i = 0; i < h->section_count; ++i) {
        const PakSection& s = dir[i];
        if (s.offset % 16u != 0 || s.offset < dir_end ||
            static_cast<uint64_t>(s.offset) + s.size > size) {
            return DataError::kBadSection;
        }
        for (int t = 0; t < kTagCount; ++t) {
            if (kTags[t] != s.tag) continue;
            if (found[t] != nullptr) return DataError::kBadSection;  // тег дважды
            found[t] = &s;
        }
    }
    auto sec = [&](uint32_t tag) -> const PakSection* {
        for (int t = 0; t < kTagCount; ++t) {
            if (kTags[t] == tag) return found[t];
        }
        return nullptr;
    };
    if (sec(kSecEconomy) == nullptr || sec(kSecStrings) == nullptr ||
        sec(kSecStringKeys) == nullptr || sec(kSecNames) == nullptr) {
        return DataError::kMissingSection;
    }
    if (sec(kSecEconomy)->count != 1) return DataError::kBadSection;

    GameData d;
    DataError e = DataError::kOk;
    uint16_t econ_count = 0;
    // clang-format off
    if ((e = take_table(base, sec(kSecGames), kMaxGames, &d.games, &d.game_count)) != DataError::kOk) return e;
    if ((e = take_table(base, sec(kSecArchetypes), kMaxArchetypes, &d.archetypes, &d.archetype_count)) != DataError::kOk) return e;
    if ((e = take_table(base, sec(kSecRegulars), kMaxRegulars, &d.regulars, &d.regular_count)) != DataError::kOk) return e;
    if ((e = take_table(base, sec(kSecHardware), kMaxHardware, &d.hardware, &d.hardware_count)) != DataError::kOk) return e;
    if ((e = take_table(base, sec(kSecGoods), kMaxGoods, &d.goods, &d.goods_count)) != DataError::kOk) return e;
    if ((e = take_table(base, sec(kSecUpgrades), kMaxUpgrades, &d.upgrades, &d.upgrade_count)) != DataError::kOk) return e;
    if ((e = take_table(base, sec(kSecMinigames), kMaxMinigames, &d.minigames, &d.minigame_count)) != DataError::kOk) return e;
    if ((e = take_table(base, sec(kSecEvents), kMaxEvents, &d.events, &d.event_count)) != DataError::kOk) return e;
    if ((e = take_table(base, sec(kSecChoices), 65535u, &d.choices, &d.choice_count)) != DataError::kOk) return e;
    if ((e = take_table(base, sec(kSecEffects), 65535u, &d.effects, &d.effect_count)) != DataError::kOk) return e;
    if ((e = take_table(base, sec(kSecConds), 65535u, &d.conds, &d.cond_count)) != DataError::kOk) return e;
    if ((e = take_table(base, sec(kSecChance), 65535u, &d.chance_terms, &d.chance_term_count)) != DataError::kOk) return e;
    if ((e = take_table(base, sec(kSecLists), 65535u, &d.lists, &d.list_count)) != DataError::kOk) return e;
    if ((e = take_table(base, sec(kSecPeriods), 65535u, &d.periods, &d.period_count)) != DataError::kOk) return e;
    if ((e = take_table(base, sec(kSecHolidays), 65535u, &d.holidays, &d.holiday_count)) != DataError::kOk) return e;
    if ((e = take_table(base, sec(kSecFlags), kMaxFlags, &d.flags, &d.flag_count)) != DataError::kOk) return e;
    if ((e = take_table(base, sec(kSecEconomy), 1u, &d.economy, &econ_count)) != DataError::kOk) return e;
    if ((e = take_table(base, sec(kSecStringKeys), 65535u, &d.string_keys, &d.string_key_count)) != DataError::kOk) return e;
    if ((e = take_strings(base, sec(kSecStrings), &d.strings, &d.string_count)) != DataError::kOk) return e;
    if ((e = take_strings(base, sec(kSecNames), &d.names, &d.name_count)) != DataError::kOk) return e;
    // clang-format on
    *out = d;
    return DataError::kOk;
}

}  // namespace

DataError data_load(const void* buffer, size_t size, GameData* out) {
    *out = GameData{};
    if (buffer == nullptr) return DataError::kTooSmall;
    return load(static_cast<const uint8_t*>(buffer), size, out);
}

const char* data_error_text(DataError err) {
    switch (err) {
        case DataError::kOk: return "ok";
        case DataError::kTooSmall: return "file too small or size mismatch";
        case DataError::kBadMagic: return "bad magic";
        case DataError::kBadVersion: return "unsupported pak version";
        case DataError::kBadCrc: return "crc mismatch";
        case DataError::kBadSection: return "bad section";
        case DataError::kMissingSection: return "missing required section";
        case DataError::kLimitExceeded: return "table limit exceeded";
        case DataError::kBadAlignment: return "buffer not aligned to 16";
    }
    return "unknown error";
}

const char* data_string(const GameData& data, StrId id) {
    return string_at(data.strings, data.string_count, id);
}

const char* data_name(const GameData& data, NameId id) {
    return string_at(data.names, data.name_count, id);
}

StrId data_find_string(const GameData& data, const char* key) {
    const uint32_t h = fnv1a32(key);
    int lo = 0, hi = static_cast<int>(data.string_key_count) - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        const uint32_t mh = data.string_keys[mid].hash;
        if (mh == h) return data.string_keys[mid].str;
        if (mh < h) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return kNone;
}

namespace {

template <typename T>
Index find_by_id(const GameData& data, const T* recs, uint16_t count, const char* id) {
    for (uint16_t i = 0; i < count; ++i) {
        if (std::strcmp(data_name(data, recs[i].id), id) == 0) return i;
    }
    return kNone;
}

}  // namespace

Index data_find_game(const GameData& d, const char* id) { return find_by_id(d, d.games, d.game_count, id); }
Index data_find_archetype(const GameData& d, const char* id) {
    return find_by_id(d, d.archetypes, d.archetype_count, id);
}
Index data_find_regular(const GameData& d, const char* id) {
    return find_by_id(d, d.regulars, d.regular_count, id);
}
Index data_find_hardware(const GameData& d, const char* id) {
    return find_by_id(d, d.hardware, d.hardware_count, id);
}
Index data_find_goods(const GameData& d, const char* id) { return find_by_id(d, d.goods, d.goods_count, id); }
Index data_find_upgrade(const GameData& d, const char* id) {
    return find_by_id(d, d.upgrades, d.upgrade_count, id);
}
Index data_find_minigame(const GameData& d, const char* id) {
    return find_by_id(d, d.minigames, d.minigame_count, id);
}
Index data_find_event(const GameData& d, const char* id) { return find_by_id(d, d.events, d.event_count, id); }

Index data_find_flag(const GameData& d, const char* id) {
    for (uint16_t i = 0; i < d.flag_count; ++i) {
        if (std::strcmp(data_name(d, d.flags[i]), id) == 0) return i;
    }
    return kNone;
}

}  // namespace core
