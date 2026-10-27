#include "render/render_internal.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace render {
namespace {

internal::Arena* g_arena = nullptr;
internal::Model* g_models = nullptr;
internal::ModelSubset* g_subsets = nullptr;
Config g_config;
uint16_t g_model_count = 0;
uint32_t g_subset_count = 0;
bool g_packed = false;

ModelId fail(const char* error) {
    std::fprintf(stderr, "MSH1: %s\n", error);
    return kInvalidModel;
}

}  // namespace

namespace internal {

bool model_resources_init(const Config& config, Arena* arena, bool packed) {
    g_config = config;
    g_arena = arena;
    g_packed = packed;
    g_model_count = 0;
    g_subset_count = 0;
    if (packed && (reinterpret_cast<uintptr_t>(arena->base) & 15)) return false;
    // Даже нулевые лимиты допустимы: тогда загрузчик возвращает ошибку.
    g_models = static_cast<Model*>(std::calloc(config.max_models ? config.max_models : 1, sizeof(Model)));
    g_subsets = static_cast<ModelSubset*>(std::calloc(config.max_submeshes ? config.max_submeshes : 1,
                                                   sizeof(ModelSubset)));
    return g_models && g_subsets;
}

void model_resources_shutdown() {
    std::free(g_models);
    std::free(g_subsets);
    g_models = nullptr;
    g_subsets = nullptr;
    g_arena = nullptr;
    g_model_count = 0;
    g_subset_count = 0;
}

const Model* model_resource(ModelId id) {
    return id < g_model_count ? g_models + id : nullptr;
}

size_t mesh_file_limit() {
    if (!g_arena || g_arena->used > g_arena->size) return 0;
    // Packed-файл не больше декодированного буфера и оставшейся таблицы подсетов.
    const uint64_t remaining = g_arena->size - g_arena->used;
    const uint64_t overhead = 96 + uint64_t(g_config.max_submeshes - g_subset_count) * 48 + 16;
    const size_t maximum = std::numeric_limits<size_t>::max();
    if (remaining > uint64_t(maximum) - overhead) return maximum;
    return static_cast<size_t>(remaining + overhead);
}

}  // namespace internal

ModelId model_load_msh(const void* data, size_t size) {
    format::MeshView view;
    const char* error;
    if (!format::validate_mesh(data, size, &view, &error)) return fail(error);
    const auto& h = view.h;
    if (!g_arena || !g_models || !g_subsets) return fail("renderer not initialized");
    if (g_model_count >= g_config.max_models ||
        uint64_t(g_subset_count) + h.submesh_count > g_config.max_submeshes)
        return fail("model/submesh limit");

    // Пробная арена делает загрузку транзакцией: отказ не расходует память.
    internal::Arena pending = *g_arena;
    const void* vertices;
    const uint16_t* indices;
    if (g_packed) {
        auto* blob = static_cast<uint8_t*>(pending.alloc(size, 16));
        if (!blob) return fail("mesh arena exhausted");
        std::memcpy(blob, data, size);
        internal::resource_writeback(blob, size);
        vertices = blob + h.vertices_offset;
        indices = reinterpret_cast<const uint16_t*>(blob + h.indices_offset);
    } else {
        const uint64_t vbytes = uint64_t(h.vertex_count) * sizeof(Vertex);
        const uint64_t ibytes = uint64_t(h.index_count) * sizeof(uint16_t);
        if (vbytes > pending.size || ibytes > pending.size) return fail("mesh arena exhausted");
        auto* v = static_cast<Vertex*>(pending.alloc(static_cast<size_t>(vbytes), 16));
        auto* idx = static_cast<uint16_t*>(pending.alloc(static_cast<size_t>(ibytes), 16));
        if (!v || !idx) return fail("mesh arena exhausted");
        for (uint32_t i = 0; i < h.vertex_count; ++i) {
            format::DecodedVertex decoded;
            format::decode_vertex(view, i, &decoded);
            v[i] = {decoded.u, decoded.v, decoded.color, decoded.x, decoded.y, decoded.z};
        }
        for (uint64_t i = 0; i < h.index_count; ++i)
            idx[i] = format::read_u16(view.data + h.indices_offset + i * 2);
        vertices = v;
        indices = idx;
    }
    internal::ModelSubset* subsets = g_subsets + g_subset_count;
    for (uint32_t i = 0; i < h.submesh_count; ++i) {
        format::MeshSubset s;
        format::mesh_subset(view, i, &s);
        const TextureId texture = s.texture[0] ? texture_find(s.texture) : kNoTexture;
        if (s.texture[0] && texture == kNoTexture)
            std::fprintf(stderr, "MSH1: unknown texture '%s', using vertex colors\n", s.texture);
        subsets[i] = {s.index_start, s.index_count, texture};
    }
    g_models[g_model_count] = {h, vertices, indices, subsets};
    *g_arena = pending;
    g_subset_count += h.submesh_count;
    internal::mutable_stats().mesh_arena_used = g_arena->used;
    return g_model_count++;
}

bool model_bounds(ModelId model, core::Vec3* out_min, core::Vec3* out_max) {
    const internal::Model* m = internal::model_resource(model);
    if (!m) return false;
    if (out_min) *out_min = {m->h.bbox_min[0], m->h.bbox_min[1], m->h.bbox_min[2]};
    if (out_max) *out_max = {m->h.bbox_max[0], m->h.bbox_max[1], m->h.bbox_max[2]};
    return true;
}

ModelId model_load_file(const char* path) {
    size_t size = 0;
    void* data = internal::read_resource_file(path, internal::mesh_file_limit(), &size);
    if (!data) return kInvalidModel;
    const ModelId id = model_load_msh(data, size);
    if (id == kInvalidModel) std::fprintf(stderr, "%s: model load failed\n", path);
    std::free(data);
    return id;
}

}  // namespace render
