# Мини-игры: SDK, хост, minigame-runner (T050)

Контракт — [`src/minigames/sdk.h`](../../src/minigames/sdk.h). Этот документ поясняет его и описывает,
как добавить игру и как её запускает `minigame-runner` (T051). GDD: §6.4, §6.5, §7.7, §7.11, §8.9.

## 1. Модель

```
главный цикл ──start(index, params)──▶ ХОСТ ──колбэки Desc──▶ ИГРА
            ◀──take_result(&r)────────      (одна за раз, арена 32 КиБ)
```

- **Игра** — набор колбэков `Desc` и структура состояния. Ничего не знает о том, где запущена.
- **Хост** (`src/minigames/host.cpp`) — один на программу: арена состояния, рамка «окна», ввод,
  меню паузы, учёт времени, результат.
- **Кто запускает:** программа ОС «Игры» (`games_app.cpp`, играбельные), `game/` у ПК в зале
  (обслуживание, T052/T053), `minigame-runner` (T051), PC-флаг `--minigame <id>`.

### Почему поверх ОС, а не в окне ui

Окно ui (T030) отдаёт стик курсору, L/R — переключению окон, ○ — закрытию окна. Игре нужны все
три. Поэтому игра идёт в **полноэкранном слое хоста**: он рисует рамку в стиле ОС (заголовок с
названием игры, фаска — цвета `ui::kCol*`), под ней видна ОС или 3D-зал. Для игрока это выглядит
как развёрнутое окно; для кода — один и тот же путь в ОС, в зале и в раннере. Время клуба при
этом идёт (GDD §6.4): хост не трогает мир.

## 2. Жизненный цикл

| Шаг | Кто вызывает | Когда | Колбэк игры |
|---|---|---|---|
| `register_builtin()` → `register_game()` | main | загрузка, после `render/audio/ui::init` | `load()` — один раз |
| `start(index, p)` | ОС / game / раннер | игрок выбрал игру, начал ремонт | `start(state, p)` |
| `update(dt, input)` | главный цикл | каждый кадр, пока `running()`, до `begin_frame` | `update(state, dt, in)` (не на паузе) |
| `draw()` | главный цикл | каждый кадр, последним перед `end_frame` | `draw(state, canvas)` (и на паузе) |
| игра вернула `kFinished` | хост | тот же кадр | `result(state, &r)` → `kIdle` |
| пауза → «Выйти» / `abort()` | игрок / главный цикл | любой момент | `result()`, затем `outcome = kAborted` |
| `take_result(&r)` | запустивший | после `running() == false` | — |

Пока `running()`, главный цикл **не вызывает `ui::update`**, не отдаёт ввод игроку в мире и не
обрабатывает START сам (его берёт хост). `ui::draw()` и 3D рисуются как обычно — под игрой.

`shutdown` у игры нет: состояние — в арене хоста, ресурсы — в аренах render/audio, освобождаются
их `shutdown()`.

## 3. Память

- **Состояние попытки** — структура игры, `Desc::state_size ≤ kStateBudget` (32 КиБ). Хост держит
  одну статическую арену (`alignas(16)`), перед `start()` обнуляет её. Игра хранит в ней ВСЁ,
  что меняется за попытку: поле, змейку, таймеры, свой `core::Rng`.
- **Без аллокаций**: ни `new`, ни `std::vector`, ни `malloc` — ни в `update`, ни в `start`.
  Массивы — фиксированного размера внутри состояния.
- **Ресурсы** — только в `load()`: `render::texture_load_file`, `audio::sound_load_file`. Бюджет на
  игру — текстуры ≤ 32 КиБ (`kTextureBudget`), звуки ≤ 64 КиБ (`kSoundBudget`). Чаще хватает
  `fill` и текста.
- **Static-переменные игры** допустимы только для неизменяемого после `load()`: id ресурсов,
  кэш строк `str()`, таблицы-константы.
- Бюджет кадра на PSP: `update + draw ≤ 2 мс`, ≤ 256 вызовов отрисовки.

## 4. Ввод

Игра получает `Input` — маски только из `kGameButtons` (крестовина, ×, □, △) и стик.

| Кнопка | Кому |
|---|---|
| крестовина, ×, □, △, стик | игре |
| ○, START | хосту: меню паузы «Продолжить / Выйти» (↑/↓, ×; ○ — продолжить) |
| L, R, SELECT | никому (окна ОС / отладочный оверлей) |

Кадр, в котором открылась или закрылась пауза, игра не получает. Мини-игры обслуживания — «одна-две
кнопки и стик» (GDD §6.5).

## 5. Отрисовка

`draw(state, canvas)`: `canvas` — экранный прямоугольник `Desc::width × Desc::height` (≤ 476 × 236)
в центре рамки. Рисовать помощниками SDK (координаты относительно холста, обрезка по холсту):

- `minigame::fill(c, x, y, w, h, color)` — прямоугольник (`render::draw_rect`);
- `minigame::sprites(c, tex, list, n)` — спрайты одной текстуры пачкой;
- `minigame::text(c, x, y, utf8, color, font)` — текст шрифтом ОС;
- цвета — `ui::kCol*` или палитра ART.md §2 через `render::rgba`.

Фон холста хост не заливает — игра рисует его сама первым `fill`. Строки — `minigame::str(key)` в
`load()`; ключи `mg.<id>.*` в `data/strings_ru.json`. Рамку, заголовок и меню паузы рисует хост.

## 6. Результат

```cpp
struct Result { Outcome outcome; int32_t score; float quality; uint32_t duration_ms; };
```

| Поле | Играбельная (`kPlayable`) | Обслуживание (`kService`) |
|---|---|---|
| `outcome` | `kCompleted` — в рекорды; `kFailed` — нет | `kCompleted`; `kFailed` → quality = 0 |
| `score` | очки или секунды (§7) | можно 0 |
| `quality` | не используется | 0…1, хост обрезает |
| `duration_ms` | ставит хост: время без пауз | так же |

`kAborted` ставит хост, если игрок вышел через паузу или вызван `abort()`: **ничего не применять**
(ни рекорд, ни ремонт).

Применяет результат запустивший:
- играбельная → `core::cmd_submit_record(world, data_find_minigame(data, id), r.score)` (T053);
- обслуживание → `cmd_reboot / cmd_clean_dust / cmd_repair / cmd_replace_component / cmd_install /
  cmd_remove_virus (world, pc, …, r.quality)` (ядро уже считает §7.7: `wear −= 30 + 30·quality`,
  `dust · (1 − quality)`; quality = 0 → `kFailed`).

### quality для обслуживания (GDD §6.5)

Каждая игра определяет quality в своём заголовке, но по общей шкале:

| quality | Смысл |
|---|---|
| 1,0 | без ошибок и быстрее нормы |
| 0,6–0,9 | с мелкими ошибками |
| 0,1–0,5 | еле справился (половина зон, таймер на исходе) |
| 0 | провал: таймер вышел, сломал (`outcome = kFailed`) |

Длительность — 10–40 с, своим таймером внутри состояния (на экране — полоска). `StartParams::difficulty`
(0…1) приходит из состояния ПК (например, `dust / 100` для Чистки) и меняет объём работы, а не
формулу quality.

## 7. Очки и симуляция посетителей (GDD §7.11)

Посетители играют **без запуска кода игры**: ядро (T053, `src/core`, без зависимостей от
`src/minigames`, работает и в `balance-runner`) считает

```
score = S_g · skill^1,5 · min(perf, 1,2) · exp(N(0; 0,25))
```

по `MinigameRec` из `data/minigames.json`: `score_scale = S_g`, `invert`. Поэтому параметры
симуляции — только в данных, а SDK фиксирует **единицы `score`**, чтобы результат игрока и
посетителя стояли в одной таблице:

| `invert` | `score` игрока | `S_g` | Симуляция |
|---|---|---|---|
| `false` | очки, больше — лучше | очки игрока со skill = 1 (≈ хороший живой результат) | формула выше |
| `true` | целые секунды до победы, меньше — лучше | секунды такого игрока | `S_g / (skill^1,5 · min(perf,1,2) · exp(N))` |

Автор игры подбирает `S_g` по своим прогонам в `minigame-runner` (средний результат «хорошего»
прохождения) и пишет в `minigames.json`. Id играбельной игры = `id` в `minigames.json`;
`minigame::validate(data)` при загрузке сообщает игры без записи.

Id мини-игр обслуживания совпадают с `ServiceGame` (`data_types.h`) с префиксом `svc_`:
`svc_reboot`, `svc_clean`, `svc_replace`, `svc_crimp`, `svc_install`, `svc_virus`, `svc_network`.

## 8. Как добавить мини-игру (шаги для Codex)

1. Папка `src/minigames/<id>/`: `<id>.h` (одна функция `const minigame::Desc& <id>_desc();` и
   комментарий: правила, смысл `difficulty`/`variant`, как считаются score/quality) и `<id>.cpp`.
   Образец — `src/minigames/example/`.
2. В `<id>.cpp`: `struct State` (всё состояние попытки, `static_assert(sizeof(State) <= minigame::kStateBudget)`),
   колбэки `load/start/update/draw/result`, функция `<id>_desc()`, возвращающая `static Desc`.
3. Строки — в `data/strings_ru.json`, ключи `mg.<id>.*` (`mg.<id>.name` уже есть для игр из
   `minigames.json`). Проверка: `python3 tools/build_data.py data/ -o build/data.pak`.
4. Регистрация — одна строка в `src/minigames/registry.cpp` (`register_game(<id>_desc());`) и файл
   в `PCCLUB_MINIGAME_SOURCES` в `CMakeLists.txt`.
5. Играбельная — запись в `data/minigames.json` (`score_scale` по прогонам раннера, `invert`, `where`).
6. Тест в `tests/minigames/` (doctest): детерминизм по сиду, границы quality/score, конец попытки
   по таймеру — через `Desc` напрямую, без хоста и графики.
7. Проверка: PC — `build/pc/minigame-runner --game <id>` (T051) или `build/pc/pcclub --minigame <id>`;
   PSP — программа «Игры» в ОС (START → значок «Игры»).

## 9. minigame-runner (T051, только PC)

Отдельный исполняемый файл `src/tools/minigame_runner.cpp`: `platform + render + audio + ui +
pcclub_minigames`, без `core::World` (`data.pak` — только для строк). Запускает одну игру в хосте
на сером фоне и печатает результат.

```
build/pc/minigame-runner --game <id> [--kind playable|service] [--seed N] [--difficulty 0..1]
                         [--variant N] [--best N] [--frames N] [--screenshot out.bmp]
                         [--input script.txt] [--repeat N]
```

| Флаг | По умолчанию | Смысл |
|---|---|---|
| `--game` | обязателен | `Desc::id`; неизвестный → код 2 и список игр |
| `--kind` | первый бит `Desc::kinds` | `StartParams::kind` |
| `--seed`, `--difficulty`, `--variant`, `--best` | 1, 0,5, 0, нет | поля `StartParams` |
| `--frames N` | без ограничения | выйти через N кадров (`abort()`), если игра не кончилась |
| `--screenshot` | — | снимок последнего кадра (как у `pcclub`) |
| `--input` | — | сценарий ввода вместо клавиатуры: строки `<кадр> <кнопки>` (`120 cross`, `121 -`), dt фиксирован 1/30 с |
| `--repeat N` | 1 | N попыток подряд с сидами seed…seed+N−1 (для подбора S_g) |

Вывод — одна строка на попытку в stdout:

```
result game=snake seed=1 outcome=completed score=240 quality=0.000 duration_ms=48233
```

Код выхода: 0 — все попытки отыграны (любой outcome), 2 — ошибка аргументов, 1 — игра не
зарегистрировалась. Со `--input` и фиксированным dt прогон детерминирован — им проверяются
длительность 10–40 с у мини-игр обслуживания и диапазон quality.

## 10. Пример

`src/minigames/example/` — «Пример»: ползунок бегает по шкале, × — остановить в зелёной зоне,
5 попыток. `score = 100 · попаданий`, `quality = попаданий / 5`; `difficulty` сужает зону.
Виды — `kPlayable | kService`. Запуск: PC — `build/pc/pcclub --minigame example`, PC и PSP —
ОС → «Игры» → «Пример».
