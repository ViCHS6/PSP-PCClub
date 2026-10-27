// Платформа PC: SDL2 + OpenGL 2.1. Нужна для отладки, тестов и balance-runner.
#include <SDL.h>
#include <malloc.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "platform/pc/pc_options.h"
#include "platform/platform_api.h"

namespace platform {
namespace {

constexpr int kWindowScale = 2;
constexpr float kDeadZone = 0.2f;

SDL_Window* g_window = nullptr;
SDL_GLContext g_gl = nullptr;
SDL_GameController* g_pad = nullptr;
InputState g_input;
PcOptions g_options;
bool g_exit = false;
int g_frame = 0;
char g_base_path[512] = "";
size_t g_heap_peak = 0;

size_t heap_used() {
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
    return mallinfo2().uordblks;
#else
    return 0;
#endif
}

float apply_dead_zone(float v) {
    const float a = std::fabs(v);
    if (a < kDeadZone) return 0.0f;
    const float scaled = (a - kDeadZone) / (1.0f - kDeadZone);
    return std::copysign(scaled > 1.0f ? 1.0f : scaled, v);
}

void parse_args(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            g_options.max_frames = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            g_options.screenshot_path = argv[++i];
        }
    }
    if (g_options.screenshot_path && g_options.max_frames <= 0) g_options.max_frames = 60;
}

bool build_path(const char* path, char* out, size_t capacity) {
    if (!path || path[0] == '/' || std::strchr(path, ':')) return false;
    const int n = std::snprintf(out, capacity, "%s%s", g_base_path, path);
    return n > 0 && static_cast<size_t>(n) < capacity;
}

uint32_t read_buttons() {
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    uint32_t b = 0;
    if (keys[SDL_SCANCODE_UP]) b |= kButtonUp;
    if (keys[SDL_SCANCODE_DOWN]) b |= kButtonDown;
    if (keys[SDL_SCANCODE_LEFT]) b |= kButtonLeft;
    if (keys[SDL_SCANCODE_RIGHT]) b |= kButtonRight;
    if (keys[SDL_SCANCODE_K]) b |= kButtonCross;
    if (keys[SDL_SCANCODE_L]) b |= kButtonCircle;
    if (keys[SDL_SCANCODE_J]) b |= kButtonSquare;
    if (keys[SDL_SCANCODE_I]) b |= kButtonTriangle;
    if (keys[SDL_SCANCODE_Q]) b |= kButtonL;
    if (keys[SDL_SCANCODE_E]) b |= kButtonR;
    if (keys[SDL_SCANCODE_RETURN]) b |= kButtonStart;
    if (keys[SDL_SCANCODE_BACKSPACE]) b |= kButtonSelect;

    if (g_pad) {
        struct Map {
            SDL_GameControllerButton pad;
            Button button;
        };
        static const Map kMap[] = {
            {SDL_CONTROLLER_BUTTON_DPAD_UP, kButtonUp},
            {SDL_CONTROLLER_BUTTON_DPAD_DOWN, kButtonDown},
            {SDL_CONTROLLER_BUTTON_DPAD_LEFT, kButtonLeft},
            {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, kButtonRight},
            {SDL_CONTROLLER_BUTTON_A, kButtonCross},
            {SDL_CONTROLLER_BUTTON_B, kButtonCircle},
            {SDL_CONTROLLER_BUTTON_X, kButtonSquare},
            {SDL_CONTROLLER_BUTTON_Y, kButtonTriangle},
            {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, kButtonL},
            {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, kButtonR},
            {SDL_CONTROLLER_BUTTON_START, kButtonStart},
            {SDL_CONTROLLER_BUTTON_BACK, kButtonSelect},
        };
        for (const Map& m : kMap) {
            if (SDL_GameControllerGetButton(g_pad, m.pad)) b |= m.button;
        }
    }
    return b;
}

void read_stick(float* x, float* y) {
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    float kx = static_cast<float>(keys[SDL_SCANCODE_D]) - static_cast<float>(keys[SDL_SCANCODE_A]);
    float ky = static_cast<float>(keys[SDL_SCANCODE_W]) - static_cast<float>(keys[SDL_SCANCODE_S]);
    if (g_pad) {
        const float px = apply_dead_zone(SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f);
        const float py = -apply_dead_zone(SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f);
        if (std::fabs(px) > std::fabs(kx)) kx = px;
        if (std::fabs(py) > std::fabs(ky)) ky = py;
    }
    *x = kx;
    *y = ky;
}

}  // namespace

const PcOptions& pc_options() { return g_options; }

bool init(const InitParams& params) {
    parse_args(params.argc, params.argv);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    if (char* base = SDL_GetBasePath()) {
        std::snprintf(g_base_path, sizeof(g_base_path), "%s", base);
        SDL_free(base);
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    g_window = SDL_CreateWindow("Компьютерный клуб (PC)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                480 * kWindowScale, 272 * kWindowScale,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!g_window) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }
    g_gl = SDL_GL_CreateContext(g_window);
    if (!g_gl) {
        std::fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError());
        return false;
    }
    SDL_GL_SetSwapInterval(1);
    return true;
}

void shutdown() {
    audio_stop();
    if (g_pad) SDL_GameControllerClose(g_pad);
    if (g_gl) SDL_GL_DeleteContext(g_gl);
    if (g_window) SDL_DestroyWindow(g_window);
    SDL_Quit();
}

bool poll() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            g_exit = true;
        } else if (e.type == SDL_CONTROLLERDEVICEADDED && !g_pad) {
            g_pad = SDL_GameControllerOpen(e.cdevice.which);
        } else if (e.type == SDL_CONTROLLERDEVICEREMOVED && g_pad &&
                   e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pad))) {
            SDL_GameControllerClose(g_pad);
            g_pad = nullptr;
        }
    }

    const uint32_t prev = g_input.held;
    g_input.held = read_buttons();
    g_input.pressed = g_input.held & ~prev;
    g_input.released = prev & ~g_input.held;
    read_stick(&g_input.stick_x, &g_input.stick_y);

    const size_t used = heap_used();
    if (used > g_heap_peak) g_heap_peak = used;

    ++g_frame;
    if (g_options.max_frames > 0 && g_frame > g_options.max_frames) g_exit = true;
    return !g_exit;
}

void request_exit() { g_exit = true; }

const char* name() { return "PC"; }

const InputState& input() { return g_input; }

uint64_t time_us() {
    static const uint64_t freq = SDL_GetPerformanceFrequency();
    const uint64_t t = SDL_GetPerformanceCounter();
    return (t / freq) * 1000000u + (t % freq) * 1000000u / freq;
}

bool file_size(const char* path, size_t* out_size) {
    char full[1024];
    if (!build_path(path, full, sizeof(full))) return false;
    FILE* f = std::fopen(full, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fclose(f);
    if (size < 0) return false;
    *out_size = static_cast<size_t>(size);
    return true;
}

bool file_read(const char* path, void* dst, size_t capacity, size_t* out_read) {
    size_t size = 0;
    if (!file_size(path, &size) || size > capacity) return false;
    char full[1024];
    build_path(path, full, sizeof(full));
    FILE* f = std::fopen(full, "rb");
    if (!f) return false;
    const size_t got = std::fread(dst, 1, size, f);
    std::fclose(f);
    if (got != size) return false;
    if (out_read) *out_read = got;
    return true;
}

MemoryStats memory_stats() {
    MemoryStats s;
    s.heap_used = heap_used();
    s.heap_peak = g_heap_peak > s.heap_used ? g_heap_peak : s.heap_used;
    return s;
}

bool audio_start(AudioCallback, void*) { return false; }

void audio_stop() {}

}  // namespace platform
