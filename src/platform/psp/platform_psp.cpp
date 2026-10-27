// Платформа PSP: PSPSDK. Цель — PSP Go и совместимость с PSP-1000.
#include <malloc.h>
#include <pspctrl.h>
#include <pspkernel.h>
#include <psppower.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "platform/platform_api.h"

PSP_MODULE_INFO("pcclub", PSP_MODULE_USER, 0, 1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

// Жёсткий бюджет кучи: 18 МиБ + код и данные ≈ 20 МиБ (AGENTS.md, «Целевое железо»).
// Выход за бюджет — сбой malloc, его видно сразу, а не на PSP-1000 у игрока.
constexpr int kHeapKiB = 18 * 1024;
PSP_HEAP_SIZE_KB(kHeapKiB);

namespace platform {
namespace {

constexpr float kDeadZone = 0.2f;

volatile bool g_exit = false;
InputState g_input;
char g_base_path[256] = "";
size_t g_heap_peak = 0;

int exit_callback(int, int, void*) {
    g_exit = true;
    return 0;
}

int callback_thread(SceSize, void*) {
    const int cbid = sceKernelCreateCallback("exit_callback", exit_callback, nullptr);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

float axis(unsigned char raw) {
    const float v = (static_cast<float>(raw) - 128.0f) / 127.0f;
    const float a = std::fabs(v);
    if (a < kDeadZone) return 0.0f;
    const float scaled = (a - kDeadZone) / (1.0f - kDeadZone);
    return std::copysign(scaled > 1.0f ? 1.0f : scaled, v);
}

uint32_t map_buttons(unsigned int psp) {
    struct Map {
        unsigned int psp;
        Button button;
    };
    static const Map kMap[] = {
        {PSP_CTRL_UP, kButtonUp},           {PSP_CTRL_DOWN, kButtonDown},
        {PSP_CTRL_LEFT, kButtonLeft},       {PSP_CTRL_RIGHT, kButtonRight},
        {PSP_CTRL_CROSS, kButtonCross},     {PSP_CTRL_CIRCLE, kButtonCircle},
        {PSP_CTRL_SQUARE, kButtonSquare},   {PSP_CTRL_TRIANGLE, kButtonTriangle},
        {PSP_CTRL_LTRIGGER, kButtonL},      {PSP_CTRL_RTRIGGER, kButtonR},
        {PSP_CTRL_START, kButtonStart},     {PSP_CTRL_SELECT, kButtonSelect},
    };
    uint32_t b = 0;
    for (const Map& m : kMap) {
        if (psp & m.psp) b |= m.button;
    }
    return b;
}

bool build_path(const char* path, char* out, size_t capacity) {
    if (!path || path[0] == '/' || std::strchr(path, ':')) return false;
    const int n = std::snprintf(out, capacity, "%s%s", g_base_path, path);
    return n > 0 && static_cast<size_t>(n) < capacity;
}

}  // namespace

bool init(const InitParams& params) {
    const int thid = sceKernelCreateThread("callbacks", callback_thread, 0x11, 0xFA0, 0, nullptr);
    if (thid >= 0) sceKernelStartThread(thid, 0, nullptr);

    // Папка приложения из argv[0]: "ef0:/PSP/GAME/PCCLUB/EBOOT.PBP" → "ef0:/PSP/GAME/PCCLUB/".
    if (params.argc > 0 && params.argv && params.argv[0]) {
        std::snprintf(g_base_path, sizeof(g_base_path), "%s", params.argv[0]);
        char* slash = std::strrchr(g_base_path, '/');
        if (slash) slash[1] = '\0'; else g_base_path[0] = '\0';
    }

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    return true;
}

void shutdown() {
    audio_stop();
    sceKernelExitGame();
}

bool poll() {
    SceCtrlData pad;
    sceCtrlPeekBufferPositive(&pad, 1);
    const uint32_t prev = g_input.held;
    g_input.held = map_buttons(pad.Buttons);
    g_input.pressed = g_input.held & ~prev;
    g_input.released = prev & ~g_input.held;
    g_input.stick_x = axis(pad.Lx);
    g_input.stick_y = -axis(pad.Ly);

    const size_t used = mallinfo().uordblks;
    if (used > g_heap_peak) g_heap_peak = used;
    return !g_exit;
}

void request_exit() { g_exit = true; }

const char* name() { return "PSP"; }

const InputState& input() { return g_input; }

uint64_t time_us() { return static_cast<uint64_t>(sceKernelGetSystemTimeWide()); }

bool file_size(const char* path, size_t* out_size) {
    char full[512];
    if (!build_path(path, full, sizeof(full))) return false;
    SceIoStat st;
    if (sceIoGetstat(full, &st) < 0) return false;
    *out_size = static_cast<size_t>(st.st_size);
    return true;
}

bool file_read(const char* path, void* dst, size_t capacity, size_t* out_read) {
    size_t size = 0;
    if (!file_size(path, &size) || size > capacity) return false;
    char full[512];
    build_path(path, full, sizeof(full));
    const SceUID fd = sceIoOpen(full, PSP_O_RDONLY, 0);
    if (fd < 0) return false;
    const int got = sceIoRead(fd, dst, size);
    sceIoClose(fd);
    if (got < 0 || static_cast<size_t>(got) != size) return false;
    if (out_read) *out_read = size;
    return true;
}

MemoryStats memory_stats() {
    MemoryStats s;
    s.heap_used = mallinfo().uordblks;
    s.heap_peak = g_heap_peak > s.heap_used ? g_heap_peak : s.heap_used;
    s.heap_limit = static_cast<size_t>(kHeapKiB) * 1024;
    s.system_free = sceKernelTotalFreeMemSize();
    return s;
}

bool audio_start(AudioCallback, void*) { return false; }

void audio_stop() {}

}  // namespace platform
