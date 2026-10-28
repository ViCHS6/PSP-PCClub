#pragma once
// Демо ОС (T030): окно со всеми виджетами и окно «О системе». Образец для программ T031/T032.
#include "ui/ui_api.h"

namespace ui {

// Регистрирует обе демо-программы. Вызывать после ui::init(). Возвращает id окна виджетов.
AppId demo_register();

}  // namespace ui
