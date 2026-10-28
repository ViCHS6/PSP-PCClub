#pragma once
// Что можно сделать с ПК в зале (T014): по состоянию ПК в ядре — действие, подсказка, мини-игра
// обслуживания (GDD §6.5) и команда ядра с её quality. Без render/ui/minigames — для тестов.
//
//   монитор ×   kHung — Перезагрузка (svc_reboot → cmd_reboot);
//               kIdle/kBusy и есть что ставить — Установка (svc_install → cmd_install)
//   системник × kOff — включить; kIdle/kHung без посетителя — выключить (cmd_set_pc_power);
//               занят — только подсказка; kBroken — Замена (svc_replace → cmd_repair), пока
//               мини-игры нет или деталь не чинится (wear ≥ 100) — подсказка «сломан»
//   системник □ пыль ≥ kDustAction и ПК не занят — Чистка (svc_clean → cmd_clean_dust)
#include "core/core_api.h"

namespace game {

constexpr float kDustAction = 10.0f;  // пыль, с которой предлагается чистка (видимая глазу)

enum class PcAction : uint8_t {
    kNone,        // нечего делать — предмет не выбирается прицелом
    kInfoBusy,    // только подсказка, × ничего не делает
    kInfoBroken,
    kPowerOn,
    kPowerOff,
    kReboot,
    kClean,
    kInstall,
    kRepair,
};

struct SeatActions {
    PcAction monitor = PcAction::kNone;      // × у монитора
    PcAction case_primary = PcAction::kNone; // × у системника
    PcAction case_alt = PcAction::kNone;     // □ у системника
    core::Index install_game = core::kNone;  // для kInstall
    bool install_pirate = false;
};

// can_repair — зарегистрирована ли мини-игра svc_replace.
SeatActions seat_actions(const core::World& world, uint8_t pc, bool can_repair);

// Игра для установки на ПК: просьба посетителя этого ПК (Request::kGame), иначе первая по
// каталогу вышедшая, неустановленная, влезающая на HDD, с лицензией или пиратским диском.
// pirate — ставить с пиратского диска (свободной лицензии нет). kNone — ставить нечего.
core::Index install_candidate(const core::World& world, uint8_t pc, bool* pirate);

// Ключ strings_ru.json подсказки (к нему дописывается номер места). nullptr — kNone.
const char* pc_action_hint(PcAction action);
// Действие выполняется (а не только подсказка).
bool pc_action_acts(PcAction action);
// Id мини-игры обслуживания или nullptr — команда сразу (питание).
const char* pc_action_minigame(PcAction action);
// StartParams::difficulty 0…1 по состоянию ПК.
float pc_action_difficulty(const core::World& world, uint8_t pc, PcAction action, core::Index game);
// StartParams::variant (Замена — core::Component).
uint8_t pc_action_variant(const core::World& world, uint8_t pc, PcAction action);

// Команда ядра. quality — результат мини-игры 0…1 (для питания не используется).
core::CmdResult pc_action_apply(core::World& world, uint8_t pc, PcAction action, core::Index game,
                                bool pirate, float quality);

}  // namespace game
