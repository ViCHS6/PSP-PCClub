#include "platform/platform_api.h"

#include <SDL.h>
#include <cstring>

namespace platform {
namespace {
SDL_AudioDeviceID g_device = 0;
AudioCallback g_callback = nullptr;
void* g_user = nullptr;
bool g_audio_subsystem = false;

void callback(void*, Uint8* dst, int bytes) {
    const int block_bytes = kAudioFrames * 2 * int(sizeof(int16_t));
    for (int offset = 0; offset < bytes; offset += block_bytes) {
        if (bytes - offset < block_bytes) {
            std::memset(dst + offset, 0, size_t(bytes - offset));
            break;
        }
        g_callback(reinterpret_cast<int16_t*>(dst + offset), kAudioFrames, g_user);
    }
}
}  // namespace

bool audio_start(AudioCallback cb, void* user) {
    if (g_device || !cb) return false;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO)) return false;
    g_audio_subsystem = true;
    SDL_AudioSpec wanted{}, actual{};
    wanted.freq = kAudioRate;
    wanted.format = AUDIO_S16SYS;
    wanted.channels = 2;
    wanted.samples = kAudioFrames;
    wanted.callback = callback;
    g_callback = cb;
    g_user = user;
    g_device = SDL_OpenAudioDevice(nullptr, 0, &wanted, &actual, 0);
    if (!g_device || actual.freq != kAudioRate || actual.channels != 2 ||
        actual.format != AUDIO_S16SYS || actual.samples != kAudioFrames) {
        audio_stop();
        return false;
    }
    SDL_PauseAudioDevice(g_device, 0);
    return true;
}

void audio_stop() {
    if (g_device) SDL_CloseAudioDevice(g_device);
    g_device = 0;
    g_callback = nullptr;
    g_user = nullptr;
    if (g_audio_subsystem) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    g_audio_subsystem = false;
}
}  // namespace platform
