#pragma once
// Отладочный оверлей: FPS, время кадра, память, статистика рендера, ввод.
// SELECT — показать/скрыть. Тексты — отладочные, поэтому не в strings_ru.json.
#include "platform/platform_api.h"

namespace game {

class DebugOverlay {
public:
    void update(float dt, const platform::InputState& input);
    void draw() const;

private:
    bool visible_ = true;
    float accum_time_ = 0.0f;
    int accum_frames_ = 0;
    float fps_ = 0.0f;
    float frame_ms_ = 0.0f;
    platform::InputState input_;
};

}  // namespace game
