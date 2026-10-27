#pragma once
// КОНТРАКТ (T001). Рендер: один интерфейс для PSP (GU) и PC (OpenGL 2.1).
// Реализации: render_psp.cpp, render_pc.cpp; общее — render_common.cpp.
// Менять — только задачей «меняет контракт» (T012 добавил загрузку .msh/.tex;
// ожидаемо: T030 — шрифты UI).
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
    uint16_t max_models = 128;     // моделей из .msh (T012)
    uint16_t max_submeshes = 512;  // подсетов всех моделей вместе
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
// Ресурсы из файлов (T012). Форматы: docs/formats/texture.md (TEX1), docs/formats/mesh.md (MSH1).
//
// Файл проверяется целиком по разделу «Проверки/Валидация загрузчиком» своего формата ДО
// выделения памяти. Ошибка — строка в stderr (имя/путь и причина) и kNoTexture / kInvalidModel;
// арены при этом не тратятся. Данные копируются во внутренние арены: PSP хранит их как в файле
// (packed-вершины, CLUT, swizzle идут в GE напрямую), PC раскрывает в Vertex / RGBA8888.
// Исходный буфер после вызова можно освободить. Вызывать только при загрузке, не в кадре.
//
//     render::texture_load_file("assets/tex_crt.tex");          // сначала текстуры,
//     const render::ModelId crt = render::model_load_file("assets/prop_crt17.msh");  // потом модели
//     ...
//     render::draw_model(crt, core::mat4_translate({1, 0.75f, 0}));

// name — id текстуры: ASCII 1..31 байт, обычно имя файла без расширения. По нему подсеты .msh
// находят свою текстуру. Имя уже занято — kNoTexture.
TextureId texture_load_tex(const char* name, const void* data, size_t size);

// Загруженная текстура по id; kNoTexture — нет такой.
TextureId texture_find(const char* name);

// Число кадров многокадровой TEX1 (экраны мониторов). У текстур из texture_create — 1.
uint16_t texture_frame_count(TextureId texture);

// Кадр для всех следующих отрисовок с этой текстурой. frame ≥ count — последний кадр.
// Без аллокаций, можно вызывать в кадре.
void texture_set_frame(TextureId texture, uint16_t frame);

using ModelId = uint16_t;
constexpr ModelId kInvalidModel = 0xFFFF;

// Модель из .msh. Текстуры подсетов разрешаются через texture_find В МОМЕНТ ЗАГРУЗКИ —
// поэтому текстуры грузят раньше моделей. Неизвестный id — подсет рисуется без текстуры
// (цвета вершин), в stderr предупреждение; модель при этом загружается.
ModelId model_load_msh(const void* data, size_t size);

// Обёртки над platform::file_read: временный буфер выделяется и освобождается внутри вызова.
// Путь — относительно папки приложения. Имя текстуры — basename пути без расширения.
TextureId texture_load_file(const char* path);
ModelId model_load_file(const char* path);

// AABB модели в пространстве объекта (bbox_min/max из заголовка .msh). false — неверный id.
bool model_bounds(ModelId model, core::Vec3* out_min, core::Vec3* out_max);

// ---------------------------------------------------------------------------
// Кадр

void begin_frame(Color clear_color);

// Матрицы камеры для последующих draw_mesh. Проекция — core::mat4_perspective.
// Вызывать каждый кадр после begin_frame(): на PSP матрицы пишутся в список команд кадра.
void set_camera(const core::Mat4& view, const core::Mat4& projection);

// Тест глубины включён, смешивание выключено, отсечение граней пока выключено.
void draw_mesh(MeshId mesh, const core::Mat4& model, TextureId texture = kNoTexture);

// Модель из .msh: по вызову отрисовки на подсет (и на каждые 65535 индексов). Состояние — как
// у draw_mesh; текстуры с прозрачностью (TEX1 flags bit1) рисуются с альфа-тестом (cutout)
// на обеих платформах. Неверный id — ничего не рисуется.
void draw_model(ModelId model, const core::Mat4& transform);

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
