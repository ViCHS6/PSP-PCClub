#include "tools/minigame_runner_input.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace minigame_runner {
namespace {

struct ButtonName { const char* name; uint32_t button; };
constexpr ButtonName kButtons[] = {
    {"up", platform::kButtonUp}, {"down", platform::kButtonDown},
    {"left", platform::kButtonLeft}, {"right", platform::kButtonRight},
    {"cross", platform::kButtonCross}, {"square", platform::kButtonSquare},
    {"triangle", platform::kButtonTriangle}, {"circle", platform::kButtonCircle},
    {"start", platform::kButtonStart}, {"l", platform::kButtonL},
    {"r", platform::kButtonR}, {"select", platform::kButtonSelect},
};

bool button_mask(char* token, uint32_t& mask) {
    for (const auto& button : kButtons) {
        if (std::strcmp(token, button.name) == 0) {
            mask |= button.button;
            return true;
        }
    }
    return false;
}

}  // namespace

bool load_input(const char* path, std::vector<InputEvent>& events) {
    FILE* file = std::fopen(path, "rb");
    if (!file) {
        std::fprintf(stderr, "--input: cannot open %s\n", path);
        return false;
    }
    events.clear();
    char line[1024];
    unsigned line_number = 0;
    bool valid = true;
    while (std::fgets(line, sizeof(line), file)) {
        ++line_number;
        if (!std::strchr(line, '\n') && !std::feof(file)) { valid = false; break; }
        if (char* comment = std::strchr(line, '#')) *comment = '\0';
        char* token = std::strtok(line, " \t\r\n");
        if (!token) continue;
        char* end = nullptr;
        errno = 0;
        const unsigned long long frame = std::strtoull(token, &end, 10);
        if (token[0] < '0' || token[0] > '9' || *end || errno || frame > UINT32_MAX ||
            (!events.empty() && frame <= events.back().frame)) { valid = false; break; }
        token = std::strtok(nullptr, " \t\r\n,+");
        if (!token) { valid = false; break; }
        uint32_t mask = 0;
        const bool release = std::strcmp(token, "-") == 0;
        if (!release && !button_mask(token, mask)) { valid = false; break; }
        while ((token = std::strtok(nullptr, " \t\r\n,+"))) {
            if (release || !button_mask(token, mask)) { valid = false; break; }
        }
        if (!valid) break;
        events.push_back({static_cast<uint32_t>(frame), mask});
    }
    if (std::ferror(file)) valid = false;
    std::fclose(file);
    if (!valid) {
        std::fprintf(stderr, "--input: invalid line %u in %s\n", line_number, path);
        events.clear();
    }
    return valid;
}

platform::InputState scripted_input(const std::vector<InputEvent>& events, uint64_t frame,
                                    size_t& next, uint32_t& held) {
    const uint32_t previous = held;
    while (next < events.size() && events[next].frame <= frame) held = events[next++].held;
    platform::InputState input;
    input.held = held;
    input.pressed = held & ~previous;
    input.released = previous & ~held;
    return input;
}

}  // namespace minigame_runner
