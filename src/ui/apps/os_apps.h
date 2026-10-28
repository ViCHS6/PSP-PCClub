#pragma once
// Все программы ОС администратора (GDD §9.2). Вызывать после ui::init() и
// minigame::register_builtin(): Сеть и Игры смотрят реестр мини-игр.
namespace ui {
void register_os_apps();
}  // namespace ui
