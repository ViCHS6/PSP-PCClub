#pragma once
#include <cstddef>
#include <cstdint>

namespace render::format {

uint16_t read_u16(const uint8_t* p);
uint32_t read_u32(const uint8_t* p);
float read_f32(const uint8_t* p);
bool valid_texture_name(const char* name);

struct MeshHeader {
    uint16_t vertex_count, submesh_count;
    uint32_t index_count, vertices_offset, indices_offset;
    float pos_offset[3], pos_scale[3], uv_offset[2], uv_scale[2];
    float bbox_min[3], bbox_max[3];
};

struct MeshView {
    const uint8_t* data = nullptr;
    MeshHeader h{};
};

struct MeshSubset {
    uint32_t index_start, index_count;
    char texture[32];
};

struct DecodedVertex {
    float u, v;
    uint32_t color;
    float x, y, z;
};

// На ошибке out обнуляется; указатели выдаются только после полной проверки файла.
bool validate_mesh(const void* data, size_t size, MeshView* out, const char** error);
bool decode_vertex(const MeshView& mesh, uint32_t index, DecodedVertex* out);
bool mesh_subset(const MeshView& mesh, uint32_t index, MeshSubset* out);

}  // namespace render::format
