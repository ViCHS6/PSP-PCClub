#include "render/mesh_format.h"

#include <cmath>
#include <cstring>

namespace render::format {

uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>(uint16_t(p[0]) | (uint16_t(p[1]) << 8));
}

uint32_t read_u32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

float read_f32(const uint8_t* p) {
    static_assert(sizeof(float) == 4);
    const uint32_t bits = read_u32(p);
    float result;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

bool valid_texture_name(const char* name) {
    if (!name || !name[0]) return false;
    for (size_t i = 0; i < 32; ++i) {
        const auto c = static_cast<unsigned char>(name[i]);
        if (!c) return true;
        if (c > 127 || c == '/' || c == '\\') return false;
    }
    return false;
}

bool mesh_subset(const MeshView& mesh, uint32_t index, MeshSubset* out) {
    if (!mesh.data || !out || index >= mesh.h.submesh_count) return false;
    const uint8_t* p = mesh.data + 96 + uint64_t(index) * 48;
    out->index_start = read_u32(p);
    out->index_count = read_u32(p + 4);
    std::memcpy(out->texture, p + 8, 32);
    return true;
}

bool decode_vertex(const MeshView& mesh, uint32_t index, DecodedVertex* out) {
    if (!mesh.data || !out || index >= mesh.h.vertex_count) return false;
    const auto& h = mesh.h;
    const uint8_t* p = mesh.data + h.vertices_offset + uint64_t(index) * 16;
    auto position = [&](size_t axis) {
        const uint32_t word = read_u16(p + 8 + axis * 2);
        const int32_t q = word < 32768 ? int32_t(word) : int32_t(word) - 65536;
        return h.pos_offset[axis] + (static_cast<float>(q) / 32768.0f) * h.pos_scale[axis];
    };
    out->u = h.uv_offset[0] + (static_cast<float>(read_u16(p)) / 32768.0f) * h.uv_scale[0];
    out->v = h.uv_offset[1] + (static_cast<float>(read_u16(p + 2)) / 32768.0f) * h.uv_scale[1];
    out->color = read_u32(p + 4);
    out->x = position(0);
    out->y = position(1);
    out->z = position(2);
    return true;
}

bool validate_mesh(const void* data, size_t size, MeshView* out, const char** error) {
    if (out) *out = {};
    if (error) *error = nullptr;
    auto fail = [&](const char* reason) { if (error) *error = reason; return false; };
    if (!data || size < 96) return fail("truncated MSH1 header");
    const auto* p = static_cast<const uint8_t*>(data);
    if (std::memcmp(p, "MSH1", 4) || read_u16(p + 4) != 1) return fail("MSH1 magic/version");
    if (read_u16(p + 6) || read_u32(p + 28)) return fail("MSH1 flags/reserved");
    MeshView view;
    view.data = p;
    auto& h = view.h;
    h.vertex_count = read_u16(p + 8);
    h.submesh_count = read_u16(p + 10);
    h.index_count = read_u32(p + 12);
    h.vertices_offset = read_u32(p + 16);
    h.indices_offset = read_u32(p + 20);
    if (!h.vertex_count || !h.submesh_count || !h.index_count || h.index_count % 3)
        return fail("MSH1 counts");
    const uint64_t subsets_end = 96 + uint64_t(h.submesh_count) * 48;
    const uint64_t vertices = (subsets_end + 15) & ~uint64_t(15);
    const uint64_t indices = vertices + uint64_t(h.vertex_count) * 16;
    const uint64_t end = indices + uint64_t(h.index_count) * 2;
    if (read_u32(p + 24) != 96 || h.vertices_offset != vertices || h.indices_offset != indices ||
        (h.vertices_offset & 15) || (h.indices_offset & 1) || ((end + 3) & ~uint64_t(3)) != size)
        return fail("MSH1 offsets/file size");
    // Только после точной проверки размера можно обращаться к секциям.
    for (uint64_t i = subsets_end; i < vertices; ++i)
        if (p[i]) return fail("MSH1 section padding");
    for (uint64_t i = end; i < size; ++i)
        if (p[i]) return fail("MSH1 trailing padding");
    for (size_t i = 0; i < 16; ++i)
        if (!std::isfinite(read_f32(p + 32 + i * 4))) return fail("MSH1 nonfinite float");
    for (size_t a = 0; a < 3; ++a) {
        h.pos_offset[a] = read_f32(p + 32 + a * 4);
        h.pos_scale[a] = read_f32(p + 44 + a * 4);
        h.bbox_min[a] = read_f32(p + 72 + a * 4);
        h.bbox_max[a] = read_f32(p + 84 + a * 4);
        if (h.pos_scale[a] <= 0.0f || h.bbox_min[a] > h.bbox_max[a]) return fail("MSH1 scale/bounds");
    }
    for (size_t a = 0; a < 2; ++a) {
        h.uv_offset[a] = read_f32(p + 56 + a * 4);
        h.uv_scale[a] = read_f32(p + 64 + a * 4);
        if (h.uv_scale[a] <= 0.0f) return fail("MSH1 UV scale");
    }
    uint64_t covered = 0;
    for (uint32_t i = 0; i < h.submesh_count; ++i) {
        const uint8_t* s = p + 96 + uint64_t(i) * 48;
        const uint32_t count = read_u32(s + 4);
        if (read_u32(s) != covered || !count || count % 3 || covered + count > h.index_count)
            return fail("MSH1 subset partition");
        covered += count;
        if (read_u32(s + 40) || read_u32(s + 44)) return fail("MSH1 subset reserved");
        size_t len = 0;
        while (len < 32 && s[8 + len]) ++len;
        if (len == 32) return fail("MSH1 texture terminator");
        if (len && !valid_texture_name(reinterpret_cast<const char*>(s + 8)))
            return fail("MSH1 texture ASCII id");
        for (size_t j = len; j < 32; ++j)
            if (s[8 + j]) return fail("MSH1 texture padding");
    }
    if (covered != h.index_count) return fail("MSH1 incomplete subset partition");
    for (uint32_t i = 0; i < h.vertex_count; ++i) {
        if (read_u16(p + vertices + uint64_t(i) * 16 + 14)) return fail("MSH1 vertex padding");
        DecodedVertex v;
        decode_vertex(view, i, &v);
        if (!std::isfinite(v.u) || !std::isfinite(v.v) || !std::isfinite(v.x) ||
            !std::isfinite(v.y) || !std::isfinite(v.z)) return fail("MSH1 decoded float overflow");
    }
    for (uint64_t i = 0; i < h.index_count; ++i)
        if (read_u16(p + indices + i * 2) >= h.vertex_count) return fail("MSH1 index range");
    if (out) *out = view;
    return true;
}

}  // namespace render::format
