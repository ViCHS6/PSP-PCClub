#pragma once
// Отладочный оверлей: FPS, время кадра, память, статистика рендера, ввод.
// SELECT — показать/скрыть. Тексты — отладочные, поэтому не в strings_ru.json.
#include "platform/platform_api.h"

namespace game {

class ServiceTimer;

class DebugOverlay {
public:
    void update(float dt, const platform::InputState& input);
    void draw() const;
    // Замер обслуживания у стойки (T033); nullptr — строки нет. Должен жить дольше оверлея.
    void set_service(const ServiceTimer* timer) { service_ = timer; }

private:
    bool visible_ = true;
    float accum_time_ = 0.0f;
    int accum_frames_ = 0;
    float fps_ = 0.0f;
    float frame_ms_ = 0.0f;
    platform::InputState input_;
    const ServiceTimer* service_ = nullptr;
};

}  // namespace game
