#pragma once
// Программа ОС «Отладка» (DBG): админские команды для проверок на PSP Go и PC — деньги, ход
// времени, посетитель, игры на ПК, поломки, сохранение, события (core/debug_cmds.h).
// Собирается при PCCLUB_DEBUG_TOOLS (CMake, по умолчанию ON; выключить к релизу — T063).
// Управление как в остальных программах: крестовина/стик — фокус, × — выполнить, ←/→ — выбор
// в «← значение →», ○ — закрыть, L/R — окна.
#include "ui/ui_api.h"

namespace ui {

// Сохранение — через игру (game/autosave). nullptr — кнопка серая (тесты без носителя).
// Вызываются только из debug_after_frame: на PSP утилита savedata крутит свои кадры.
struct DebugHost {
    bool (*save)(const core::World& world) = nullptr;  // сохранить сейчас, с системным диалогом
    bool (*load)(core::World& world) = nullptr;        // прочитать слот в мир (мир тот же объект)
    bool (*erase)() = nullptr;                         // затереть слот: следующий запуск — новая игра
};

// os_apps регистрирует без хоста; игра подключает его после (debug_set_host) — так pcclub_ui
// не зависит от game/ (minigame-runner собирает ui без игры).
AppId debug_register(const DebugHost& host = DebugHost{});
void debug_set_host(const DebugHost& host);

// Через game/autosave и platform/save_api (debug_host.cpp, только в исполняемом pcclub).
DebugHost debug_game_host();

// Главный цикл. Ускорение для core::debug_minutes_per_real_second: 1 — обычные правила.
int debug_speed();
// После render::end_frame: выполнить отложенную запись/чтение/стирание слота. world — тот же,
// что в set_world; nullptr — ничего.
void debug_after_frame(core::World* world);

}  // namespace ui
