#include "tools/minigame_runner_screenshot.h"

#include <SDL.h>
#include <SDL_opengl.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

namespace minigame_runner {

bool save_screenshot(const char* path) {
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    const int width = viewport[2], height = viewport[3];
    if (width <= 0 || height <= 0) return false;
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    std::vector<uint8_t> flipped(pixels.size());
    GLint previous_buffer = 0;
    glGetIntegerv(GL_READ_BUFFER, &previous_buffer);
    glReadBuffer(GL_FRONT);
    glReadPixels(viewport[0], viewport[1], width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glReadBuffer(static_cast<GLenum>(previous_buffer));
    if (glGetError() != GL_NO_ERROR) {
        std::fprintf(stderr, "--screenshot: OpenGL read failed\n");
        return false;
    }
    for (int y = 0; y < height; ++y)
        std::memcpy(flipped.data() + static_cast<size_t>(y) * width * 4,
                    pixels.data() + static_cast<size_t>(height - 1 - y) * width * 4,
                    static_cast<size_t>(width) * 4);
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(
        flipped.data(), width, height, 32, width * 4, SDL_PIXELFORMAT_ABGR8888);
    const bool saved = surface && SDL_SaveBMP(surface, path) == 0;
    if (surface) SDL_FreeSurface(surface);
    if (!saved) std::fprintf(stderr, "--screenshot: %s: %s\n", path, SDL_GetError());
    return saved;
}

}  // namespace minigame_runner
