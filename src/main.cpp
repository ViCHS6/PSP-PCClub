// Точка входа — общая для PSP и PC.
#include "game/debug_overlay.h"
#include "game/player_demo.h"
#include "platform/platform_api.h"
#include "platform/audio_api.h"
#include "render/render_api.h"

int main(int argc, char** argv) {
    if (!platform::init({argc, argv})) return 1;
    if (!render::init(render::Config{})) {
        platform::shutdown();
        return 1;
    }

    if (!audio::init(audio::Config{})) { render::shutdown(); platform::shutdown(); return 1; }
    // T010: демо управления; до T014 — вместо DemoScene (T001/T012/T041), та остаётся в сборке.
    static game::PlayerDemo scene;  // статически: пулы Colliders/InteractSystem не на стеке
    game::DebugOverlay overlay;
    if (!scene.init()) {
        audio::shutdown();
        render::shutdown();
        platform::shutdown();
        return 1;
    }

    uint64_t prev = platform::time_us();
    while (platform::poll()) {
        const uint64_t now = platform::time_us();
        float dt = static_cast<float>(now - prev) * 1e-6f;
        prev = now;
        if (dt > 0.1f) dt = 0.1f;  // после паузы или сна консоли — без скачка

        const platform::InputState& input = platform::input();
        scene.update(dt, input);
        audio::update();
        overlay.update(dt, input);

        render::begin_frame(render::rgba(40, 44, 52));
        scene.draw();
        overlay.draw();
        render::end_frame();
    }

    audio::shutdown();
    render::shutdown();
    platform::shutdown();
    return 0;
}
