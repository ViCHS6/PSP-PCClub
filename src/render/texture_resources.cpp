#include "render/render_internal.h"
#include "platform/platform_api.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace render {
namespace internal {

void* read_resource_file(const char* path, size_t limit, size_t* size) {
    if (!path) {
        std::fprintf(stderr, "resource: null path\n");
        return nullptr;
    }
    if (!platform::file_size(path, size) || !*size || *size > limit) {
        std::fprintf(stderr, "%s: cannot read file or resource budget exceeded\n", path);
        return nullptr;
    }
    void* data = std::malloc(*size);
    size_t got = 0;
    if (!data || !platform::file_read(path, data, *size, &got) || got != *size) {
        std::fprintf(stderr, "%s: allocation/read failed\n", path);
        std::free(data);
        return nullptr;
    }
    return data;
}

}  // namespace internal

TextureId texture_find(const char* name) {
    if (!format::valid_texture_name(name)) return kNoTexture;
    for (uint32_t i = 0; i < kNoTexture; ++i) {
        auto* info = internal::texture_info(static_cast<TextureId>(i));
        if (!info) break;
        if (!std::strcmp(info->name, name)) return static_cast<TextureId>(i);
    }
    return kNoTexture;
}

uint16_t texture_frame_count(TextureId texture) {
    const auto* info = internal::texture_info(texture);
    return info ? info->frame_count : 0;
}

void texture_set_frame(TextureId texture, uint16_t frame) {
    auto* info = internal::texture_info(texture);
    if (info) info->frame = frame < info->frame_count ? frame : static_cast<uint16_t>(info->frame_count - 1);
}

TextureId texture_load_file(const char* path) {
    if (!path) {
        std::fprintf(stderr, "TEX1: null path\n");
        return kNoTexture;
    }
    const char* basename = path;
    for (const char* p = path; *p; ++p)
        if (*p == '/' || *p == '\\') basename = p + 1;
    const char* dot = std::strrchr(basename, '.');
    const size_t len = dot ? static_cast<size_t>(dot - basename) : std::strlen(basename);
    char name[32]{};
    if (!len || len > 31) {
        std::fprintf(stderr, "%s: texture id must contain 1..31 ASCII bytes\n", path);
        return kNoTexture;
    }
    std::memcpy(name, basename, len);
    if (!format::valid_texture_name(name) || texture_find(name) != kNoTexture) {
        std::fprintf(stderr, "%s: invalid or duplicate texture id\n", path);
        return kNoTexture;
    }
    size_t size = 0;
    void* data = internal::read_resource_file(path, internal::texture_file_limit(), &size);
    if (!data) return kNoTexture;
    const TextureId id = texture_load_tex(name, data, size);
    if (id == kNoTexture) std::fprintf(stderr, "%s: texture load failed\n", path);
    std::free(data);
    return id;
}

}  // namespace render
