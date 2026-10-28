#pragma once
// КОНТРАКТ (T050). SDK мини-игр: играбельные (Змейка, Сапёр… — GDD §6.4) и мини-игры
// обслуживания (Перезагрузка, Чистка… — GDD §6.5). Реализация хоста и реестра —
// src/minigames/host.cpp (T050). Пояснения, шаги «как добавить игру», интерфейс
// minigame-runner — docs/formats/minigame.md. Менять — только задачей «меняет контракт».
//
// ГДЕ ИГРАЕТСЯ
//   Мини-игра всегда идёт в ХОСТЕ — полноэкранном слое поверх ОС или 3D-мира. Хост рисует
//   рамку «окна» в стиле ОС (заголовок, рамка — цвета ui::kCol*), внутри — холст игры размером
//   Desc::width × Desc::height (≤ 476 × 236 = наибольшая клиентская область окна ОС), по центру.
//   Почему не окно ui: в окне стик двигает курсор, L/R переключают окна, ○ закрывает окно —
//   всё это мешает игре. Хост забирает ввод целиком, а на вид это развёрнутое окно ОС.
//   Одна и та же игра одинаково идёт: из программы «Игры» в ОС (games_app.cpp), у ПК в зале
//   (game/, T052/T053), в minigame-runner (T051) и по флагу --minigame <id> (PC).
//   Время клуба при этом НЕ останавливается (GDD §6.4) — мир тикает как обычно.
//
// КТО ВЫЗЫВАЕТ
//   - register_builtin() — game/ (main) при загрузке, после render/audio/ui::init. Вызывает
//     Desc::load каждой игры.
//   - start / update / draw / take_result / abort — главный цикл (main, game/, раннер).
//     Пока running(), главный цикл зовёт ui::update с ПУСТЫМ InputState (ОС не видит ввода,
//     но и не держит нажатия прошлого кадра), не отдаёт ввод игроку в мире и не обрабатывает
//     START сам; ui::draw (ОС под игрой) и 3D рисуются как обычно, minigame::draw() — последним.
//   - Колбэки Desc — только хост, из update()/draw(). Всё — из главного потока.
//
// ПАМЯТЬ
//   Игра НЕ выделяет память вообще (ни new, ни vector, ни static-буферов под уровень сверх
//   своего состояния). Всё изменяемое состояние попытки — структура игры размером
//   Desc::state_size ≤ kStateBudget; хост держит одну статическую арену (идёт одна игра за раз)
//   и передаёт указатель в колбэки. Арена выровнена на 16 и перед start() обнулена.
//   Ресурсы (текстуры, звуки) — в Desc::load при загрузке, в аренах render/audio; бюджет на
//   игру — kTextureBudget / kSoundBudget. Указатели const char* из str() кэшировать в load()
//   в static-переменных — это разрешено (не состояние попытки).
//
// ПРИМЕР (игра) — полностью см. src/minigames/example/example.cpp
//
//     struct State { float t; int hits; bool done; };
//     static const char* g_hint;
//     static bool load() { g_hint = minigame::str("mg.example.hint"); return true; }
//     static void start(void* s, const minigame::StartParams&) { /* арена уже нулевая */ }
//     static minigame::Status update(void* p, float dt, const minigame::Input& in) {
//         State& s = *static_cast<State*>(p);
//         s.t += dt;
//         if (in.pressed & platform::kButtonCross) ++s.hits;
//         return s.t > 10.0f ? minigame::Status::kFinished : minigame::Status::kRunning;
//     }
//     static void draw(const void* p, const minigame::Canvas& c) {
//         minigame::fill(c, 0, 0, c.w, c.h, ui::kColField);
//         minigame::text(c, 4, 4, g_hint, ui::kColText);
//     }
//     static void result(const void* p, minigame::Result* out) {
//         const State& s = *static_cast<const State*>(p);
//         out->score = s.hits * 100;
//         out->quality = s.hits / 5.0f;
//     }
//     const minigame::Desc& example_desc() {
//         static minigame::Desc d = [] {
//             minigame::Desc x{};
//             x.id = "example"; x.title_key = "mg.example.name";
//             x.kinds = minigame::kPlayable | minigame::kService;
//             x.width = 240; x.height = 120; x.state_size = sizeof(State);
//             x.load = load; x.start = start; x.update = update; x.draw = draw; x.result = result;
//             return x;
//         }();
//         return d;
//     }
//
// ПРИМЕР (главный цикл)
//
//     minigame::register_builtin();                       // при загрузке
//     ...
//     minigame::start(minigame::find_game("snake"), params);
//     while (platform::poll()) {
//         const bool mg = minigame::running();
//         if (mg) minigame::update(dt, platform::input());
//         ui::update(dt, mg ? platform::InputState{} : platform::input());
//         minigame::Result r;
//         if (minigame::take_result(&r) && r.outcome == minigame::Outcome::kCompleted)
//             core::cmd_submit_record(world, core::data_find_minigame(data, "snake"), r.score);
//         render::begin_frame(...); ... 3D ...; ui::draw();
//         minigame::draw();                                // последним, поверх всего
//         render::end_frame();
//     }
#include <cstdint>

#include "core/data_api.h"
#include "platform/platform_api.h"
#include "render/render_api.h"
#include "ui/ui_api.h"

namespace minigame {

// ---------------------------------------------------------------------------
// Лимиты

constexpr int kMaxGames = 24;                          // зарегистрированных игр всех видов
constexpr uint32_t kStateBudget = 32u * 1024u;         // состояние попытки, байт (арена хоста)
constexpr uint32_t kTextureBudget = 32u * 1024u;       // текстуры игры в арене render, байт
constexpr uint32_t kSoundBudget = 64u * 1024u;         // звуки игры (WAV в памяти), байт
constexpr int kMaxCanvasWidth = ui::kMaxClientWidth;   // 476
constexpr int kMaxCanvasHeight = ui::kMaxClientHeight; // 236
// Бюджет кадра на PSP (замер — оверлей SELECT): update + draw ≤ 2 мс, ≤ 256 вызовов
// render::draw_rect / draw_sprites / text за кадр. Спрайты одной текстуры — пачкой.

// Кнопки, которые получает игра. ○ и START забирает хост (меню паузы), L/R и SELECT —
// не отдаются никогда (L/R — окна ОС, SELECT — отладочный оверлей).
constexpr uint32_t kGameButtons = platform::kButtonUp | platform::kButtonDown |
                                  platform::kButtonLeft | platform::kButtonRight |
                                  platform::kButtonCross | platform::kButtonSquare |
                                  platform::kButtonTriangle;
constexpr uint32_t kPauseButtons = platform::kButtonCircle | platform::kButtonStart;

// ---------------------------------------------------------------------------
// Типы

// Вид мини-игры (битовая маска в Desc::kinds; в StartParams::kind — ровно один бит).
enum Kind : uint8_t {
    kPlayable = 1u << 0,  // играбельная: результат — score, идёт в доску рекордов (§7.11)
    kService = 1u << 1,   // обслуживание ПК: результат — quality 0…1 (§6.5), 10–40 с
};

// Параметры попытки. Заполняет тот, кто запускает (ОС, game/, раннер).
struct StartParams {
    uint32_t seed = 1;          // сид для core::Rng игры; одинаковый сид + ввод → одинаковая попытка
    float difficulty = 0.5f;    // 0…1. Service: из состояния ПК (напр. dust/100 для Чистки);
                                // playable: 0,5. Смысл шкалы описывает игра в своём заголовке
    int32_t best_score = 0;     // текущий рекорд (для «Рекорд: N»); has_best = 0 — рекорда нет
    uint8_t has_best = 0;
    uint8_t kind = kPlayable;   // в каком качестве запущена (один бит из Desc::kinds)
    uint8_t variant = 0;        // уточнение для service: core::Component для Замены и т.п.
    uint8_t pad = 0;
};

// Ввод кадра. Маски — только из kGameButtons; стик — как platform::InputState.
struct Input {
    uint32_t held = 0;
    uint32_t pressed = 0;
    uint32_t released = 0;
    float stick_x = 0.0f;   // −1 … +1, мёртвая зона уже вычтена
    float stick_y = 0.0f;   // −1 (вниз) … +1 (вверх)
};

// Холст игры в экранных пикселях. Игра рисует только внутри; helpers ниже обрезают по нему.
struct Canvas {
    int16_t x = 0, y = 0;   // левый верхний угол на экране
    int16_t w = 0, h = 0;   // = Desc::width/height
};

enum class Status : uint8_t {
    kRunning,   // продолжаем
    kFinished,  // попытка окончена — хост вызовет result() и закроет игру
};

enum class Outcome : uint8_t {
    kCompleted,  // доиграна: score/quality в силе
    kFailed,     // провал (Сапёр подорвался, таймер ремонта вышел): в рекорды не идёт, quality = 0
    kAborted,    // игрок вышел через паузу. Ставит хост; ничего не применять
};

struct Result {
    Outcome outcome = Outcome::kAborted;
    int32_t score = 0;         // playable: в единицах minigames.json (см. ниже), ≥ 0
    float quality = 0.0f;      // service: 0…1 (хост обрезает); playable — не используется
    uint32_t duration_ms = 0;  // активное время попытки без паузы — считает хост
};

// ОЧКИ И СИМУЛЯЦИЯ ПОСЕТИТЕЛЕЙ (GDD §7.11)
//   Посетители играют без запуска кода игры: ядро (T053, src/core) считает результат формулой
//   по данным MinigameRec (data/minigames.json): score_scale = S_g, invert. Ядро не зависит от
//   src/minigames, поэтому параметры симуляции — только в данных, а SDK задаёт ЕДИНИЦЫ:
//   - invert = false: score — очки, больше = лучше. S_g — результат посетителя со skill = 1 и
//     perf = 1 (без шума), т.е. «очень сильный игрок» ≈ хороший результат живого игрока.
//   - invert = true: score — время в целых секундах, меньше = лучше. S_g — время такого же
//     игрока. Для симуляции время делится на skill^1,5 · min(perf, 1,2) · exp(N(0; 0,25)).
//   Автор игры подбирает S_g по своим прогонам в minigame-runner и пишет его в
//   minigames.json. Id играбельной игры = id в minigames.json (validate() это проверяет).

struct Desc {
    const char* id = nullptr;         // ASCII snake_case, уникален; для kPlayable = id в minigames.json,
                                      // для kService — "svc_reboot", "svc_clean"… (docs/formats/minigame.md)
    const char* title_key = nullptr;  // ключ strings_ru.json: заголовок рамки ("mg.snake.name")
    uint8_t kinds = kPlayable;        // маска Kind
    int16_t width = 240;              // холст, ≤ kMaxCanvasWidth × kMaxCanvasHeight
    int16_t height = 160;
    uint32_t state_size = 0;          // sizeof(состояния), 1 … kStateBudget

    // Колбэки. state — арена хоста (state_size байт, выравнивание 16).
    // Один раз при register_game(): загрузить текстуры/звуки, закэшировать str(). Необязателен.
    // false — игра не регистрируется (причина — в stderr).
    bool (*load)() = nullptr;
    // ОБЯЗАТЕЛЕН. Новая попытка: арена обнулена, разложить поле, засеять core::Rng из p.seed.
    void (*start)(void* state, const StartParams& p) = nullptr;
    // ОБЯЗАТЕЛЕН. Раз в кадр, пока не пауза. dt — секунды, 0 < dt ≤ 0,1. Без аллокаций.
    Status (*update)(void* state, float dt, const Input& input) = nullptr;
    // ОБЯЗАТЕЛЕН. Раз в кадр (и на паузе — кадр замирает под меню). Только чтение состояния.
    void (*draw)(const void* state, const Canvas& canvas) = nullptr;
    // ОБЯЗАТЕЛЕН. После kFinished: заполнить outcome (kCompleted/kFailed), score, quality.
    // При выходе через паузу тоже вызывается (хост затем ставит kAborted) — для отладки раннера.
    void (*result)(const void* state, Result* out) = nullptr;
    // Пауза/продолжение (заглушить зацикленные звуки). Необязателен.
    void (*on_pause)(void* state, bool paused) = nullptr;
};

// ---------------------------------------------------------------------------
// Реестр (при загрузке)

using GameIndex = int8_t;
constexpr GameIndex kNoGame = -1;

// Проверяет Desc (обязательные колбэки, размеры, state_size, уникальность id), вызывает load().
// Копирует Desc. kNoGame — отказ (причина — в stderr).
GameIndex register_game(const Desc& desc);
// Регистрирует все встроенные игры — список в src/minigames/registry.cpp.
void register_builtin();
int game_count();
const Desc* game(GameIndex index);       // nullptr — неверный индекс
GameIndex find_game(const char* id);     // kNoGame — нет такой

// Сверка с data.pak: возвращает число kPlayable-игр (кроме "example"), которых нет в
// minigames.json, — каждую строкой в stderr. Записи minigames.json без кода — не ошибка
// (игры приходят по вехам M5/M9): они только перечисляются в stderr. Для main и тестов.
int validate(const core::GameData& data);

// ---------------------------------------------------------------------------
// Хост: одна игра за раз

enum class HostState : uint8_t {
    kIdle,     // ничего не идёт
    kRunning,  // игра идёт (update вызывается)
    kPaused,   // меню паузы: «Продолжить» / «Выйти»
};

// Запустить попытку. p.kind должен входить в Desc::kinds. false — неверный индекс, kind или
// уже идёт игра (сначала abort()). Обнуляет арену, вызывает Desc::start.
bool start(GameIndex index, const StartParams& p);
HostState state();
inline bool running() { return state() != HostState::kIdle; }
GameIndex current();                     // kNoGame при kIdle

// Раз в кадр после platform::poll(), до render::begin_frame(), пока running(). Фильтрует ввод
// до kGameButtons, обрабатывает ○/START (пауза), вызывает Desc::update. Кадр, в котором игра
// вернула kFinished, — последний: хост вызывает result(), запоминает Result и уходит в kIdle.
void update(float dt, const platform::InputState& input);

// Между begin_frame и end_frame, последним. Рисует рамку, холст (Desc::draw), меню паузы.
// При kIdle — ничего.
void draw();

// Прервать игру (клуб закрывается, ПК сгорел, тесты): результат с Outcome::kAborted.
void abort();
// Забрать результат последней законченной попытки (один раз). false — результата нет.
bool take_result(Result* out);

// ---------------------------------------------------------------------------
// Помощники для Desc::draw (координаты — относительно холста; всё обрезается по холсту)

void fill(const Canvas& canvas, int x, int y, int w, int h, render::Color color);
// Спрайты с координатами относительно холста; не попавшие целиком в холст — не рисуются.
// count ≤ 64 за вызов.
void sprites(const Canvas& canvas, render::TextureId texture, const render::Sprite* list, int count);
// Текст шрифтом ОС (ui::draw_text), max_width по умолчанию — до правого края холста.
int text(const Canvas& canvas, int x, int y, const char* utf8, render::Color color,
         ui::Font font = ui::Font::kUi12, int max_width = 0);
// Строка игрока по ключу (= ui::str). Звать в load(), хранить указатель.
inline const char* str(const char* key) { return ui::str(key); }

}  // namespace minigame
