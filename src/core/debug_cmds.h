#pragma once
// Отладочные команды ядра (DBG): программа ОС «Отладка» и тесты. НЕ контракт и не правила
// игры — команды обходят ограничения (лицензии, шанс событий, ход времени) ради проверок на
// PSP Go и PC. Вызываются из того же потока, что и всё ядро, между тиками. Без аллокаций.
// Игровой код вне отладки их не вызывает: в релизе (PCCLUB_DEBUG_TOOLS=OFF, T063) они
// остаются в pcclub_core, но никем не используются.
#include <cstdint>

#include "core/core_api.h"

namespace core {

// Деньги в кассу как есть (без статей выручки). Сумма может быть отрицательной.
CmdResult debug_add_money(World& world, int32_t amount);

enum class DebugSkip : uint8_t {
    kToOpen,     // до 9:00 (сейчас открыто — до открытия следующего дня)
    kToClose,    // до 24:00, фаза kClosed: итоги дня ждут cmd_end_day (автосохранение при закрытии)
    kToMorning,  // до 8:00 следующего дня, фаза kMorning
};

// Промотать время скачком: минуты внутри фазы пропускаются (посетители не тратят терпение,
// часы событий не наступают), смены фаз — настоящие (tick и cmd_end_day: закрытие дня, расходы,
// ночь, утро с доставкой и почтой). kGameOver → kWrongPhase.
CmdResult debug_skip(World& world, DebugSkip to);

// Игровых минут в реальную секунду с отладочным ускорением mult (×4, ×16) — без правила GDD §4
// «при очереди ×1». mult ≤ 1 — обычное minutes_per_real_second.
float debug_minutes_per_real_second(const World& world, int mult);

// Новый посетитель случайного архетипа входит в клуб и через kTransitMinutes·2 встаёт в очередь
// (денег — хотя бы на час). Только kOpen/kNight (kWrongPhase), очередь полна → kLimit.
CmdResult debug_spawn_visitor(World& world);

// Все вышедшие игры на все собранные ПК: недостающие лицензии добавляются, установка — cmd_install
// с quality = 1 (HDD проверяется: не влезло — пропуск). Возвращает число новых установок.
int debug_install_all(World& world);

// Выбранный ПК (pc < club.seats). Ломать и вешать — только работающий (kIdle/kBusy), иначе kInvalid.
// Ломается самый изношенный компонент (износ не меняется — ремонт возможен).
CmdResult debug_break_pc(World& world, uint8_t pc);
CmdResult debug_hang_pc(World& world, uint8_t pc);
// Пыль 90 (≥ 70 — красная полоска в Сети). Пустое место → kInvalid.
CmdResult debug_dust_pc(World& world, uint8_t pc);
// Износ, пыль, вирус — 0; зависший/сломанный снова работает, просьба посетителя решена.
CmdResult debug_fix_pc(World& world, uint8_t pc);

// Показать событие сейчас, без условий, шанса и кулдауна; всегда письмом в Почте (другие способы
// показа UI пока не умеет). Нет такого события → kInvalid, нет свободного слота → kLimit.
CmdResult debug_fire_event(World& world, Index event);

}  // namespace core
