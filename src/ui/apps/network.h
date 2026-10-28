#pragma once
// Программа ОС «Сеть» (T031, GDD §9.2): схема зала с состоянием каждого ПК, выбор ПК и действия
// core_api — перезагрузка, чистка, установка игры, ремонт (через мини-игры обслуживания), питание.
#include "ui/ui_api.h"

namespace ui {

// Запуск мини-игр обслуживания (GDD §6.5). В игре — network_minigame_runner() (SDK мини-игр),
// в тестах — заглушка. Пока мини-игра идёт, окно Сети рисуется под ней без ввода.
struct ServiceRunner {
    // Есть ли такая мини-игра. Нет — действие недоступно (кнопка серая).
    bool (*available)(core::ServiceGame game) = nullptr;
    // Запустить попытку. false — игры нет или уже идёт другая.
    bool (*start)(core::ServiceGame game, float difficulty, uint8_t variant, uint32_t seed) = nullptr;
    // Попытка, запущенная start(), закончилась: true, *quality 0…1 (провал — 0), *aborted — вышли
    // через паузу (ничего не применять). Пока идёт — false.
    bool (*finished)(float* quality, bool* aborted) = nullptr;
};

AppId network_register(const ServiceRunner& runner);

// Запуск через src/minigames (network_minigames.cpp): id "svc_<игра>" (minigame.md §7).
ServiceRunner network_minigame_runner();

}  // namespace ui
