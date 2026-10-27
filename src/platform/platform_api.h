#pragma once
// КОНТРАКТ (T001). Платформенный слой: один интерфейс для PSP и PC.
// Реализации: src/platform/psp/, src/platform/pc/. Менять — только задачей «меняет контракт».
//
// Все функции вызываются из главного потока, если не сказано иное.
// Функции не выделяют память после init(), кроме оговорённых.
//
// Пример главного цикла:
//
//     platform::init({argc, argv});
//     while (platform::poll()) {
//         const platform::InputState& in = platform::input();
//         if (in.pressed & platform::kButtonCross) { ... }
//     }
//     platform::shutdown();
#include <cstddef>
#include <cstdint>

namespace platform {

// ---------------------------------------------------------------------------
// Жизненный цикл

struct InitParams {
    int argc = 0;
    char** argv = nullptr;
};

// PSP: колбэк выхода по HOME, настройка контроллера, папка приложения из argv[0].
// PC: окно SDL2 480×272 ×2 с контекстом OpenGL 2.1 (его использует render), разбор
//     аргументов --frames N и --screenshot путь.bmp (см. pc/pc_options.h).
// false — фатальная ошибка, игру нужно завершить.
bool init(const InitParams& params);

// PSP: завершает программу через sceKernelExitGame() и не возвращается.
void shutdown();

// Раз в кадр, до чтения input(): системные события, опрос ввода, учёт пика памяти.
// false — пора выходить (HOME → «Выход», закрыто окно, исчерпан --frames).
bool poll();

// Попросить выход: следующий poll() вернёт false.
void request_exit();

// "PSP" или "PC" — для отладочного оверлея.
const char* name();

// ---------------------------------------------------------------------------
// Ввод

enum Button : uint32_t {
    kButtonUp = 1u << 0,
    kButtonDown = 1u << 1,
    kButtonLeft = 1u << 2,
    kButtonRight = 1u << 3,
    kButtonCross = 1u << 4,
    kButtonCircle = 1u << 5,
    kButtonSquare = 1u << 6,
    kButtonTriangle = 1u << 7,
    kButtonL = 1u << 8,
    kButtonR = 1u << 9,
    kButtonStart = 1u << 10,
    kButtonSelect = 1u << 11,
};

// PC-раскладка: стик — WASD, крестовина — стрелки, × — K, ○ — L, □ — J, △ — I,
// L — Q, R — E, START — Enter, SELECT — Backspace. Геймпад SDL — по положению кнопок.
struct InputState {
    uint32_t held = 0;      // зажатые сейчас (маска Button)
    uint32_t pressed = 0;   // нажатые в этом кадре
    uint32_t released = 0;  // отпущенные в этом кадре
    float stick_x = 0.0f;   // −1 (влево) … +1 (вправо), мёртвая зона уже вычтена
    float stick_y = 0.0f;   // −1 (вниз/на себя) … +1 (вверх/от себя)
};

// Состояние на момент последнего poll().
const InputState& input();

// ---------------------------------------------------------------------------
// Время

// Монотонное время в микросекундах от произвольной точки отсчёта.
uint64_t time_us();

// ---------------------------------------------------------------------------
// Файлы
//
// Пути — относительно папки приложения (где лежит EBOOT.PBP или исполняемый файл),
// разделитель '/', без ведущего '/'. Префиксы устройств (ms0:, ef0:) запрещены.

bool file_size(const char* path, size_t* out_size);

// Читает файл целиком в dst. false — нет файла, ошибка чтения или файл больше capacity.
bool file_read(const char* path, void* dst, size_t capacity, size_t* out_read);

// ---------------------------------------------------------------------------
// Память

struct MemoryStats {
    size_t heap_used = 0;      // занято в куче malloc
    size_t heap_peak = 0;      // максимум heap_used, замеряется в каждом poll()
    size_t heap_limit = 0;     // PSP: жёсткий размер кучи (бюджет); PC: 0 — без лимита
    size_t system_free = 0;    // PSP: свободно в пользовательском разделе ядра; PC: 0
};

MemoryStats memory_stats();

// ---------------------------------------------------------------------------
// Звук — ЗАГЛУШКА до T061: audio_start() возвращает false.

// Заполнить frames стереокадров int16 (L, R, L, R, …), 44 100 Гц.
// Вызывается из АУДИОПОТОКА: без аллокаций, без блокировок главного потока.
using AudioCallback = void (*)(int16_t* dst, int frames, void* user);

bool audio_start(AudioCallback callback, void* user);
void audio_stop();

}  // namespace platform
