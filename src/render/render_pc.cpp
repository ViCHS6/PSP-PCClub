// Рендер PC: OpenGL 2.1 с фиксированным конвейером — по возможностям близко к GU.
#include <SDL.h>
#include <SDL_opengl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "platform/pc/pc_options.h"
#include "render/render_internal.h"
#include "render/texture_format.h"

namespace render {
namespace {

struct Mesh {
    const Vertex* vertices;
    const uint16_t* indices;
    uint16_t index_count;
};

struct Texture {
    GLuint id;
    int width, height;
    internal::TextureInfo info;
    GLuint* frames;
};

enum class Mode { kNone, k3D, k2D };

Config g_config;
internal::Arena g_mesh_arena;
size_t g_texture_bytes = 0;
Mesh* g_meshes = nullptr;
uint16_t g_mesh_count = 0;
Texture* g_textures = nullptr;
uint16_t g_texture_count = 0;

core::Mat4 g_view = core::mat4_identity();
core::Mat4 g_projection = core::mat4_identity();
Mode g_mode = Mode::kNone;
int g_viewport[4] = {0, 0, kScreenWidth, kScreenHeight};
uint32_t g_draw_calls = 0;
uint32_t g_triangles = 0;
int g_frame_index = 0;

void gl_color(Color c) {
    glColor4ub(c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF, (c >> 24) & 0xFF);
}

void bind_texture(TextureId id) {
    glDisable(GL_ALPHA_TEST);
    if (id < g_texture_count) {
        const Texture& t = g_textures[id];
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, t.frames ? t.frames[t.info.frame] : t.id);
        if (t.info.transparent) {
            glEnable(GL_ALPHA_TEST);
            glAlphaFunc(GL_GREATER, 0.0f);
        }
    } else {
        glDisable(GL_TEXTURE_2D);
    }
}

void enter_3d() {
    if (g_mode == Mode::k3D) return;
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(g_projection.m);
    g_mode = Mode::k3D;
}

void enter_2d() {
    if (g_mode == Mode::k2D) return;
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, kScreenWidth, kScreenHeight, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    g_mode = Mode::k2D;
}

bool upload_texture(GLuint* id, int width, int height, const void* pixels) {
    glGenTextures(1, id);
    glBindTexture(GL_TEXTURE_2D, *id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    return *id != 0 && glGetError() == GL_NO_ERROR;
}

void save_screenshot(const char* path) {
    const int w = g_viewport[2], h = g_viewport[3];
    auto* pixels = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(w) * h * 4));
    auto* flipped = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(w) * h * 4));
    if (pixels && flipped) {
        glReadPixels(g_viewport[0], g_viewport[1], w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        for (int y = 0; y < h; ++y) {
            std::memcpy(flipped + static_cast<size_t>(y) * w * 4,
                        pixels + static_cast<size_t>(h - 1 - y) * w * 4, static_cast<size_t>(w) * 4);
        }
        SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(flipped, w, h, 32, w * 4, SDL_PIXELFORMAT_ABGR8888);
        if (s && SDL_SaveBMP(s, path) == 0) {
            std::printf("снимок экрана: %s\n", path);
        } else {
            std::fprintf(stderr, "не удалось сохранить снимок: %s\n", SDL_GetError());
        }
        if (s) SDL_FreeSurface(s);
    }
    std::free(pixels);
    std::free(flipped);
}

}  // namespace

namespace internal {

TextureInfo* texture_info(TextureId id) {
    return id < g_texture_count ? &g_textures[id].info : nullptr;
}

void resource_writeback(const void*, size_t) {}

size_t texture_file_limit() {
    if (!g_textures || g_texture_bytes > g_config.texture_arena_bytes) return 0;
    const uint64_t remaining = g_config.texture_arena_bytes - g_texture_bytes;
    if (remaining > uint64_t(SIZE_MAX) - 1056) return SIZE_MAX;
    return static_cast<size_t>(remaining + 1056);
}

}  // namespace internal

bool init(const Config& config) {
    g_config = config;
    g_mesh_arena.base = static_cast<uint8_t*>(std::malloc(config.mesh_arena_bytes));
    g_mesh_arena.size = config.mesh_arena_bytes;
    g_meshes = static_cast<Mesh*>(std::calloc(config.max_meshes, sizeof(Mesh)));
    g_textures = static_cast<Texture*>(std::calloc(config.max_textures, sizeof(Texture)));
    if (!g_mesh_arena.base || !g_meshes || !g_textures) return false;
    if (!internal::model_resources_init(config, &g_mesh_arena, false)) return false;

    glDisable(GL_CULL_FACE);
    glDepthFunc(GL_LEQUAL);
    glShadeModel(GL_SMOOTH);
    glEnableClientState(GL_VERTEX_ARRAY);

    FrameStats& s = internal::mutable_stats();
    s.mesh_arena_size = config.mesh_arena_bytes;
    s.texture_arena_size = config.texture_arena_bytes;
    return internal::debug_font_init();
}

void shutdown() {
    internal::model_resources_shutdown();
    for (uint16_t i = 0; i < g_texture_count; ++i) {
        Texture& t = g_textures[i];
        if (t.frames) {
            glDeleteTextures(t.info.frame_count, t.frames);
            std::free(t.frames);
        } else {
            glDeleteTextures(1, &t.id);
        }
    }
    std::free(g_textures);
    std::free(g_meshes);
    std::free(g_mesh_arena.base);
    g_textures = nullptr;
    g_meshes = nullptr;
    g_mesh_arena = {};
    g_mesh_count = g_texture_count = 0;
    g_texture_bytes = 0;
    g_frame_index = 0;
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
    g_meshes[g_mesh_count] = {v, i, index_count};
    internal::mutable_stats().mesh_arena_used = g_mesh_arena.used;
    return g_mesh_count++;
}

TextureId texture_create(int width, int height, TextureFormat format, const void* pixels) {
    auto pow2 = [](int n) { return n >= 8 && n <= 512 && (n & (n - 1)) == 0; };
    if (!g_textures || !pixels || format != TextureFormat::kRgba8888 || !pow2(width) || !pow2(height)) return kNoTexture;
    if (g_texture_count >= g_config.max_textures) return kNoTexture;
    // Арена на PC не нужна, но лимит считаем так же, как на PSP, — чтобы бюджет ловился и здесь.
    const size_t bytes = static_cast<size_t>(width) * height * 4;
    if (uint64_t(g_texture_bytes) + bytes > g_config.texture_arena_bytes) return kNoTexture;

    Texture& t = g_textures[g_texture_count];
    if (!upload_texture(&t.id, width, height, pixels)) {
        glDeleteTextures(1, &t.id);
        return kNoTexture;
    }
    t.width = width;
    t.height = height;
    t.info = {};
    t.frames = nullptr;
    g_texture_bytes += bytes;
    internal::mutable_stats().texture_arena_used = g_texture_bytes;
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
    const auto& h = view.h;
    const uint64_t frame_bytes = uint64_t(h.width) * h.height * 4;
    // Арену считаем как на PSP — по размеру TEX1 (CLUT + кадры), а не по раскрытому RGBA,
    // который живёт в памяти драйвера GL: иначе PC отказывает там, где PSP укладывается.
    const uint64_t bytes = size;
    if (!g_textures || g_texture_count >= g_config.max_textures ||
        bytes > g_config.texture_arena_bytes || g_texture_bytes > g_config.texture_arena_bytes - bytes)
        return fail("texture limit/arena exhausted");
    auto* rgba_pixels = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(frame_bytes)));
    auto* frames = static_cast<GLuint*>(std::calloc(h.frame_count, sizeof(GLuint)));
    if (!rgba_pixels || !frames) {
        std::free(rgba_pixels);
        std::free(frames);
        return fail("temporary allocation failed");
    }
    bool ok = true;
    for (uint32_t f = 0; f < h.frame_count; ++f) {
        format::decode_texture_frame(view, static_cast<uint16_t>(f), rgba_pixels, static_cast<size_t>(frame_bytes));
        if (!upload_texture(frames + f, h.width, h.height, rgba_pixels)) { ok = false; break; }
    }
    std::free(rgba_pixels);
    if (!ok) {
        glDeleteTextures(h.frame_count, frames);
        std::free(frames);
        return fail("OpenGL upload failed");
    }
    Texture& t = g_textures[g_texture_count];
    t = {};
    t.width = h.width;
    t.height = h.height;
    t.frames = frames;
    std::strcpy(t.info.name, name);
    t.info.frame_count = h.frame_count;
    t.info.transparent = (h.flags & 2) != 0;
    g_texture_bytes += static_cast<size_t>(bytes);
    internal::mutable_stats().texture_arena_used = g_texture_bytes;
    return g_texture_count++;
}

void begin_frame(Color clear_color) {
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(SDL_GL_GetCurrentWindow(), &w, &h);
    // Вписываем 480×272 в окно с сохранением пропорций.
    const float scale = (static_cast<float>(w) / kScreenWidth < static_cast<float>(h) / kScreenHeight)
                            ? static_cast<float>(w) / kScreenWidth
                            : static_cast<float>(h) / kScreenHeight;
    g_viewport[2] = static_cast<int>(kScreenWidth * scale);
    g_viewport[3] = static_cast<int>(kScreenHeight * scale);
    g_viewport[0] = (w - g_viewport[2]) / 2;
    g_viewport[1] = (h - g_viewport[3]) / 2;

    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, w, h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    glViewport(g_viewport[0], g_viewport[1], g_viewport[2], g_viewport[3]);
    glScissor(g_viewport[0], g_viewport[1], g_viewport[2], g_viewport[3]);
    glEnable(GL_SCISSOR_TEST);
    glClearColor((clear_color & 0xFF) / 255.0f, ((clear_color >> 8) & 0xFF) / 255.0f,
                 ((clear_color >> 16) & 0xFF) / 255.0f, 1.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    g_mode = Mode::kNone;
}

void set_camera(const core::Mat4& view, const core::Mat4& projection) {
    g_view = view;
    g_projection = projection;
    if (g_mode == Mode::k3D) {
        glMatrixMode(GL_PROJECTION);
        glLoadMatrixf(g_projection.m);
    }
}

void draw_mesh(MeshId mesh, const core::Mat4& model, TextureId texture) {
    if (mesh >= g_mesh_count) return;
    enter_3d();
    const core::Mat4 model_view = g_view * model;
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(model_view.m);
    bind_texture(texture);
    const Mesh& m = g_meshes[mesh];
    glInterleavedArrays(GL_T2F_C4UB_V3F, 0, m.vertices);
    glDrawElements(GL_TRIANGLES, m.index_count, GL_UNSIGNED_SHORT, m.indices);
    ++g_draw_calls;
    g_triangles += m.index_count / 3;
}

void draw_model(ModelId model, const core::Mat4& transform) {
    const internal::Model* m = internal::model_resource(model);
    if (!m) return;
    enter_3d();
    const core::Mat4 model_view = g_view * transform;
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(model_view.m);
    glInterleavedArrays(GL_T2F_C4UB_V3F, 0, m->vertices);
    for (uint32_t i = 0; i < m->h.submesh_count; ++i) {
        const auto& s = m->subsets[i];
        bind_texture(s.texture);
        for (uint32_t done = 0; done < s.index_count;) {
            const uint32_t remaining = s.index_count - done;
            const uint32_t count = remaining < 65535 ? remaining : 65535;
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(count), GL_UNSIGNED_SHORT,
                           m->indices + s.index_start + done);
            ++g_draw_calls;
            g_triangles += count / 3;
            done += count;
        }
    }
    glDisable(GL_ALPHA_TEST);
}

void draw_rect(int x, int y, int w, int h, Color color) {
    enter_2d();
    bind_texture(kNoTexture);
    gl_color(color);
    glBegin(GL_QUADS);
    glVertex2i(x, y);
    glVertex2i(x + w, y);
    glVertex2i(x + w, y + h);
    glVertex2i(x, y + h);
    glEnd();
    ++g_draw_calls;
}

void draw_sprites(TextureId texture, const Sprite* sprites, int count) {
    if (texture >= g_texture_count || count <= 0) return;
    enter_2d();
    bind_texture(texture);
    const float iw = 1.0f / g_textures[texture].width;
    const float ih = 1.0f / g_textures[texture].height;
    glBegin(GL_QUADS);
    for (int i = 0; i < count; ++i) {
        const Sprite& s = sprites[i];
        const float u0 = s.u * iw, v0 = s.v * ih, u1 = (s.u + s.uw) * iw, v1 = (s.v + s.vh) * ih;
        gl_color(s.color);
        glTexCoord2f(u0, v0);
        glVertex2i(s.x, s.y);
        glTexCoord2f(u1, v0);
        glVertex2i(s.x + s.w, s.y);
        glTexCoord2f(u1, v1);
        glVertex2i(s.x + s.w, s.y + s.h);
        glTexCoord2f(u0, v1);
        glVertex2i(s.x, s.y + s.h);
    }
    glEnd();
    ++g_draw_calls;
}

void end_frame() {
    const platform::PcOptions& opt = platform::pc_options();
    ++g_frame_index;
    if (opt.screenshot_path && g_frame_index == opt.max_frames) save_screenshot(opt.screenshot_path);
    SDL_GL_SwapWindow(SDL_GL_GetCurrentWindow());

    FrameStats& s = internal::mutable_stats();
    s.draw_calls = g_draw_calls;
    s.triangles = g_triangles;
    g_draw_calls = 0;
    g_triangles = 0;
}

}  // namespace render
