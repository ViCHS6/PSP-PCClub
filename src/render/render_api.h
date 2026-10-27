#pragma once
// КОНТРАКТ (T001). Рендер: один интерфейс для PSP (GU) и PC (OpenGL 2.1).
// Реализации: render_psp.cpp, render_pc.cpp; общее — render_common.cpp.
// Менять — только задачей «меняет контракт» (ожидаемо: T011/T012 — форматы вершин
// и текстур с палитрой, T030 — шрифты UI).
//
// Модель памяти: ресурсы создаются при загрузке, данные КОПИРУЮТСЯ во внутренние арены
// фиксированного размера (Config). Исходные буферы после вызова можно освободить.
// Удаления по одному нет — только всё сразу в shutdown(). В кадре память не выделяется.
//
// Пример кадра:
//
//     render::begin_frame(render::rgba(20, 24, 32));
//     render::set_camera(view, projection);
//     render::draw_mesh(cube, model);
//     render::draw_rect(4, 4, 100, 20, render::rgba(0, 0, 0, 160));   // 2D — поверх
//     render::debug_text(8, 8, "Привет, клуб!", render::kWhite);
//     render::end_frame();
#include <cstddef>
#include <cstdint>

#include "core/math.h"

namespace render {

constexpr int kScreenWidth = 480;
constexpr int kScreenHeight = 272;

// Цвет 0xAABBGGRR: в памяти байты R, G, B, A. Так его читают и GU, и OpenGL.
using Color = uint32_t;

constexpr Color rgba(uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255) {
    return (a << 24) | (b << 16) | (g << 8) | r;
}

constexpr Color kWhite = rgba(255, 255, 255);
constexpr Color kBlack = rgba(0, 0, 0);

// Порядок полей совпадает с форматом вершины GU
// (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF) и GL_T2F_C4UB_V3F.
struct Vertex {
    float u, v;   // 0…1
    Color color;  // умножается на текстуру
    float x, y, z;
};
static_assert(sizeof(Vertex) == 24, "Vertex должен совпадать с форматом GU");

using MeshId = uint16_t;
using TextureId = uint16_t;
constexpr MeshId kInvalidMesh = 0xFFFF;
constexpr TextureId kNoTexture = 0xFFFF;

struct Config {
    size_t mesh_arena_bytes = 512 * 1024;     // вершины и индексы всех мешей
    size_t texture_arena_bytes = 1024 * 1024; // пиксели всех текстур (основная RAM)
    uint16_t max_meshes = 256;
    uint16_t max_textures = 128;
};

// Вызывать после platform::init(). false — фатальная ошибка.
bool init(const Config& config);
void shutdown();

// ---------------------------------------------------------------------------
// Ресурсы

// Индексированные треугольники. kInvalidMesh — кончилась арена или лимит мешей.
MeshId mesh_create(const Vertex* vertices, uint16_t vertex_count,
                   const uint16_t* indices, uint16_t index_count);

enum class TextureFormat : uint8_t {
    kRgba8888,  // 4 байта на пиксель, байты R, G, B, A
};

// width, height — степени двойки от 8 до 512. Фильтрация — ближайший тексель.
// kNoTexture — неверный размер, кончилась арена или лимит текстур.
TextureId texture_create(int width, int height, TextureFormat format, const void* pixels);

// ---------------------------------------------------------------------------
// Кадр

void begin_frame(Color clear_color);

// Матрицы камеры для последующих draw_mesh. Проекция — core::mat4_perspective.
// Вызывать каждый кадр после begin_frame(): на PSP матрицы пишутся в список команд кадра.
void set_camera(const core::Mat4& view, const core::Mat4& projection);

// Тест глубины включён, смешивание выключено, отсечение граней пока выключено.
void draw_mesh(MeshId mesh, const core::Mat4& model, TextureId texture = kNoTexture);

// 2D: пиксели экрана 480×272, (0, 0) — левый верхний угол. Без теста глубины,
// с альфа-смешиванием. Рисуется в порядке вызовов — обычно после всего 3D.
void draw_rect(int x, int y, int w, int h, Color color);

struct Sprite {
    int16_t x, y, w, h;    // прямоугольник на экране
    int16_t u, v, uw, vh;  // прямоугольник в текстуре, в текселях
    Color color;           // умножается на текстуру
};

// Пачка спрайтов из одной текстуры — один вызов отрисовки.
void draw_sprites(TextureId texture, const Sprite* sprites, int count);

// Отправить кадр и дождаться вертикальной синхронизации.
void end_frame();

// ---------------------------------------------------------------------------
// Отладочный текст — ВРЕМЕННЫЙ шрифт Terminus 6×12 (T001), UTF-8 с кириллицей.
// Только для отладки: текст для игрока идёт через шрифт UI (T003/T030).
// Неизвестные символы рисуются как '?'. '\n' — перевод строки.

constexpr int kDebugGlyphWidth = 6;
constexpr int kDebugGlyphHeight = 12;

// Возвращает ширину самой длинной строки в пикселях.
int debug_text(int x, int y, const char* utf8, Color color);

// ---------------------------------------------------------------------------
// Статистика

struct FrameStats {
    uint32_t draw_calls = 0;          // за последний завершённый кадр
    uint32_t triangles = 0;           // 3D-треугольники за последний кадр
    size_t mesh_arena_used = 0;
    size_t mesh_arena_size = 0;
    size_t texture_arena_used = 0;
    size_t texture_arena_size = 0;
    size_t vram_used = 0;             // PSP: кадровые буферы + Z; PC: 0
};

const FrameStats& stats();

}  // namespace render
