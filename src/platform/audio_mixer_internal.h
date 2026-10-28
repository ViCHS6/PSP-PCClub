#pragma once

#include <cstddef>
#include <cstdint>

namespace audio::internal {

struct WavInfo {
    uint32_t rate = 0;
    uint16_t channels = 0;
    size_t data_offset = 0;
    size_t data_bytes = 0;
};

bool parse_wav(const void* data, size_t size, WavInfo* out);

// Только при остановленном устройстве: тот же колбэк, что вызывает платформа.
void mix(int16_t* dst, int frames, void* user = nullptr);
constexpr int kFadeFrames = 221;
constexpr uint32_t kCommandCapacity = 128;

}  // namespace audio::internal
