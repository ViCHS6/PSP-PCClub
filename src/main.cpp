// Точка входа — общая для PSP и PC.
#include "game/club_scene.h"
#include "game/debug_overlay.h"
#include "platform/platform_api.h"
#include "platform/audio_api.h"
#include "render/render_api.h"
#include "core/core_api.h"
#include "core/data_api.h"
#include "minigames/games_app.h"
#include "minigames/sdk.h"
#include "ui/apps/os_apps.h"
#include "ui/demo_app.h"
#include "ui/ui_api.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// data.pak — строки ОС (T030). Буфер по примеру data_api.h; без файла ОС покажет ключи.
alignas(16) uint8_t g_pak[1 << 20];
core::GameData g_data;
core::World g_world;
float g_sim_minutes = 0.0f;

// Мир идёт в реальном времени (T031: в Кассе должны появляться посетители). До итогов дня
// (T062) день закрывается сам.
void sim_update(float dt) {
    if (g_world.clock.phase == core::Phase::kClosed) core::cmd_end_day(g_world);
    g_sim_minutes += dt * core::minutes_per_real_second(g_world);
    while (g_sim_minutes >= 1.0f) {
        core::tick(g_world);
        g_sim_minutes -= 1.0f;
    }
}

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

const char* arg_value(int argc, char** argv, const char* arg) {
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], arg) == 0) return argv[i + 1];
    return nullptr;
}

// --minigame <id>: сразу запустить мини-игру в хосте (T050); первый вид из Desc::kinds.
bool start_cli_minigame(const char* id) {
    const minigame::GameIndex index = minigame::find_game(id);
    const minigame::Desc* desc = minigame::game(index);
    if (!desc) {
        std::fprintf(stderr, "--minigame %s: unknown\n", id);
        return false;
    }
    minigame::StartParams params;
    params.kind = (desc->kinds & minigame::kPlayable) ? minigame::kPlayable : minigame::kService;
    return minigame::start(index, params);
}

}  // namespace

int main(int argc, char** argv) {
    if (!platform::init({argc, argv})) return 1;
    if (!render::init(render::Config{})) {
        platform::shutdown();
        return 1;
    }

    if (!audio::init(audio::Config{})) { render::shutdown(); platform::shutdown(); return 1; }
    game::DebugOverlay overlay;

    // ОС администратора: демо по START (PC — ещё и флагом --ui-demo, для снимков).
    const bool have_data = load_data();
    const bool have_ui = ui::init({have_data ? &g_data : nullptr, audio::sound_find("sfx_click")});
    ui::AppId demo_app = ui::kNoApp;
    if (have_ui) {
        if (have_data) {
            core::world_init(g_world, g_data, 1);
            ui::set_world(&g_world);
        }
        minigame::register_builtin();
        if (have_data) minigame::validate(g_data);
        ui::register_os_apps();
        demo_app = ui::demo_register();  // демо T030 — последними значками
        // PC, для снимков: --skip-minutes N — промотать мир, --ui-app <id> — открыть программу.
        const char* skip = arg_value(argc, argv, "--skip-minutes");
        for (int i = have_data && skip ? std::atoi(skip) : 0; i > 0; --i) {
            if (g_world.clock.phase == core::Phase::kClosed) core::cmd_end_day(g_world);
            core::tick(g_world);
        }
        const char* app = arg_value(argc, argv, "--ui-app");
        if (has_arg(argc, argv, "--ui-demo") || app) {
            ui::set_active(true);
            ui::open_app(app ? ui::find_app(app) : demo_app);
            platform::InputState select{};  // оверлей заслоняет окно на снимке; SELECT вернёт его
            select.pressed = platform::kButtonSelect;
            overlay.update(0.0f, select);
        }
    }

    // T014: зал на 6 мест (вместо демо T010 PlayerDemo; DemoScene/PlayerDemo остаются в сборке).
    // Мир демо не тикает — club_demo_world расставляет состояния ПК, чтобы было что обслужить.
    static game::ClubScene scene;  // статически: пулы Colliders/InteractSystem не на стеке
    if (have_data) game::club_demo_world(g_world);
    if (!scene.init({have_data ? &g_world : nullptr, have_data ? &g_data : nullptr})) {
        ui::shutdown();
        audio::shutdown();
        render::shutdown();
        platform::shutdown();
        return 1;
    }

    const char* cli_game = have_ui ? arg_value(argc, argv, "--minigame") : nullptr;
    bool cli_running = cli_game && start_cli_minigame(cli_game);

    uint64_t prev = platform::time_us();
    while (platform::poll()) {
        const uint64_t now = platform::time_us();
        float dt = static_cast<float>(now - prev) * 1e-6f;
        prev = now;
        if (dt > 0.1f) dt = 0.1f;  // после паузы или сна консоли — без скачка

        const platform::InputState& input = platform::input();
        // Мини-игра забирает весь ввод, включая START (sdk.h); ОС и мир под ней — без ввода.
        const bool in_minigame = minigame::running();
        if (have_ui && !in_minigame && (input.pressed & platform::kButtonStart)) {
            ui::set_active(!ui::active());
        }
        if (in_minigame) minigame::update(dt, input);
        ui::update(dt, in_minigame ? platform::InputState{} : input);
        scene.update(dt, ui::active() || in_minigame ? platform::InputState{} : input);
        if (have_ui && scene.take_os_request()) {  // × у стойки; переход камеры — T033
            ui::set_active(true);
            if (!ui::is_open(demo_app)) ui::open_app(demo_app);
        }
        minigame::Result result;
        if (cli_running && !minigame::running() && minigame::take_result(&result)) {
            cli_running = false;
            static const char* const kOutcome[] = {"completed", "failed", "aborted"};
            std::printf("result game=%s outcome=%s score=%d quality=%.3f duration_ms=%u\n", cli_game,
                        kOutcome[static_cast<int>(result.outcome)], static_cast<int>(result.score),
                        static_cast<double>(result.quality), static_cast<unsigned>(result.duration_ms));
        }
        if (have_data) sim_update(dt);
        audio::update();
        overlay.update(dt, input);

        render::begin_frame(render::rgba(40, 44, 52));
        scene.draw(!ui::active() && !minigame::running());  // подсказки мира — не под ОС и мини-игрой
        ui::draw();
        minigame::draw();
        overlay.draw();
        render::end_frame();
    }

    ui::shutdown();
    audio::shutdown();
    render::shutdown();
    platform::shutdown();
    return 0;
}
