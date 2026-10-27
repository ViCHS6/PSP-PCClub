#include <doctest/doctest.h>
#include <algorithm>
#include <array>
#include <cstring>

#include "render/render_internal.h"
#include "render/texture_format.h"
#include "platform/platform_api.h"
#include "fixtures.h"

using namespace render_test;

namespace {

std::array<render::internal::TextureInfo, 2> g_textures;
uint16_t g_texture_count = 0;
Bytes g_file;
size_t g_writeback_bytes = 0;
const void* g_writeback_data = nullptr;
size_t g_reads = 0;

struct Resources {
    alignas(16) std::array<uint8_t, 4096> bytes{};
    render::internal::Arena arena;

    explicit Resources(bool packed = false, size_t budget = 4096, uint16_t models = 2, uint16_t subsets = 2) {
        g_texture_count = 0;
        g_textures = {};
        g_file.clear();
        g_reads = g_writeback_bytes = 0;
        g_writeback_data = nullptr;
        render::internal::mutable_stats() = {};
        arena = {bytes.data(), budget, 0};
        render::Config config;
        config.max_models = models;
        config.max_submeshes = subsets;
        REQUIRE(render::internal::model_resources_init(config, &arena, packed));
    }

    ~Resources() { render::internal::model_resources_shutdown(); }
};

}  // namespace

// Заглушки платформы и GPU: тестируем общие ресурсы и файловые обёртки без окна GL.
namespace platform {

bool file_size(const char* path, size_t* size) {
    if (!path || !std::strcmp(path, "missing") || g_file.empty()) return false;
    *size = g_file.size();
    return true;
}

bool file_read(const char*, void* dst, size_t capacity, size_t* read) {
    ++g_reads;
    if (g_file.size() > capacity) return false;
    std::memcpy(dst, g_file.data(), g_file.size());
    *read = g_file.size();
    return true;
}

}  // namespace platform

namespace render {

TextureId texture_create(int, int, TextureFormat, const void*) { return kNoTexture; }
void draw_sprites(TextureId, const Sprite*, int) {}

TextureId texture_load_tex(const char* name, const void* data, size_t size) {
    format::TextureView view;
    if (!format::valid_texture_name(name) || texture_find(name) != kNoTexture ||
        g_texture_count == g_textures.size() || !format::validate_texture(data, size, &view, nullptr))
        return kNoTexture;
    auto& info = g_textures[g_texture_count];
    std::strcpy(info.name, name);
    info.frame_count = view.h.frame_count;
    return g_texture_count++;
}

namespace internal {

TextureInfo* texture_info(TextureId id) { return id < g_texture_count ? &g_textures[id] : nullptr; }
size_t texture_file_limit() { return 4096; }
void resource_writeback(const void* data, size_t size) {
    g_writeback_data = data;
    g_writeback_bytes = size;
}

}  // namespace internal
}  // namespace render

TEST_CASE("model resources decode once resolve textures and preserve original AABB") {
    Resources resources;
    g_file = texture(4, 8, 8, 2);
    const auto texture_id = render::texture_load_file("assets/tex.desk.tex");
    REQUIRE(texture_id != render::kNoTexture);
    CHECK(render::texture_find("tex.desk") == texture_id);
    CHECK(render::texture_find("unknown") == render::kNoTexture);
    auto b = quad();
    std::memcpy(b.data() + 104, "tex.desk", 8);
    const auto id = render::model_load_msh(b.data(), b.size());
    REQUIRE(id != render::kInvalidModel);
    const auto* model = render::internal::model_resource(id);
    REQUIRE(model != nullptr);
    CHECK(model->subsets[0].texture == texture_id);
    const auto* vertices = static_cast<const render::Vertex*>(model->vertices);
    CHECK(vertices[0].u == -1.0f);
    CHECK(vertices[2].x == doctest::Approx(1.9999694824f));
    CHECK(model->indices[5] == 3);
    // После загрузки исходный буфер больше не нужен.
    std::fill(b.begin(), b.end(), 0);
    CHECK(vertices[2].color == 0xFF404040);
    core::Vec3 min, max;
    CHECK(render::model_bounds(id, &min, &max));
    CHECK(min.x == 0.0f);
    CHECK(max.x == 2.0f);
    CHECK(max.z == 0.0f);
    CHECK(render::model_bounds(id, nullptr, nullptr));
    CHECK_FALSE(render::model_bounds(render::kInvalidModel, &min, &max));
    CHECK(g_writeback_bytes == 0);
}

TEST_CASE("model resources copy packed file aligned and write back complete blob") {
    Resources resources(true);
    const auto b = quad();
    const auto id = render::model_load_msh(b.data(), b.size());
    REQUIRE(id != render::kInvalidModel);
    const auto* m = render::internal::model_resource(id);
    REQUIRE(m != nullptr);
    CHECK(resources.arena.used == b.size());
    CHECK((reinterpret_cast<uintptr_t>(g_writeback_data) & 15) == 0);
    CHECK(g_writeback_bytes == b.size());
    CHECK(std::memcmp(g_writeback_data, b.data(), b.size()) == 0);
    CHECK(m->vertices == static_cast<const uint8_t*>(g_writeback_data) + 144);
    CHECK(m->indices == reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(g_writeback_data) + 208));
}

TEST_CASE("model failures do not consume arena models or subsets") {
    for (bool packed : {false, true}) {
        Resources resources(packed);
        auto b = quad(); b[0] = 0;
        CHECK(render::model_load_msh(b.data(), b.size()) == render::kInvalidModel);
        CHECK(resources.arena.used == 0);
        b = quad();
        REQUIRE(render::model_load_msh(b.data(), b.size()) == 0);
        const size_t used = resources.arena.used;
        b[158] = 1;
        CHECK(render::model_load_msh(b.data(), b.size()) == render::kInvalidModel);
        CHECK(resources.arena.used == used);
        b[158] = 0;
        REQUIRE(render::model_load_msh(b.data(), b.size()) == 1);
        const size_t full = resources.arena.used;
        CHECK(render::model_load_msh(b.data(), b.size()) == render::kInvalidModel);
        CHECK(resources.arena.used == full);
    }
}

TEST_CASE("model arena exhaustion rolls back even when vertices would fit") {
    Resources resources(false, 100);
    const auto b = quad();
    CHECK(render::model_load_msh(b.data(), b.size()) == render::kInvalidModel);
    CHECK(resources.arena.used == 0);
    CHECK(render::stats().mesh_arena_used == 0);
}

TEST_CASE("model limits include all subsets and unknown textures use vertex colors") {
    Resources resources(false, 4096, 2, 1);
    auto b = quad(); std::memcpy(b.data() + 104, "missing_texture", 15);
    const auto id = render::model_load_msh(b.data(), b.size());
    REQUIRE(id != render::kInvalidModel);
    CHECK(render::internal::model_resource(id)->subsets[0].texture == render::kNoTexture);
    const size_t used = resources.arena.used;
    CHECK(render::model_load_msh(b.data(), b.size()) == render::kInvalidModel);
    CHECK(resources.arena.used == used);
}

TEST_CASE("multiple subsets share one decoded vertex buffer") {
    Resources resources;
    auto tex = texture(4, 8, 8);
    const auto first = render::texture_load_tex("first", tex.data(), tex.size());
    const auto second = render::texture_load_tex("second", tex.data(), tex.size());
    const auto b = split_quad();
    const auto id = render::model_load_msh(b.data(), b.size());
    REQUIRE(id != render::kInvalidModel);
    const auto* m = render::internal::model_resource(id);
    REQUIRE(m != nullptr);
    CHECK(m->subsets[0].texture == first);
    CHECK(m->subsets[1].texture == second);
    CHECK(m->subsets[1].index_start == 3);
    CHECK(m->subsets[1].index_count == 3);
    CHECK(resources.arena.used == 4 * sizeof(render::Vertex) + 6 * sizeof(uint16_t));
}

TEST_CASE("texture registry and frame selection clamp without creating resources") {
    Resources resources;
    g_file = texture(3, 8, 8, 3);
    const auto id = render::texture_load_file("assets/screen.tex");
    REQUIRE(id != render::kNoTexture);
    CHECK(render::texture_frame_count(id) == 3);
    render::texture_set_frame(id, 1);
    CHECK(render::internal::texture_info(id)->frame == 1);
    render::texture_set_frame(id, 65535);
    CHECK(render::internal::texture_info(id)->frame == 2);
    render::texture_set_frame(id, 0);
    CHECK(render::internal::texture_info(id)->frame == 0);
    render::texture_set_frame(render::kNoTexture, 1);
    CHECK(render::texture_frame_count(render::kNoTexture) == 0);
    CHECK(g_texture_count == 1);
    const size_t reads = g_reads;
    CHECK(render::texture_load_file("assets/screen.tex") == render::kNoTexture);
    CHECK(g_reads == reads);
}

TEST_CASE("file wrappers reject bad paths ids oversized files and malformed files") {
    Resources resources;
    g_file = quad();
    CHECK(render::model_load_file(nullptr) == render::kInvalidModel);
    CHECK(render::model_load_file("missing") == render::kInvalidModel);
    REQUIRE(render::model_load_file("assets/desk.msh") != render::kInvalidModel);
    g_file[0] = 0;
    CHECK(render::model_load_file("assets/bad.msh") == render::kInvalidModel);
    g_file = texture(4, 8, 8);
    CHECK(render::texture_load_file(nullptr) == render::kNoTexture);
    CHECK(render::texture_load_file("assets/.tex") == render::kNoTexture);
    CHECK(render::texture_load_file("assets/abcdefghijklmnopqrstuvwxyz012345.tex") == render::kNoTexture);
    g_file.resize(4097);
    const size_t reads = g_reads;
    CHECK(render::texture_load_file("assets/large.tex") == render::kNoTexture);
    CHECK(g_reads == reads);
}

TEST_CASE("arena allocation rejects overflow and preserves cursor") {
    alignas(16) uint8_t bytes[64];
    render::internal::Arena arena{bytes, sizeof(bytes), 1};
    CHECK(arena.alloc(16, 16) == bytes + 16);
    CHECK(arena.used == 32);
    CHECK(arena.alloc(SIZE_MAX, 16) == nullptr);
    CHECK(arena.used == 32);
    CHECK(arena.alloc(1, SIZE_MAX) == nullptr);
    CHECK(arena.alloc(1, 3) == nullptr);
    CHECK(arena.used == 32);
}
