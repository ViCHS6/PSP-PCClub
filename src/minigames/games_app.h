#pragma once
// Программа ОС «Игры» (GDD §9.2, T050): список играбельных мини-игр, запуск в хосте SDK,
// итог последней попытки. Запись рекорда (cmd_submit_record) — T053.
#include "ui/ui_api.h"

namespace minigame {

// После ui::init() и register_builtin(). kNoApp — не зарегистрировалась.
ui::AppId games_app_register();

}  // namespace minigame
