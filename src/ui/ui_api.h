#pragma once
// КОНТРАКТ (T030). ОС администратора «Окна 2000 Клубная» (GDD §9.2): рабочий стол, окна
// программ, фокус крестовиной, курсор на стике, виджеты, текст bitmap-шрифтами T003.
// Реализация — src/ui/ (T030). Пояснения, раскладка экрана и правила для программ —
// docs/formats/ui.md. Менять — только задачей «меняет контракт».
//
// КТО ВЫЗЫВАЕТ
//   - game/ (главный цикл): init/shutdown при загрузке, register_app для всех программ,
//     set_world, set_active (камера въехала в монитор / выехала), update и draw каждый кадр;
//   - программы ОС (src/ui/apps/*, T031/T032): только из своих колбэков AppDesc — виджеты,
//     рисование, str(), чтение ctx.world и команды core::cmd_*;
//   - мини-игры (T050) — текстом и примитивами экранного уровня (draw_text, text_width).
//   Всё — из главного потока.
//
// ПАМЯТЬ
//   UI не выделяет память ни при загрузке, ни в кадре: все пулы — статические массивы
//   с лимитами ниже. Шрифты грузятся в арену текстур render (init). Состояние программы
//   (выделение в списке, вкладка, черновик заказа) хранит САМА программа — обычно static-
//   структура, указатель на которую передаётся в AppDesc::user. UI хранит только окна,
//   фокус и списки фокусируемых областей.
//
// МОДЕЛЬ — немедленный режим (immediate mode)
//   Программа не создаёт объекты виджетов. Каждый кадр UI вызывает AppDesc::on_frame для
//   каждого открытого окна (снизу вверх по стопке), и программа в нём заново «объявляет»
//   виджеты: button() одновременно рисует кнопку и возвращает true, если её нажали.
//   Ввод получает только активное (верхнее) окно; остальные лишь рисуются.
//
// ПРИМЕР (программа)
//
//     struct ShopState { ui::ListState list; int qty = 1; };
//     static ShopState g_shop;
//     static const char* g_buy;  // строки — один раз при открытии/регистрации
//
//     static const char* item_text(int i, char* buf, int cap, void* user) {
//         const core::GameData& d = *static_cast<ui::Context*>(user)->data;
//         return core::data_string(d, d.hardware[i].name);   // или snprintf в buf
//     }
//     static void shop_open(ui::Context&, void*) { g_buy = ui::str("ui.shop.buy"); }
//     static void shop_frame(ui::Context& ctx, void* user) {
//         ShopState& s = *static_cast<ShopState*>(user);
//         ui::list(ctx, 1, {4, 4, 200, 140}, s.list, ctx.data->hardware_count, item_text, &ctx);
//         if (ui::button(ctx, 2, {210, 4, 80, 16}, g_buy) && s.list.selected >= 0)
//             core::cmd_order(*ctx.world, core::OrderKind::kHardware, s.list.selected, 1);
//     }
//
//     // game/: при загрузке
//     ui::AppDesc shop{};
//     shop.id = "shop"; shop.title_key = "ui.shop.title";
//     shop.width = 300; shop.height = 160;
//     shop.on_open = shop_open; shop.on_frame = shop_frame; shop.user = &g_shop;
//     ui::register_app(shop);
//
// ПРИМЕР (главный цикл)
//
//     ui::init({&game_data, click_sound});
//     ui::set_world(&world);
//     while (platform::poll()) {
//         ui::update(dt, platform::input());     // курсор, L/R, хит-тест — без рисования
//         render::begin_frame(...);
//         ... 3D ...
//         ui::draw();                             // рабочий стол, окна (on_frame), курсор
//         render::end_frame();
//     }
#include <cstdint>

#include "core/core_api.h"
#include "core/data_api.h"
#include "platform/audio_api.h"
#include "platform/platform_api.h"
#include "render/render_api.h"

namespace ui {

// ---------------------------------------------------------------------------
// Лимиты и раскладка экрана (480×272, пиксели, (0,0) — левый верхний угол)

constexpr int kMaxApps = 16;           // зарегистрированных программ = окон (одно окно на программу)
constexpr int kMaxFocusables = 64;     // фокусируемых виджетов в одном окне за кадр; лишние — без фокуса
constexpr int kMaxColumns = 8;         // столбцов в таблице

constexpr int kTaskbarHeight = 18;     // панель задач внизу экрана
constexpr int kDesktopHeight = render::kScreenHeight - kTaskbarHeight;  // 254: окна живут здесь
constexpr int kTitleHeight = 14;       // заголовок окна
constexpr int kBorder = 2;             // рамка окна с каждой стороны
// Наибольшая клиентская область (окно с kAppMaximized): 476 × 236.
constexpr int kMaxClientWidth = render::kScreenWidth - 2 * kBorder;
constexpr int kMaxClientHeight = kDesktopHeight - kTitleHeight - 2 * kBorder;

constexpr int kRowHeight = 14;         // строка списка и таблицы (font_ui12 + 2)
constexpr int kButtonHeight = 16;      // рекомендуемая высота кнопки и choice

struct Rect {
    int16_t x = 0, y = 0, w = 0, h = 0;
};

// ---------------------------------------------------------------------------
// Цвета темы. Программы рисуют своё (графики, схема зала) этими же цветами.
// База — «Windows 2000» в бежевом пластике из палитры ART.md §2; акценты — экранные.

using render::Color;
constexpr Color kColDesktop = render::rgba(0x3A, 0x6E, 0x8A);     // обои
constexpr Color kColFace = render::rgba(0xD8, 0xD0, 0xB8);        // окна, кнопки, панель задач
constexpr Color kColLight = render::rgba(0xF4, 0xF0, 0xE4);       // светлая фаска
constexpr Color kColShadow = render::rgba(0x8F, 0x86, 0x70);      // тёмная фаска
constexpr Color kColDark = render::rgba(0x40, 0x3A, 0x30);        // внешняя тень, рамки
constexpr Color kColField = render::rgba(0xFC, 0xFA, 0xF2);       // фон списков, таблиц, полей
constexpr Color kColText = render::rgba(0x10, 0x10, 0x10);
constexpr Color kColTextDim = render::rgba(0x80, 0x78, 0x68);     // неактивное, подписи
constexpr Color kColTextInv = render::rgba(0xFF, 0xFF, 0xFF);     // текст на акценте
constexpr Color kColAccent = render::rgba(0x20, 0x50, 0xC0);      // заголовок активного окна, выделение, фокус
constexpr Color kColAccentDim = render::rgba(0x80, 0x88, 0x98);   // заголовок неактивного окна, выделение без фокуса
constexpr Color kColGood = render::rgba(0x30, 0xC0, 0x50);
constexpr Color kColBad = render::rgba(0xE0, 0x40, 0x30);
constexpr Color kColWarn = render::rgba(0xF0, 0xC0, 0x20);

// ---------------------------------------------------------------------------
// Жизненный цикл

struct InitParams {
    // Строки игрока (str()). nullptr — допустимо (тесты, раннеры): str() вернёт сам ключ.
    // Должен жить до shutdown().
    const core::GameData* data = nullptr;
    // Звук щелчка при нажатии кнопок/пунктов. kNoSound — без звука.
    audio::SoundId click_sound = audio::kNoSound;
};

// Вызывать после render::init(), при загрузке. Грузит шрифты (assets/font_*.fnt + .tex, см.
// docs/formats/ui.md). false — шрифты не загрузились (причина — в stderr); UI неработоспособен.
bool init(const InitParams& params);

// Закрывает окна (on_close), забывает программы. Текстуры шрифтов освобождает render::shutdown().
void shutdown();

// Мир, который видят программы через Context::world. Можно менять в любой момент вне draw()
// (загрузка сохранения); nullptr — программы должны это переносить (демо, раннеры).
void set_world(core::World* world);

// Включить/выключить ОС (камера въехала в монитор / выехала). Выключенная ОС не читает ввод
// и ничего не рисует; окна и их состояние сохраняются. ○ при активном рабочем столе выключает
// ОС сама — игра узнаёт это по active() == false.
void set_active(bool active);
bool active();

// Раз в кадр после platform::poll(), ДО render::begin_frame(). Курсор, переключение окон
// (L/R), хит-тест и клик по рамкам. Не рисует, программы не вызывает. При !active() — ничего.
void update(float dt, const platform::InputState& input);

// Раз в кадр между render::begin_frame() и end_frame(), после 3D. Рисует рабочий стол, окна
// снизу вверх (вызывая их on_frame) и курсор. Здесь же применяются нажатия кнопок, навигация
// крестовиной и ○. При !active() — ничего.
void draw();

// ---------------------------------------------------------------------------
// Строки игрока (AGENTS.md: только ключи data/strings_ru.json)

// UTF-8 по ключу ("ui.cashier.title"). Указатель живёт, пока жив буфер data.pak.
// Нет ключа или нет данных — возвращается сам ключ (видно на экране, в stderr — предупреждение).
// Поиск двоичный, но не бесплатный: вызывать при регистрации/открытии и хранить указатель.
const char* str(const char* key);

// ---------------------------------------------------------------------------
// Программы и окна

using AppId = uint8_t;
constexpr AppId kNoApp = 0xFF;

struct Context;

enum AppFlags : uint8_t {
    kAppOnDesktop = 1u << 0,   // значок на рабочем столе (открыть — ×)
    kAppMaximized = 1u << 1,   // окно на весь рабочий стол, width/height игнорируются
    kAppNoClose = 1u << 2,     // ○ и кнопка закрытия не закрывают (Касса в быстром сценарии)
};

struct AppDesc {
    const char* id = nullptr;         // ASCII, уникальный: "cashier", "shop". Строка должна жить вечно
    const char* title_key = nullptr;  // ключ заголовка в strings_ru.json: "ui.cashier.title"
    int16_t width = 240;              // размер КЛИЕНТСКОЙ области, обрезается до kMaxClient*
    int16_t height = 140;
    uint8_t flags = kAppOnDesktop;
    // Значок на столе: 2 буквы/символа UTF-8 на цветной плашке, пока нет текстур ui_icon_* (ART.md §7).
    const char* icon_text = nullptr;  // ключ strings_ru.json, напр. "ui.cashier.icon" → "Кс"
    Color icon_color = kColAccent;

    // Колбэки — из ui::draw(). user — AppDesc::user как есть.
    void (*on_open)(Context& ctx, void* user) = nullptr;   // окно открылось (сброс состояния, кэш строк)
    void (*on_frame)(Context& ctx, void* user) = nullptr;  // ОБЯЗАТЕЛЕН: каждый кадр, пока окно открыто
    void (*on_close)(void* user) = nullptr;                // окно закрылось (○, кнопка, close_app, shutdown)
    void* user = nullptr;                                  // состояние программы; владеет программа
};

// При загрузке. Копирует AppDesc. kNoApp — кончились слоты, нет on_frame или id занят.
AppId register_app(const AppDesc& desc);
AppId find_app(const char* id);

// Открыть окно программы (или поднять уже открытое) и сделать его активным. Новое окно —
// по центру рабочего стола со сдвигом «лесенкой». Можно вызывать в любой момент, в т.ч. из
// on_frame другой программы (окно появится со следующего кадра). false — неверный id.
bool open_app(AppId app);
// Закрыть окно (on_close вызывается сразу). Для kAppNoClose тоже работает: это решение игры.
void close_app(AppId app);
bool is_open(AppId app);
// Активное окно; kNoApp — активен рабочий стол.
AppId active_app();

// ---------------------------------------------------------------------------
// Контекст кадра окна

// Передаётся в on_frame. Публичные поля — только для чтения; detail_ — внутреннее.
struct Context {
    AppId app = kNoApp;
    Rect client;                 // клиентская область на экране. Координаты виджетов — ОТНОСИТЕЛЬНО неё
    bool is_active = false;      // окно активно и получает ввод
    float dt = 0.0f;             // секунды кадра
    core::World* world = nullptr;               // из set_world(); может быть nullptr
    const core::GameData* data = nullptr;       // из InitParams; может быть nullptr
    void* detail_ = nullptr;
};

// Кнопки, нажатые в этом кадре и ещё никем не взятые (для неактивного окна — 0).
// L/R, START, SELECT в этой маске не бывает: L/R переключают окна, START/SELECT — у игры.
uint32_t pressed(const Context& ctx);

// Забрать кнопки себе: возвращает маску тех из buttons, что были нажаты и ещё не взяты, и
// снимает их. Так программа перехватывает ○ («назад» внутри программы вместо закрытия окна),
// крестовину (←/→ тариф в Кассе) или □/△. Взятая крестовина не двигает фокус, взятый ○ не
// закрывает окно. Виджеты (list, choice) берут свои кнопки так же — кто раньше, тот и взял.
uint32_t consume(Context& ctx, uint32_t buttons);

// ---------------------------------------------------------------------------
// Фокус
//
// Фокусируемые виджеты (button, choice, list, table, hotspot) регистрируют свой прямоугольник
// в кадре. Крестовина переводит фокус к ближайшему виджету в её направлении (по прямоугольникам
// этого кадра, новый фокус — со следующего). Курсор при этом прыгает на виджет. Курсор, сдвинутый
// стиком, фокусирует виджет под собой. × нажимает виджет под курсором (обычно — фокусный).
// Фокус без выбора (окно только открылось или виджет исчез) — первый объявленный виджет.

// Id виджета задаёт программа: уникален в пределах окна, 1…0xFFEF (0 и ≥ 0xFFF0 — служебные).
// Id должен быть стабилен между кадрами — по нему UI помнит фокус.
using WidgetId = uint16_t;

// Поставить фокус (и курсор) на виджет со следующего кадра. Напр., Касса — на кнопку «Готово».
void set_focus(Context& ctx, WidgetId id);
bool is_focused(const Context& ctx, WidgetId id);  // окно активно и фокус на id

// ---------------------------------------------------------------------------
// Текст
//
// Шрифты T003 (docs/formats/font.md). UTF-8; неизвестный символ — '?', '\n' — новая строка.
// Цифры во всех шрифтах табличные: суммы денег выравниваются стабильно.

enum class Font : uint8_t {
    kUi12,      // основной: 12 пикс., пропорциональный
    kMono12,    // моноширинный 6×12: таблицы-«терминалы», логи
    kUi14,      // заголовки разделов
    kUi14Bold,  // крупные цифры, акценты
    kCount,
};

enum class Align : uint8_t { kLeft, kCenter, kRight };

int line_height(Font font);
// Ширина для раскладки: сумма advance (font.md «Ширина строки»), у многострочного — максимум.
int text_width(const char* utf8, Font font = Font::kUi12);

// Экранные координаты, без окна — для мини-игр (T050) и оверлеев. y — верх строки.
// max_width > 0 — строка обрезается до этой ширины с «…» на конце.
// Возвращает ширину нарисованного (как text_width для того, что влезло).
int draw_text(int x, int y, const char* utf8, Color color, Font font = Font::kUi12, int max_width = 0);

// В клиентских координатах окна: текст в rect с выравниванием по горизонтали, по вертикали —
// по центру rect; не шире rect.w (обрезка с «…»).
void label(Context& ctx, Rect r, const char* utf8, Color color = kColText,
           Font font = Font::kUi12, Align align = Align::kLeft);

// "1 500 ₽", "-200 ₽" (разделитель тысяч — пробел). Возвращает длину без NUL; обрезает по cap.
int format_money(int32_t rubles, char* buf, int cap);

// ---------------------------------------------------------------------------
// Примитивы (клиентские координаты окна) — для своих виджетов: схема зала, графики

void fill(Context& ctx, Rect r, Color color);
void outline(Context& ctx, Rect r, Color color);      // рамка 1 пиксель
void bevel(Context& ctx, Rect r, bool sunken);        // объёмная фаска 2 пикселя без заливки
// Подпись над группой элементов, рамка с надписью в верхней кромке (как в Win2000).
void group(Context& ctx, Rect r, const char* caption);

// ---------------------------------------------------------------------------
// Виджеты (клиентские координаты окна)

enum WidgetFlags : uint8_t {
    kWidgetDisabled = 1u << 0,   // рисуется тускло, не фокусируется, не нажимается
};

// true — кнопку нажали в этом кадре (× при курсоре на ней). Высота — kButtonHeight.
bool button(Context& ctx, WidgetId id, Rect r, const char* label, uint8_t flags = 0);

// Невидимая фокусируемая область: свой виджет программы (место на схеме зала, ячейка календаря).
// Рисует программа сама, подсветку — по is_focused(). true — нажали.
bool hotspot(Context& ctx, WidgetId id, Rect r, uint8_t flags = 0);

// Текст пункта index. Вернуть можно постоянную строку (str(), data_string) или buf, заполненный
// snprintf (cap ≥ 64). Вызывается только для видимых пунктов, в том же кадре.
using ItemTextFn = const char* (*)(int index, char* buf, int cap, void* user);

// «← Значение →». ←/→ при фокусе (или × по левой/правой половине) меняет *index в [0, count),
// без зацикливания. true — значение изменилось в этом кадре.
bool choice(Context& ctx, WidgetId id, Rect r, int* index, int count, ItemTextFn text, void* user,
            uint8_t flags = 0);

// Полоска 0…1 (значение обрезается) с необязательной подписью по центру (nullptr — без).
void progress(Context& ctx, Rect r, float value, Color fill = kColAccent, const char* caption = nullptr);

// Выделение и прокрутка списка/таблицы. Хранит программа (в своём состоянии), UI меняет.
struct ListState {
    int16_t selected = -1;   // -1 — ничего; при count > 0 и фокусе становится 0
    int16_t scroll = 0;      // первый видимый пункт; UI держит выделение видимым
};

enum class ListEvent : uint8_t {
    kNone,
    kChanged,    // выделение сменилось (крестовина или курсор)
    kActivated,  // × по выделенному пункту
};

// Список строк kRowHeight. ↑/↓ при фокусе — выделение (на краю фокус уходит соседу), курсор
// над строкой выделяет её. Полоса прокрутки — справа, если пункты не помещаются.
// state.selected обрезается при уменьшении count.
ListEvent list(Context& ctx, WidgetId id, Rect r, ListState& state, int count, ItemTextFn text,
               void* user);

struct Column {
    const char* header = nullptr;    // заголовок (str()); nullptr — пустой
    int16_t width = 40;              // пикселей; у последнего столбца 0 — «до правого края»
    Align align = Align::kLeft;      // деньги и числа — kRight
};

// Ячейка (row, col) — как ItemTextFn, но с номером столбца.
using CellTextFn = const char* (*)(int row, int col, char* buf, int cap, void* user);

// Таблица: строка заголовков + строки как в list(). Выделяется строка целиком.
// columns копировать не нужно: читается только во время вызова. column_count ≤ kMaxColumns.
ListEvent table(Context& ctx, WidgetId id, Rect r, ListState& state, const Column* columns,
                int column_count, int row_count, CellTextFn cell, void* user);

}  // namespace ui
