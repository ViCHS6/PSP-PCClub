#pragma once
// Внутреннее состояние ядра внутри World::priv. НЕ контракт: структуру расширяют T022/T023
// по мере реализации. Снаружи ядра не читать и не писать.
// Требование: тривиально копируемая, без указателей на себя и без аллокаций.
#include <cstdint>

#include "core/data_types.h"

namespace core {
namespace detail {

// Совпадают с kMaxVisitors / kMaxPcs из core_api.h (проверяется static_assert в world.cpp).
constexpr int kPrivVisitors = 32;
constexpr int kPrivPcs = 16;

// Дата «никогда» для сроков действия.
constexpr DayNum kNoDate = -0x7FFFFFFF;

struct WorldPrivate {
    // --- Посетители, по слотам World::visitors (T022)
    uint8_t vis_hours_roll[kPrivVisitors];  // бросок session_hours (§7.2) до ограничения деньгами
    uint8_t vis_night[kPrivVisitors];       // 1 — взял ночной пакет
    uint8_t vis_resolved[kPrivVisitors];    // решённых просьб в текущем часе игры (§7.5)
    uint8_t vis_hour_min[kPrivVisitors];    // минут с начала текущего часа игры
    float vis_event_sat[kPrivVisitors];     // event_mods в Δsat за час (§7.5), ставит T023

    // --- ПК (T022)
    uint8_t pc_load_min[kPrivPcs];          // минут нагрузки с последнего часового шага (§7.7)

    // --- День (T022)
    uint16_t open_minutes_today = 0;        // минут работы клуба (kOpen + kNight) — электричество
    uint8_t cache_valid = 0;                // 0 — пересчитать pop_today и lib_appeal
    uint8_t pad = 0;

    // --- Постоянные клиенты: день последнего визита (приходят не чаще раза в день)
    DayNum reg_last_visit[kMaxRegulars];

    // --- Реклама kUpgAd: действует по этот день включительно (§7.1, F_ad аддитивен)
    DayNum ad_until[kMaxUpgrades];

    // --- Кэши на час игры (§7.6): популярность с модификаторами и L_a
    float pop_today[kMaxGames];
    float lib_appeal[kMaxArchetypes];
};

}  // namespace detail
}  // namespace core
