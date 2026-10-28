// Точка входа — общая для PSP и PC.
#include "game/admin_desk.h"
#include "game/autosave.h"
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
    game::autosave_update(g_world);  // T060: утро и закрытие — до cmd_end_day
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
    bool loaded = false;  // T060: мир из сохранения — без демо-поломок club_demo_world
    if (have_ui) {
        if (have_data) {
            core::world_init(g_world, g_data, 1);
            loaded = game::autosave_load(g_world, g_data);
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
    // В новой игре ПК не виснут и не пылятся ещё долго — club_demo_world расставляет состояния
    // ПК, чтобы было что обслужить (до стартового состояния новой игры, T062).
    static game::ClubScene scene;  // статически: пулы Colliders/InteractSystem не на стеке
    if (have_data && !loaded) game::club_demo_world(g_world);
    if (!scene.init({have_data ? &g_world : nullptr, have_data ? &g_data : nullptr})) {
        ui::shutdown();
        audio::shutdown();
        render::shutdown();
        platform::shutdown();
        return 1;
    }

    // T033: × у стойки — камера въезжает в монитор, ОС открывается с Кассой.
    static game::AdminDesk desk;
    desk.init(scene.admin_screen(), have_ui ? ui::find_app("cashier") : ui::kNoApp);
    overlay.set_service(&desk.timer());
    core::World* const world = have_data ? &g_world : nullptr;
    if (have_ui && has_arg(argc, argv, "--desk")) desk.enter(world);  // PC, для снимков въезда

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
        // START — ОС сразу, без стойки (пока нет системного меню, GDD §9.1); не во время въезда.
        const bool desk_moving = desk.state() == game::AdminDesk::State::kEntering ||
                                 desk.state() == game::AdminDesk::State::kLeaving;
        if (have_ui && !in_minigame && !desk_moving && (input.pressed & platform::kButtonStart)) {
            ui::set_active(!ui::active());
        }
        if (in_minigame) minigame::update(dt, input);
        desk.update(dt, world);
        ui::update(dt, in_minigame ? platform::InputState{} : input);
        const bool world_input = desk.world_input() && !ui::active() && !in_minigame;
        scene.update(dt, world_input ? input : platform::InputState{});
        if (have_ui && scene.take_os_request()) desk.enter(world);
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
        // Под ОС у стойки 3D не нужен: рабочий стол непрозрачен.
        if (desk.draw_world())
            scene.draw(desk.hud() && !ui::active() && !minigame::running(), desk.camera(scene.player()));
        desk.draw_fade();
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
