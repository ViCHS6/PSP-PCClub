// T051: хост мини-игр без мира клуба; data.pak нужен только для строк.
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "minigames/sdk.h"
#include "platform/audio_api.h"
#include "tools/minigame_runner_input.h"
#include "tools/minigame_runner_screenshot.h"

namespace {

struct Options {
    const char* game = nullptr;
    const char* screenshot = nullptr;
    const char* input = nullptr;
    minigame::StartParams params;
    uint32_t frames = 0;
    uint32_t repeat = 1;
    bool explicit_kind = false;
};

alignas(16) uint8_t g_pak[1 << 20];
core::GameData g_data;

bool integer(const char* value, uint32_t limit, uint32_t& out) {
    if (value[0] < '0' || value[0] > '9') return false;
    errno = 0;
    char* end = nullptr;
    const unsigned long long number = std::strtoull(value, &end, 10);
    if (errno || *end || number > limit) return false;
    out = static_cast<uint32_t>(number);
    return true;
}

bool parse_args(int argc, char** argv, Options& options) {
    constexpr const char* kFlags[] = {"--game", "--kind", "--seed", "--difficulty", "--variant",
                                    "--best", "--frames", "--screenshot", "--input", "--repeat"};
    uint32_t seen = 0;
    for (int i = 1; i < argc; ++i) {
        int flag = -1;
        for (int j = 0; j < 10; ++j)
            if (std::strcmp(argv[i], kFlags[j]) == 0) flag = j;
        if (flag < 0 || (seen & (1u << flag)) || i + 1 >= argc) return false;
        seen |= 1u << flag;
        const char* value = argv[++i];
        uint32_t number = 0;
        switch (flag) {
        case 0: options.game = value; break;
        case 1:
            options.explicit_kind = true;
            if (std::strcmp(value, "playable") == 0) options.params.kind = minigame::kPlayable;
            else if (std::strcmp(value, "service") == 0) options.params.kind = minigame::kService;
            else return false;
            break;
        case 2:
            if (!integer(value, UINT32_MAX, options.params.seed)) return false;
            break;
        case 3: {
            char* end = nullptr;
            errno = 0;
            options.params.difficulty = std::strtof(value, &end);
            if (end == value || *end || errno || !std::isfinite(options.params.difficulty) ||
                options.params.difficulty < 0.0f || options.params.difficulty > 1.0f) return false;
            break;
        }
        case 4:
            if (!integer(value, UINT8_MAX, number)) return false;
            options.params.variant = static_cast<uint8_t>(number);
            break;
        case 5:
            if (!integer(value, INT32_MAX, number)) return false;
            options.params.best_score = static_cast<int32_t>(number);
            options.params.has_best = 1;
            break;
        case 6:
            if (!integer(value, UINT32_MAX, options.frames) || options.frames == 0) return false;
            break;
        case 7: options.screenshot = value; break;
        case 8: options.input = value; break;
        case 9:
            if (!integer(value, UINT32_MAX, options.repeat) || options.repeat == 0) return false;
            break;
        }
        if (!*value) return false;
    }
    return options.game && options.repeat - 1u <= UINT32_MAX - options.params.seed;
}

void list_games() {
    std::fprintf(stderr, "Registered games:");
    for (int i = 0; i < minigame::game_count(); ++i)
        std::fprintf(stderr, " %s", minigame::game(static_cast<minigame::GameIndex>(i))->id);
    std::fprintf(stderr, "\n");
}

bool load_data() {
    size_t size = 0;
    if (!platform::file_read("data.pak", g_pak, sizeof(g_pak), &size)) {
        std::fprintf(stderr, "data.pak: cannot read\n");
        return false;
    }
    const auto error = core::data_load(g_pak, size, &g_data);
    if (error == core::DataError::kOk) return true;
    std::fprintf(stderr, "data.pak: %s\n", core::data_error_text(error));
    return false;
}

int run(const Options& options, const std::vector<minigame_runner::InputEvent>& events) {
    minigame::register_builtin();
    if (minigame::game_count() == 0) {
        std::fprintf(stderr, "minigame: no games registered\n");
        return 1;
    }
    const auto index = minigame::find_game(options.game);
    const auto* desc = minigame::game(index);
    if (!desc) {
        std::fprintf(stderr, "--game: unknown or unavailable: %s\n", options.game);
        list_games();
        return 2;
    }
    minigame::StartParams params = options.params;
    if (!options.explicit_kind)
        params.kind = desc->kinds & minigame::kPlayable ? minigame::kPlayable : minigame::kService;
    if (!(desc->kinds & params.kind)) {
        std::fprintf(stderr, "--kind: unsupported by %s\n", options.game);
        return 2;
    }
    bool closed = false;
    for (uint32_t attempt = 0; attempt < options.repeat; ++attempt) {
        params.seed = options.params.seed + attempt;
        if (!minigame::start(index, params)) return 1;
        uint64_t frame = 0;
        uint64_t previous_time = platform::time_us();
        size_t next_event = 0;
        uint32_t held = 0;
        while (minigame::running() && !closed) {
            if (!platform::poll()) { closed = true; break; }
            const uint64_t now = platform::time_us();
            float dt = options.input ? 1.0f / 30.0f :
                static_cast<float>(now - previous_time) * 1e-6f;
            previous_time = now;
            if (dt < 0.001f) dt = 0.001f;
            if (dt > 0.1f) dt = 0.1f;
            const auto input = options.input ?
                minigame_runner::scripted_input(events, frame, next_event, held) : platform::input();
            minigame::update(dt, input);
            ui::update(dt, platform::InputState{});
            audio::update();
            // Хост уже закрыл закончившуюся игру: сохраняем её последний видимый кадр.
            if (minigame::running()) {
                render::begin_frame(render::rgba(96, 96, 96));
                minigame::draw();
                render::end_frame();
            }
            ++frame;
            if (options.frames && frame >= options.frames) minigame::abort();
        }
        minigame::abort();
        minigame::Result result;
        if (!minigame::take_result(&result)) return 1;
        constexpr const char* kOutcomes[] = {"completed", "failed", "aborted"};
        std::printf("result game=%s seed=%u outcome=%s score=%d quality=%.3f duration_ms=%u\n",
                    options.game, static_cast<unsigned>(params.seed),
                    kOutcomes[static_cast<int>(result.outcome)], static_cast<int>(result.score),
                    static_cast<double>(result.quality), static_cast<unsigned>(result.duration_ms));
        std::fflush(stdout);
        if (closed && attempt + 1 < options.repeat) return 1;
    }
    if (options.screenshot && !minigame_runner::save_screenshot(options.screenshot)) return 1;
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse_args(argc, argv, options)) {
        std::fprintf(stderr, "Usage: minigame-runner --game ID [--kind playable|service] [--seed N]\n"
            "  [--difficulty 0..1] [--variant N] [--best N] [--frames N]\n"
            "  [--screenshot out.bmp] [--input script.txt] [--repeat N]\n");
        return 2;
    }
    std::vector<minigame_runner::InputEvent> events;
    if (options.input && !minigame_runner::load_input(options.input, events)) return 2;
    // Раннер сам считает кадры каждой попытки; platform не получает --frames/--screenshot.
    if (!platform::init({1, argv})) { platform::shutdown(); return 1; }
    if (!render::init(render::Config{})) { platform::shutdown(); return 1; }
    if (!audio::init(audio::Config{})) {
        render::shutdown(); platform::shutdown(); return 1;
    }
    int status = 1;
    if (load_data() && ui::init({&g_data, audio::kNoSound})) status = run(options, events);
    ui::shutdown();
    audio::shutdown();
    render::shutdown();
    platform::shutdown();
    return status;
}
