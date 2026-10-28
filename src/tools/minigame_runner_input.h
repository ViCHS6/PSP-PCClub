#pragma once

#include <cstdint>
#include <vector>

#include "platform/platform_api.h"

namespace minigame_runner {

// Нумерация с нуля; кнопки удерживаются до следующей строки. Поддерживаются
// имена up/down/left/right/cross/square/triangle/circle/start/l/r/select,
// сочетания через пробел, '+' или ','; '-' отпускает всё, '#' начинает комментарий.
struct InputEvent {
    uint32_t frame;
    uint32_t held;
};

bool load_input(const char* path, std::vector<InputEvent>& events);
platform::InputState scripted_input(const std::vector<InputEvent>& events, uint64_t frame,
                                    size_t& next, uint32_t& held);

}  // namespace minigame_runner
