// Рендер PSP: GU напрямую, без GUM. Кадровые буферы 16 бит (5650) + Z 16 бит в VRAM.
#include <malloc.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspkernel.h>

#include <cstring>
#include <cstdio>

#include "render/render_internal.h"
#include "render/texture_format.h"

namespace render {
namespace {

constexpr int kBufferWidth = 512;  // ширина строки буфера в пикселях — степень двойки
constexpr size_t kFrameBufferBytes = kBufferWidth * kScreenHeight * 2;
constexpr int kVertex3D = GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D;
constexpr int kVertex2D = GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D;

// Список команд GE на один кадр. 256 КиБ с запасом под 2D-вершины из sceGuGetMemory.
alignas(64) unsigned int g_display_list[64 * 1024];

struct Mesh {
    const Vertex* vertices;
    const uint16_t* indices;
    uint16_t index_count;
};

struct Texture {
    const uint8_t* pixels;
    int width, height;
    internal::TextureInfo info;
    format::TextureHeader h;
};

enum class Mode { kNone, k3D, k2D };

Config g_config;
internal::Arena g_mesh_arena;
internal::Arena g_texture_arena;
Mesh* g_meshes = nullptr;
uint16_t g_mesh_count = 0;
Texture* g_textures = nullptr;
uint16_t g_texture_count = 0;

Mode g_mode = Mode::kNone;
uint32_t g_draw_calls = 0;
uint32_t g_triangles = 0;

void* arena_block(size_t bytes) { return memalign(64, bytes); }

void bind_texture(TextureId id) {
    sceGuDisable(GU_ALPHA_TEST);
    if (id >= g_texture_count) {
        sceGuDisable(GU_TEXTURE_2D);
        return;
    }
    sceGuEnable(GU_TEXTURE_2D);
    const Texture& t = g_textures[id];
    const auto& h = t.h;
    sceGuTexMode(h.format, 0, 0, (h.flags & 1) ? GU_TRUE : GU_FALSE);
    if (h.clut_count) {
        sceGuClutMode(GU_PSM_8888, 0, h.format == GU_PSM_T4 ? 0x0F : 0xFF, 0);
        sceGuClutLoad(h.clut_count / 8, t.pixels + h.clut_offset);
    }
    sceGuTexImage(0, h.width, h.height, h.stride,
                  t.pixels + h.pixels_offset + uint64_t(t.info.frame) * h.frame_bytes);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexWrap(GU_REPEAT, GU_REPEAT);
    sceGuTexFlush();
    if (t.info.transparent) {
        sceGuEnable(GU_ALPHA_TEST);
        sceGuAlphaFunc(GU_GREATER, 0, 0xFF);
    }
}

void enter_3d() {
    if (g_mode == Mode::k3D) return;
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDisable(GU_BLEND);
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
    g_mode = Mode::k3D;
}

void enter_2d() {
    if (g_mode == Mode::k2D) return;
    sceGuDisable(GU_DEPTH_TEST);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    g_mode = Mode::k2D;
}

}  // namespace

namespace internal {

TextureInfo* texture_info(TextureId id) {
    return id < g_texture_count ? &g_textures[id].info : nullptr;
}

void resource_writeback(const void* data, size_t size) {
    sceKernelDcacheWritebackRange(data, size);
}

size_t texture_file_limit() {
    return g_texture_arena.used <= g_texture_arena.size ? g_texture_arena.size - g_texture_arena.used : 0;
}

}  // namespace internal

bool init(const Config& config) {
    g_config = config;
    g_mesh_arena.base = static_cast<uint8_t*>(arena_block(config.mesh_arena_bytes));
    g_mesh_arena.size = config.mesh_arena_bytes;
    g_texture_arena.base = static_cast<uint8_t*>(arena_block(config.texture_arena_bytes));
    g_texture_arena.size = config.texture_arena_bytes;
    g_meshes = static_cast<Mesh*>(calloc(config.max_meshes, sizeof(Mesh)));
    g_textures = static_cast<Texture*>(calloc(config.max_textures, sizeof(Texture)));
    if (!g_mesh_arena.base || !g_texture_arena.base || !g_meshes || !g_textures) return false;
    if (!internal::model_resources_init(config, &g_mesh_arena, true)) return false;

    // VRAM: [кадр 0][кадр 1][Z] — адреса относительно начала VRAM.
    void* draw_buffer = reinterpret_cast<void*>(0);
    void* display_buffer = reinterpret_cast<void*>(kFrameBufferBytes);
    void* depth_buffer = reinterpret_cast<void*>(kFrameBufferBytes * 2);

    sceGuInit();
    sceGuStart(GU_DIRECT, g_display_list);
    sceGuDrawBuffer(GU_PSM_5650, draw_buffer, kBufferWidth);
    sceGuDispBuffer(kScreenWidth, kScreenHeight, display_buffer, kBufferWidth);
    sceGuDepthBuffer(depth_buffer, kBufferWidth);
    sceGuOffset(2048 - kScreenWidth / 2, 2048 - kScreenHeight / 2);
    sceGuViewport(2048, 2048, kScreenWidth, kScreenHeight);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, kScreenWidth, kScreenHeight);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuEnable(GU_DITHER);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    FrameStats& s = internal::mutable_stats();
    s.mesh_arena_size = config.mesh_arena_bytes;
    s.texture_arena_size = config.texture_arena_bytes;
    s.vram_used = kFrameBufferBytes * 3;
    return internal::debug_font_init();
}

void shutdown() {
    sceGuTerm();
    internal::model_resources_shutdown();
    free(g_textures);
    free(g_meshes);
    free(g_texture_arena.base);
    free(g_mesh_arena.base);
    g_textures = nullptr;
    g_meshes = nullptr;
    g_mesh_arena = {};
    g_texture_arena = {};
    g_mesh_count = g_texture_count = 0;
}

MeshId mesh_create(const Vertex* vertices, uint16_t vertex_count, const uint16_t* indices,
                   uint16_t index_count) {
    if (g_mesh_count >= g_config.max_meshes) return kInvalidMesh;
    internal::Arena pending = g_mesh_arena;
    auto* v = static_cast<Vertex*>(pending.alloc(sizeof(Vertex) * vertex_count, 16));
    auto* i = static_cast<uint16_t*>(pending.alloc(sizeof(uint16_t) * index_count, 16));
    if (!v || !i) return kInvalidMesh;
    g_mesh_arena = pending;
    std::memcpy(v, vertices, sizeof(Vertex) * vertex_count);
    std::memcpy(i, indices, sizeof(uint16_t) * index_count);
    // GE читает основную память в обход кэша CPU.
    sceKernelDcacheWritebackRange(v, sizeof(Vertex) * vertex_count);
    sceKernelDcacheWritebackRange(i, sizeof(uint16_t) * index_count);
    g_meshes[g_mesh_count] = {v, i, index_count};
    internal::mutable_stats().mesh_arena_used = g_mesh_arena.used;
    return g_mesh_count++;
}

TextureId texture_create(int width, int height, TextureFormat format, const void* pixels) {
    auto pow2 = [](int n) { return n >= 8 && n <= 512 && (n & (n - 1)) == 0; };
    if (!g_textures || !pixels || format != TextureFormat::kRgba8888 || !pow2(width) || !pow2(height)) return kNoTexture;
    if (g_texture_count >= g_config.max_textures) return kNoTexture;
    const size_t bytes = static_cast<size_t>(width) * height * 4;
    void* dst = g_texture_arena.alloc(bytes, 16);
    if (!dst) return kNoTexture;
    std::memcpy(dst, pixels, bytes);
    sceKernelDcacheWritebackRange(dst, bytes);
    Texture& t = g_textures[g_texture_count];
    t = {};
    t.pixels = static_cast<const uint8_t*>(dst);
    t.width = width;
    t.height = height;
    t.h.format = GU_PSM_8888;
    t.h.width = static_cast<uint16_t>(width);
    t.h.height = static_cast<uint16_t>(height);
    t.h.stride = static_cast<uint16_t>(width);
    t.h.frame_count = 1;
    internal::mutable_stats().texture_arena_used = g_texture_arena.used;
    return g_texture_count++;
}

TextureId texture_load_tex(const char* name, const void* data, size_t size) {
    auto fail = [&](const char* error) {
        std::fprintf(stderr, "TEX1 '%.32s': %s\n", name ? name : "(null)", error);
        return kNoTexture;
    };
    if (!format::valid_texture_name(name)) return fail("invalid ASCII id");
    if (texture_find(name) != kNoTexture) return fail("duplicate id");
    format::TextureView view;
    const char* error;
    if (!format::validate_texture(data, size, &view, &error)) return fail(error);
    if (!g_textures || g_texture_count >= g_config.max_textures) return fail("texture limit");
    internal::Arena pending = g_texture_arena;
    auto* dst = static_cast<uint8_t*>(pending.alloc(size, 16));
    if (!dst) return fail("texture arena exhausted");
    std::memcpy(dst, data, size);
    sceKernelDcacheWritebackRange(dst, size);
    Texture& t = g_textures[g_texture_count];
    t = {};
    t.pixels = dst;
    t.width = view.h.width;
    t.height = view.h.height;
    t.h = view.h;
    std::strcpy(t.info.name, name);
    t.info.frame_count = view.h.frame_count;
    t.info.transparent = (view.h.flags & 2) != 0;
    g_texture_arena = pending;
    internal::mutable_stats().texture_arena_used = g_texture_arena.used;
    return g_texture_count++;
}

void begin_frame(Color clear_color) {
    sceGuStart(GU_DIRECT, g_display_list);
    sceGuClearColor(clear_color);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    g_mode = Mode::kNone;
}

void set_camera(const core::Mat4& view, const core::Mat4& projection) {
    sceGuSetMatrix(GU_PROJECTION, reinterpret_cast<const ScePspFMatrix4*>(projection.m));
    sceGuSetMatrix(GU_VIEW, reinterpret_cast<const ScePspFMatrix4*>(view.m));
}

void draw_mesh(MeshId mesh, const core::Mat4& model, TextureId texture) {
    if (mesh >= g_mesh_count) return;
    enter_3d();
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
    sceGuSetMatrix(GU_MODEL, reinterpret_cast<const ScePspFMatrix4*>(model.m));
    bind_texture(texture);
    const Mesh& m = g_meshes[mesh];
    sceGuDrawArray(GU_TRIANGLES, kVertex3D | GU_INDEX_16BIT, m.index_count, m.indices, m.vertices);
    ++g_draw_calls;
    g_triangles += m.index_count / 3;
}

void draw_model(ModelId model, const core::Mat4& transform) {
    const internal::Model* m = internal::model_resource(model);
    if (!m) return;
    enter_3d();
    const auto& h = m->h;
    const core::Mat4 packed_model = transform *
        core::mat4_translate({h.pos_offset[0], h.pos_offset[1], h.pos_offset[2]}) *
        core::mat4_scale({h.pos_scale[0], h.pos_scale[1], h.pos_scale[2]});
    sceGuSetMatrix(GU_MODEL, reinterpret_cast<const ScePspFMatrix4*>(packed_model.m));
    sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_CULL_FACE);
    sceGuShadeModel(GU_SMOOTH);
    constexpr int kPackedVertex = GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT |
                                  GU_INDEX_16BIT | GU_TRANSFORM_3D;
    static_assert(kPackedVertex == 0x111E, "MSH1 GE vertex layout");
    for (uint32_t i = 0; i < h.submesh_count; ++i) {
        const auto& s = m->subsets[i];
        bind_texture(s.texture);
        sceGuTexMapMode(GU_TEXTURE_COORDS, 0, 0);
        sceGuTexScale(h.uv_scale[0], h.uv_scale[1]);
        sceGuTexOffset(h.uv_offset[0], h.uv_offset[1]);
        for (uint32_t done = 0; done < s.index_count;) {
            const uint32_t remaining = s.index_count - done;
            const uint32_t count = remaining < 65535 ? remaining : 65535;
            sceGuDrawArray(GU_TRIANGLES, kPackedVertex, static_cast<int>(count),
                           m->indices + s.index_start + done, m->vertices);
            ++g_draw_calls;
            g_triangles += count / 3;
            done += count;
        }
    }
    sceGuSetMatrix(GU_MODEL, reinterpret_cast<const ScePspFMatrix4*>(transform.m));
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
    sceGuDisable(GU_ALPHA_TEST);
}

void draw_rect(int x, int y, int w, int h, Color color) {
    enter_2d();
    bind_texture(kNoTexture);
    auto* v = static_cast<Vertex*>(sceGuGetMemory(2 * sizeof(Vertex)));
    v[0] = {0, 0, color, static_cast<float>(x), static_cast<float>(y), 0};
    v[1] = {0, 0, color, static_cast<float>(x + w), static_cast<float>(y + h), 0};
    sceGuDrawArray(GU_SPRITES, kVertex2D, 2, nullptr, v);
    ++g_draw_calls;
}

void draw_sprites(TextureId texture, const Sprite* sprites, int count) {
    if (texture >= g_texture_count || count <= 0) return;
    enter_2d();
    bind_texture(texture);
    // В режиме TRANSFORM_2D координаты текстуры — в текселях.
    auto* v = static_cast<Vertex*>(sceGuGetMemory(2 * count * sizeof(Vertex)));
    for (int i = 0; i < count; ++i) {
        const Sprite& s = sprites[i];
        v[2 * i] = {static_cast<float>(s.u), static_cast<float>(s.v), s.color,
                    static_cast<float>(s.x), static_cast<float>(s.y), 0};
        v[2 * i + 1] = {static_cast<float>(s.u + s.uw), static_cast<float>(s.v + s.vh), s.color,
                        static_cast<float>(s.x + s.w), static_cast<float>(s.y + s.h), 0};
    }
    sceGuDrawArray(GU_SPRITES, kVertex2D, 2 * count, nullptr, v);
    ++g_draw_calls;
}

void end_frame() {
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();

    FrameStats& s = internal::mutable_stats();
    s.draw_calls = g_draw_calls;
    s.triangles = g_triangles;
    g_draw_calls = 0;
    g_triangles = 0;
}

}  // namespace render
