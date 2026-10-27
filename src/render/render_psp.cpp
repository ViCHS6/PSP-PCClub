// Рендер PSP: GU напрямую, без GUM. Кадровые буферы 16 бит (5650) + Z 16 бит в VRAM.
#include <malloc.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspkernel.h>

#include <cstring>

#include "render/render_internal.h"

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
    const void* pixels;
    int width, height;
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
TextureId g_bound_texture = kNoTexture;
uint32_t g_draw_calls = 0;
uint32_t g_triangles = 0;

void* arena_block(size_t bytes) { return memalign(64, bytes); }

void bind_texture(TextureId id) {
    if (id >= g_texture_count) {
        sceGuDisable(GU_TEXTURE_2D);
        g_bound_texture = kNoTexture;
        return;
    }
    sceGuEnable(GU_TEXTURE_2D);
    if (g_bound_texture == id) return;
    const Texture& t = g_textures[id];
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, t.width, t.height, t.width, t.pixels);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexWrap(GU_REPEAT, GU_REPEAT);
    sceGuTexFlush();
    g_bound_texture = id;
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

bool init(const Config& config) {
    g_config = config;
    g_mesh_arena.base = static_cast<uint8_t*>(arena_block(config.mesh_arena_bytes));
    g_mesh_arena.size = config.mesh_arena_bytes;
    g_texture_arena.base = static_cast<uint8_t*>(arena_block(config.texture_arena_bytes));
    g_texture_arena.size = config.texture_arena_bytes;
    g_meshes = static_cast<Mesh*>(calloc(config.max_meshes, sizeof(Mesh)));
    g_textures = static_cast<Texture*>(calloc(config.max_textures, sizeof(Texture)));
    if (!g_mesh_arena.base || !g_texture_arena.base || !g_meshes || !g_textures) return false;

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
    auto* v = static_cast<Vertex*>(g_mesh_arena.alloc(sizeof(Vertex) * vertex_count, 16));
    auto* i = static_cast<uint16_t*>(g_mesh_arena.alloc(sizeof(uint16_t) * index_count, 16));
    if (!v || !i) return kInvalidMesh;
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
    if (format != TextureFormat::kRgba8888 || !pow2(width) || !pow2(height)) return kNoTexture;
    if (g_texture_count >= g_config.max_textures) return kNoTexture;
    const size_t bytes = static_cast<size_t>(width) * height * 4;
    void* dst = g_texture_arena.alloc(bytes, 16);
    if (!dst) return kNoTexture;
    std::memcpy(dst, pixels, bytes);
    sceKernelDcacheWritebackRange(dst, bytes);
    g_textures[g_texture_count] = {dst, width, height};
    internal::mutable_stats().texture_arena_used = g_texture_arena.used;
    return g_texture_count++;
}

void begin_frame(Color clear_color) {
    sceGuStart(GU_DIRECT, g_display_list);
    sceGuClearColor(clear_color);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    g_mode = Mode::kNone;
    g_bound_texture = kNoTexture;
}

void set_camera(const core::Mat4& view, const core::Mat4& projection) {
    sceGuSetMatrix(GU_PROJECTION, reinterpret_cast<const ScePspFMatrix4*>(projection.m));
    sceGuSetMatrix(GU_VIEW, reinterpret_cast<const ScePspFMatrix4*>(view.m));
}

void draw_mesh(MeshId mesh, const core::Mat4& model, TextureId texture) {
    if (mesh >= g_mesh_count) return;
    enter_3d();
    sceGuSetMatrix(GU_MODEL, reinterpret_cast<const ScePspFMatrix4*>(model.m));
    bind_texture(texture);
    const Mesh& m = g_meshes[mesh];
    sceGuDrawArray(GU_TRIANGLES, kVertex3D | GU_INDEX_16BIT, m.index_count, m.indices, m.vertices);
    ++g_draw_calls;
    g_triangles += m.index_count / 3;
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
