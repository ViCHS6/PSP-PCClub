#include "platform/platform_api.h"

#include <pspaudio.h>
#include <pspkernel.h>
#include <atomic>
#include <cstring>

namespace platform {
namespace {
int g_channel = -1;
SceUID g_thread = -1;
std::atomic<uint32_t> g_running{0};
AudioCallback g_callback = nullptr;
void* g_user = nullptr;
alignas(64) int16_t g_buffers[2][kAudioFrames * 2];

int audio_thread(SceSize, void*) {
    int buffer = 0;
    bool played = false;
    while (g_running.load(std::memory_order_acquire)) {
        g_callback(g_buffers[buffer], kAudioFrames, g_user);
        if (sceAudioOutputPannedBlocking(g_channel, PSP_AUDIO_VOLUME_MAX, PSP_AUDIO_VOLUME_MAX,
                                        g_buffers[buffer]) < 0) return 0;
        played = true;
        buffer ^= 1;
    }
    // Дождаться последнего блока, сохраняя двойной буфер до освобождения канала.
    std::memset(g_buffers[buffer], 0, sizeof(g_buffers[buffer]));
    if (played) {
        constexpr int kStopFrames = kAudioRate / 200;
        for (int i = 0; i < kStopFrames; ++i) for (int c = 0; c < 2; ++c) {
            const int32_t last = g_buffers[buffer ^ 1][(kAudioFrames - 1) * 2 + c];
            g_buffers[buffer][i * 2 + c] = int16_t(last * (kStopFrames - i - 1) / kStopFrames);
        }
    }
    sceAudioOutputPannedBlocking(g_channel, PSP_AUDIO_VOLUME_MAX, PSP_AUDIO_VOLUME_MAX, g_buffers[buffer]);
    while (sceAudioGetChannelRestLen(g_channel) > 0) sceKernelDelayThread(1000);
    return 0;
}
}  // namespace

bool audio_start(AudioCallback cb, void* user) {
    if (g_thread >= 0 || !cb) return false;
    g_channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, kAudioFrames, PSP_AUDIO_FORMAT_STEREO);
    if (g_channel < 0) return false;
    g_callback = cb;
    g_user = user;
    // Меньшее число — более высокий приоритет, чем у главного потока.
    const int main_priority = sceKernelGetThreadCurrentPriority();
    g_thread = sceKernelCreateThread("pcclub_audio", audio_thread,
        main_priority > 8 ? main_priority - 8 : 1, 16 * 1024,
        PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU, nullptr);
    if (g_thread < 0) { audio_stop(); return false; }
    g_running.store(1, std::memory_order_release);
    if (sceKernelStartThread(g_thread, 0, nullptr) < 0) {
        sceKernelDeleteThread(g_thread);
        g_thread = -1;
        audio_stop();
        return false;
    }
    return true;
}

void audio_stop() {
    g_running.store(0, std::memory_order_release);
    if (g_thread >= 0) {
        sceKernelWaitThreadEnd(g_thread, nullptr);
        sceKernelDeleteThread(g_thread);
        g_thread = -1;
    }
    if (g_channel >= 0) sceAudioChRelease(g_channel);
    g_channel = -1;
    g_callback = nullptr;
    g_user = nullptr;
}
}  // namespace platform
