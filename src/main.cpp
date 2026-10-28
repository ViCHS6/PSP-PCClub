// Точка входа — общая для PSP и PC.
#include "game/debug_overlay.h"
#include "game/demo_scene.h"
#include "platform/platform_api.h"
#include "platform/audio_api.h"
#include "render/render_api.h"
#include "core/core_api.h"
#include "core/data_api.h"
#include "ui/demo_app.h"
#include "ui/ui_api.h"

#include <cstdio>
#include <cstring>

namespace {

// data.pak — строки ОС (T030). Буфер по примеру data_api.h; без файла ОС покажет ключи.
alignas(16) uint8_t g_pak[1 << 20];
core::GameData g_data;
core::World g_world;  // демо: стартовый мир без тиков — деньги и часы на панели задач

bool load_data() {
    size_t size = 0;
    if (!platform::file_read("data.pak", g_pak, sizeof(g_pak), &size)) {
        std::fprintf(stderr, "data.pak: not found\n");
        return false;
    }
    const core::DataError err = core::data_load(g_pak, size, &g_data);
    if (err != core::DataError::kOk) {
        std::fprintf(stderr, "data.pak: %s\n", core::data_error_text(err));
        return false;
    }
    return true;
}

bool has_arg(int argc, char** argv, const char* arg) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], arg) == 0) return true;
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (!platform::init({argc, argv})) return 1;
    if (!render::init(render::Config{})) {
        platform::shutdown();
        return 1;
    }

    if (!audio::init(audio::Config{})) { render::shutdown(); platform::shutdown(); return 1; }
    game::DemoScene scene;
    game::DebugOverlay overlay;
    if (!scene.init()) {
        audio::shutdown();
        render::shutdown();
        platform::shutdown();
        return 1;
    }

    // ОС администратора: демо по START (PC — ещё и флагом --ui-demo, для снимков).
    const bool have_data = load_data();
    const bool have_ui = ui::init({have_data ? &g_data : nullptr, audio::sound_find("sfx_click")});
    ui::AppId demo_app = ui::kNoApp;
    if (have_ui) {
        if (have_data) {
            core::world_init(g_world, g_data, 1);
            ui::set_world(&g_world);
        }
        demo_app = ui::demo_register();
        if (has_arg(argc, argv, "--ui-demo")) {
            ui::set_active(true);
            ui::open_app(demo_app);
            platform::InputState select{};  // оверлей заслоняет окно на снимке; SELECT вернёт его
            select.pressed = platform::kButtonSelect;
            overlay.update(0.0f, select);
        }
    }

    uint64_t prev = platform::time_us();
    while (platform::poll()) {
        const uint64_t now = platform::time_us();
        float dt = static_cast<float>(now - prev) * 1e-6f;
        prev = now;
        if (dt > 0.1f) dt = 0.1f;  // после паузы или сна консоли — без скачка

        const platform::InputState& input = platform::input();
        if (have_ui && (input.pressed & platform::kButtonStart)) {
            ui::set_active(!ui::active());
            if (ui::active() && !ui::is_open(demo_app)) ui::open_app(demo_app);
        }
        ui::update(dt, input);
        scene.update(dt, ui::active() ? platform::InputState{} : input);
        audio::update();
        overlay.update(dt, input);

        render::begin_frame(render::rgba(40, 44, 52));
        scene.draw();
        ui::draw();
        overlay.draw();
        render::end_frame();
    }

    ui::shutdown();
    audio::shutdown();
    render::shutdown();
    platform::shutdown();
    return 0;
}
