#pragma once
// Внутреннее для реализаций рендера — не для игрового кода.
#include <cstddef>
#include <cstdint>

#include "render/render_api.h"
#include "render/mesh_format.h"

namespace render::internal {

// Линейный распределитель поверх одного блока, выделенного при init().
struct Arena {
    uint8_t* base = nullptr;
    size_t size = 0;
    size_t used = 0;

    // nullptr — не хватило места. align — степень двойки.
    void* alloc(size_t bytes, size_t align);
};

struct TextureInfo {
    char name[32]{};
    uint16_t frame_count = 1;
    uint16_t frame = 0;
    bool transparent = false;
};

// Бэкенд возвращает nullptr для несуществующего ресурса.
TextureInfo* texture_info(TextureId id);
void resource_writeback(const void* data, size_t size);

struct ModelSubset {
    uint32_t index_start, index_count;
    TextureId texture;
};

struct Model {
    format::MeshHeader h;
    const void* vertices;
    const uint16_t* indices;
    const ModelSubset* subsets;
};

bool model_resources_init(const Config& config, Arena* arena, bool packed);
void model_resources_shutdown();
const Model* model_resource(ModelId id);

// Временный буфер загрузки ограничивается доступным бюджетом ресурса.
size_t mesh_file_limit();
size_t texture_file_limit();
void* read_resource_file(const char* path, size_t limit, size_t* size);

// Статистика, которую заполняют бэкенды.
FrameStats& mutable_stats();

// Строит атлас отладочного шрифта. Бэкенд вызывает в конце своего init().
bool debug_font_init();

}  // namespace render::internal
