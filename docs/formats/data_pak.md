# Формат `data.pak`

**Контракт (T020).** Пишет — `tools/build_data.py` (T021), читает — `core::data_load()` (`src/core/data_api.h`, реализация T022). Структуры записей — `src/core/data_types.h`, источник схемы — GDD §8. Изменение формата = задача «меняет контракт»: правится этот файл, `data_types.h`, `kPakVersion` и `build_data.py` одновременно.

## 1. Общее

- Один файл, бюджет — до 1 МиБ (GDD §10.3). Ядро не копирует данные: игра загружает файл в буфер, выровненный на 16, и читает записи прямо из него.
- **Порядок байтов** — little-endian. `float` — IEEE 754 binary32. Строки — UTF-8 с завершающим нулём.
- **Индексы.** Каждая ссылка на запись другой таблицы — `u16`-индекс в этой таблице. Индекс = порядковый номер записи в секции. `0xFFFF` (`kNone`) — «нет ссылки».
- **Даты** — `i32`, число дней от 2000-01-01 (`DayNum`): 2000-01-01 → 0, 2001-09-01 → 609, 1999-12-31 → −1. Для `MM-DD` без года — `u16` `month * 100 + day`.
- **Деньги** — `i32`, рубли. Вероятности и множители — `float`.
- Все записи — фиксированного размера, размер кратен 4, паддинг — нули.

### Порядок записей

Записи таблицы идут **в порядке исходника**: для JSON-массива — по порядку элементов; для `data/events/*.json` — файлы по имени (сортировка байтов UTF-8), внутри файла — по порядку. Порядок стабилен между сборками, если исходники не менялись.

## 2. Раскладка файла

```
PakHeader              16 байт, смещение 0
PakSection[count]      20 байт каждая, сразу за заголовком
(паддинг нулями до кратного 16)
секция 0               смещение кратно 16
(паддинг)
секция 1
...
```

### `PakHeader`

| Смещение | Тип | Поле | Значение |
|---|---|---|---|
| 0 | u32 | `magic` | `'P','C','C','D'` = `0x44434350` |
| 4 | u16 | `version` | `1` (`kPakVersion`) |
| 6 | u16 | `section_count` | число записей каталога |
| 8 | u32 | `file_size` | размер файла целиком |
| 12 | u32 | `crc32` | CRC-32 байтов `[16, file_size)` — как `zlib.crc32` |

### `PakSection`

| Смещение | Тип | Поле | Значение |
|---|---|---|---|
| 0 | u32 | `tag` | fourcc: 4 ASCII-символа в порядке байтов (`GAME` → байты `47 41 4D 45`) |
| 4 | u32 | `offset` | от начала файла, кратно 16 |
| 8 | u32 | `size` | байт в секции (без хвостового паддинга) |
| 12 | u32 | `count` | число записей |
| 16 | u16 | `rec_size` | размер записи; для строковых секций — 1 |
| 18 | u16 | `pad` | 0 |

Для табличной секции `size == count * rec_size`. Каждый тег встречается не больше одного раза. Загрузчик сверяет `rec_size` с `sizeof` своей структуры: расхождение → ошибка `kBadSection` (так ловится рассинхрон `build_data.py` и `data_types.h`). Неизвестные теги загрузчик пропускает.

### Секции

| Тег | Запись | Размер | Обязательна | Источник |
|---|---|---|---|---|
| `GAME` | `GameRec` | 52 | | `data/games.json` |
| `ARCH` | `ArchetypeRec` | 256 | | `data/archetypes.json` |
| `REGU` | `RegularRec` | 32 | | `data/regulars.json` |
| `HARD` | `HardwareRec` | 32 | | `data/hardware.csv` |
| `GOOD` | `GoodsRec` | 16 | | `data/goods.csv` |
| `UPGR` | `UpgradeRec` | 36 | | `data/upgrades.csv` |
| `MINI` | `MinigameRec` | 12 | | `data/minigames.json` |
| `EVNT` | `EventRec` | 36 | | `data/events/*.json` |
| `CHOI` | `ChoiceRec` | 8 | | `choices` событий |
| `EFFE` | `EffectRec` | 20 | | `effects` вариантов, `then` эффектов `chance` |
| `COND` | `CondRec` | 8 | | `conditions`, `requires`, `unlock` |
| `CHNC` | `ChanceTermRec` | 8 | | `chance.per` событий |
| `LIST` | `u16` | 2 | | списки индексов (`event_pool`, `start_upgrades`) |
| `PERI` | `PeriodRec` | 8 | | `data/calendar.json: periods` |
| `HOLI` | `u16` (MonthDay) | 2 | | `data/calendar.json: holidays` |
| `FLAG` | `u16` (NameId) | 2 | | имена флагов событий |
| `ECON` | `EconomyRec` | 64 | да, `count = 1` | `data/economy.json` + `calendar.json: start` |
| `STRS` | строки | 1 | да | `data/strings_ru.json` + ники |
| `STRK` | `StringKeyRec` | 8 | да | ключи `strings_ru.json` |
| `NAME` | строки | 1 | да | служебные имена |

Отсутствующая необязательная секция = пустая таблица. Порядок секций в файле — любой; `build_data.py` пишет в порядке этой таблицы.

**Лимиты** (`data_types.h`, `kMax*`): игр ≤ 256, архетипов ≤ 16, постоянных клиентов ≤ 64, железа ≤ 256, товаров ≤ 32, апгрейдов ≤ 64, мини-игр ≤ 16, событий ≤ 512, флагов ≤ 512, моделей у архетипа ≤ 4. Списки внутри записи (`conditions`, `choices`, `effects`, `chance.per`, `unlock`, `event_pool`, `then`) — не длиннее 255. Общие таблицы `CHOI`, `EFFE`, `COND`, `CHNC`, `LIST`, а также `STRS` и `NAME` — не больше 65 535 записей.

## 3. Строки

### `STRS` и `NAME`

Обе секции устроены одинаково (`rec_size = 1`, `count` — **число строк**):

```
u32 offsets[count]     смещение начала строки от начала секции
байты строк            UTF-8, каждая с завершающим 0
```

`StrId`/`NameId` — индекс в `offsets`. `size` — полный размер (таблица смещений + байты).

- **`STRS`** — тексты игрока: все значения `strings_ru.json` плюс ники постоянных клиентов (`nick` — литерал, без ключа). Сначала строки `strings_ru.json` в порядке ключей в файле, затем ники в порядке `regulars.json`. Одинаковые тексты не объединяются.
- **`NAME`** — служебные ASCII-имена: `id` всех записей всех таблиц, имена моделей (`models`, `model`, `present.model`), текстур (`box_tex`), флагов. Одинаковые имена хранятся один раз: `NameId` уникален для строки.

### `STRK` — ключи строк

Записи `{u32 hash; u16 str; u16 pad}`, **отсортированы по `hash` по возрастанию**; `str` — индекс в `STRS`. `hash` — FNV-1a 32 бита по байтам UTF-8 ключа (`core::fnv1a32`):

```python
def fnv1a32(key: str) -> int:
    h = 0x811C9DC5
    for b in key.encode("utf-8"):
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h
```

Совпадение хешей двух разных ключей — **ошибка сборки** с обоими ключами в сообщении. Ники в `STRK` не попадают.

## 4. Таблицы

Поля и смещения — в комментариях структур `data_types.h`; здесь — правила перевода. Значения перечислений — по спискам в `data_types.h`: строка исходника из комментария → порядковый номер. Неизвестная строка → ошибка.

### `GAME` ← `games.json` (GDD §8.2)

| Исходник | Поле | Правило |
|---|---|---|
| `id` | `id` | NameId |
| `title` | `title` | ключ строки → StrId |
| `ref` | — | не пакуется (только для разработчика) |
| `genre` | `genre` | `Genre` |
| `release` | `release` | `"YYYY-MM-DD"` → DayNum |
| `price_license`, `price_pirate` | | ₽, ≥ 0 |
| `req.cpu`, `req.gpu`, `req.ram`, `req.hdd_mb` | `req_*` | ≥ 0; `cpu`/`gpu`/`ram` ≤ 65535 |
| `net` | `net` | `NetReq` |
| `pop.peak`, `pop.floor` | | `0 ≤ floor ≤ peak`, `peak > 0` |
| `pop.rise_days`, `pop.half_life_days` | | ≥ 0 |
| `minigame` | `minigame` | id из `minigames.json` или `null` → kNone |
| `score_scale` | `score_scale` | float, по умолчанию 0 |
| `tags` | `tags` | маска `GameTag` |
| `box_tex` | `box_tex` | NameId, `null` → kNone |

### `ARCH` ← `archetypes.json` (GDD §8.3)

| Исходник | Поле | Правило |
|---|---|---|
| `name` | `name` | StrId |
| `base_rate` | | ≥ 0 |
| `hourly` | `hourly[24]` | ровно 24 значения ≥ 0 |
| `weekday` | `weekday[7]` | ровно 7 значений ≥ 0, с понедельника |
| `season.holidays`, `season.session` | `season_*` | по умолчанию 1,0 |
| `money` | `money_min/max` | `[min, max]`, `0 ≤ min ≤ max` |
| `price_expect` | | > 0 |
| `elasticity` | | ≥ 0 |
| `patience` | `patience_min/max` | минуты, `1 ≤ min ≤ max` |
| `skill` | `skill_min/max` | `0 ≤ min ≤ max ≤ 1` |
| `session_hours` | `session_hours_min/max` | `1 ≤ min ≤ max ≤ 24` |
| `extend`, `alt_accept`, `virus`, `hunger` | | `[0, 1]`; `alt_accept` по умолчанию 0,6 |
| `rep_weight` | | ≥ 0, по умолчанию 0,3 |
| `affinity` | `affinity[13]` | по `Genre`; жанр не указан → **1,0**; ключ не из `Genre` → ошибка; значения ≥ 0 |
| `flags` | `flags` | маска `ArchetypeFlag` |
| `models` | `models[4]`, `model_count` | 1–4 имени, лишние слоты — kNone |

### `REGU` ← `regulars.json` (GDD §8.4)

| Исходник | Поле | Правило |
|---|---|---|
| `name` | `name` | StrId |
| `nick` | `nick` | литерал → новая строка в `STRS` |
| `archetype` | `archetype` | индекс ARCH |
| `model` | `model` | NameId |
| `palette` | | 0–255 |
| `fav_game` | | индекс GAME или `null` → kNone |
| `fav_minigame` | | индекс MINI или `null` → kNone |
| `skill` | | `[0, 1]` |
| `unlock` | `unlock_first/count` | условия → `COND` (§5) |
| `traits` | `traits` | маска `RegularTrait` |
| `event_pool` | `event_pool_first/count` | id событий → индексы EVNT в `LIST` |

### `HARD` ← `hardware.csv` (GDD §8.5)

Первая строка — заголовок, столбцы по имени (порядок не важен). Пустая ячейка → 0.

| Столбец | Поле | Правило |
|---|---|---|
| `type` | `type` | `Component` |
| `name` | `name` | StrId |
| `release` | | DayNum |
| `base_price` | | > 0 |
| `score` | | обязателен (> 0) для `cpu`, `gpu` |
| `ram_mb` | | обязателен для `ram` |
| `hdd_mb` | | обязателен для `hdd` |
| `power_w` | | ≥ 0 |
| `wear_rate`, `fail_base` | | ≥ 0 |
| `comfort` | | −128…127 |

### `GOOD` ← `goods.csv` (GDD §8.9)

Столбцы `id,name,buy_price,sell_price,weight`. `buy_price > 0`, `sell_price > 0`, `weight ≥ 0` (по умолчанию 1).

### `UPGR` ← `upgrades.csv` (GDD §8.9)

Столбцы `id,kind,name,level,archetype,requires,days,price,cost_month,cost_day,cost_hour,cost_minute,value`. Пустые `archetype`/`requires` → kNone, остальные пустые → 0.

| `kind` | `level` | `value` | `archetype` | `days` |
|---|---|---|---|---|
| `hall` | ступень | число мест (≤ 16) | — | — |
| `internet` | 0 нет, 1 диал-ап, 2+ выделенка | кбит/с | — | — |
| `network` | 1 хаб, 2 свитч, 3 сервер | — | — | — |
| `aircon` | — | множитель перегрева (0,5) | — | — |
| `soundproof` | — | прибавка к environment | — | — |
| `renovation` | — | прибавка к comfort | — | — |
| `sign` | — | бонус в `F_ad` | архетип или пусто (все) | — |
| `ad` | — | бонус в `F_ad` | архетип или пусто (все) | > 0 |
| `night_tariff` | — | — | — | — |
| `staff_admin`, `staff_guard` | — | — | — | — |
| `staff_cleaner` | — | множитель пыли (0,6) | — | — |
| `antivirus` | — | AV (§7.8) | — | — |

`requires` не может ссылаться на саму запись; цикл → ошибка.

### `MINI` ← `minigames.json` (GDD §8.9)

`id`, `name` (StrId), `score_scale` (> 0), `invert` (bool → 0/1, по умолчанию false), `where` (список из `admin`, `visitors` → маска `MinigameWhere`).

### `PERI`, `HOLI` ← `calendar.json` (GDD §8.7)

`periods[]`: `id`, `tag` (`PeriodTag`), `from`/`to` — `"MM-DD"` → MonthDay, оба включительно; `to < from` — период через Новый год. `holidays[]` — `"MM-DD"` → MonthDay. Даты проверяются на существование (29 февраля допустимо).

### `ECON` ← `economy.json` + `calendar.json: start` (GDD §8.9)

Ровно одна запись. `start_date` — из `calendar.json`. Остальные поля — `economy.json`, все обязательны:

| Исходник | Поле | Правило |
|---|---|---|
| `start_money` | | ₽ |
| `start_rep` | | `[0, 100]` |
| `start_price_hour`, `start_price_night` | | > 0 |
| `tax_rate`, `loan_rate_week` | | `[0, 1]` |
| `electricity_price` | | ≥ 0 |
| `debt_limit` | | > 0 |
| `debt_days_to_lose` | | > 0 |
| `start_upgrades` | `start_upgrades_first/count` | id апгрейдов → `LIST`; ровно один `hall` |
| `start_pcs` | | `0 ≤ n ≤` мест стартового зала |
| `start_pc` | `start_pc[10]` | объект `{тип: id железа}`; ключ — `Component`, тип записи должен совпадать с ключом; не указан → kNone; обязательны `cpu gpu ram hdd psu monitor mouse keyboard` |

## 5. События (GDD §8.6)

### `EVNT`

| Исходник | Поле | Правило |
|---|---|---|
| `category` | | `EventCategory` |
| `trigger.type` | `trigger` | `TriggerType` |
| `trigger.hour` | `trigger_hour` | 0–23; нет → `0xFF`. Обязателен для `daily_roll` |
| `trigger.date` | `trigger_date` | обязателен для `date`, иначе 0 |
| `trigger.action` | `trigger_action` | `ActionId`, обязателен для `on_action`, иначе 0 |
| `weight` | | > 0, по умолчанию 1 |
| `chance.base` | `chance_base` | `[0, 1]`; нет `chance` → 1 |
| `chance.per` | `chance_first/count` | объект `{переменная: коэффициент}` → `CHNC` |
| `cooldown_days` | | 0–65535, по умолчанию 0 |
| `once` | | bool, по умолчанию false |
| `conditions` | `cond_first/count` | → `COND` |
| `present.kind` | `present_kind` | `PresentKind` |
| `present.model` | `present_model` | NameId, нет → kNone |
| `present.text` | `present_text` | StrId, обязателен |
| `choices` | `choice_first/count` | ≥ 1 варианта; событие без выбора — один вариант «Ок» |

Записи `CHOI`, `COND`, `CHNC`, `EFFE` одного события лежат подряд; `*_first` при `*_count = 0` — 0.

### `CHOI`

`text` → StrId; `requires` → `COND` (`req_first/count`); `effects` → `EFFE` (`effect_first/count`).

### Переменные (`COND`, `CHNC`)

Строка переменной разбирается как `имя` или `имя:параметр`:

| Строка | `var` | `arg` | `val` в условии |
|---|---|---|---|
| `money`, `rep`, `day`, `hour`, `weekday`, `pcs`, `pirate_share`, `pirate_installs`, `competitor_active` | одноимённая | kNone | число |
| `date` | `kVarDate` | kNone | `"YYYY-MM-DD"` → DayNum (как float) |
| `flag:<id>` | `kVarFlag` | индекс флага | 0/1 |
| `relation:<regular>` | `kVarRelation` | индекс REGU | число |
| `game_installed:<game>` | `kVarGameInstalled` | индекс GAME | число ПК |
| `visits_archetype:<a>` | `kVarVisitsArchetype` | индекс ARCH | число |

`op` → `CondOp` по строке (`==`, `!=`, `<`, `<=`, `>`, `>=`). В `CHNC` `op` не используется (байт 1 = 0), `coef` — коэффициент.

**Флаги.** Индекс флага назначает `build_data.py`: все имена из `flag:<id>` и эффектов `flag`, без повторов, **по алфавиту** (байты UTF-8). Секция `FLAG` — NameId имени по индексу флага. Флаг, который проверяется, но нигде не ставится, — предупреждение (не ошибка).

### `EFFE` — раскладка эффектов

Поля: `type`, `sub`, `hour`, `child_count`, `ref`, `arg`, `child_first`, `ival`, `fval`. Неиспользуемые: `ref`/`arg` = kNone, `hour` = `0xFF`, остальное — 0.

| `type` | Исходник | Раскладка |
|---|---|---|
| `money` | `val` | `sub = 0`, `ival = val` |
| `money` | `expr: "<число> * <переменная>"` | `sub = 1`, `fval = число`, `ref = var` (`CondVar`), `arg` = параметр переменной. Итог в ядре — `money_round(fval · значение)`. Другой вид выражения → ошибка |
| `rep` | `val` | `fval = val` |
| `flag` | `id`, `val` | `ref` = индекс флага, `ival` = 0/1 |
| `relation` | `regular`, `val` | `ref` = индекс REGU, `ival = val` |
| `modifier` | `target`, `mult`, `days` | `sub` = `ModTarget`; `arg` = архетип (`arrivals:<a>`), игра (`pop:<game>`) или kNone; `fval = mult` (> 0); `ival = days` (> 0) |
| `spawn_visitor` | `archetype` / `regular` | `sub = 0`, `ref` = ARCH / `sub = 1`, `ref` = REGU |
| `schedule_event` | `id`, `in_days`, `hour` | `ref` = EVNT; `ival = in_days` (≥ 0); `hour` = 0–23 или нет → `0xFF` |
| `uninstall` | `filter` | `sub`: `pirate` → 0, `all` → 1, `game:<id>` → 2 и `ref` = GAME |
| `damage_pc` | `pc`, `component` | `ref` = номер места 0–15 или `"random"` → kNone; `arg` = `Component` или `"random"` → kNone |
| `item` | `id`, `count` | `sub = 0`, `ref` = HARD, или `sub = 1`, `ref` = GOOD (id ищется в обеих таблицах; найден в обеих → ошибка); `ival = count` (может быть < 0) |
| `unlock` | `id` | `sub = 0`, `ref` = UPGR, или `sub = 1`, `ref` = GAME (+1 лицензия) |
| `chance` | `p`, `then` | `fval = p` (`[0, 1]`); эффекты `then` → `EFFE`, `child_first/child_count`. Вложенность `chance` в `chance` разрешена до глубины 4 |
| `start_minigame` | `id` | `sub = 0`, `ref` = MINI, или `sub = 1`, `ref` = `ServiceGame` (`reboot`, `clean`, `replace`, `crimp`, `install`, `virus`, `network`) |

## 6. Проверки `build_data.py`

Каждая ошибка — с файлом, `id` записи и полем (`data/games.json: kontr_udar_13.req.cpu: ожидалось число ≥ 0`). При любой ошибке `data.pak` не пишется, код выхода ≠ 0.

1. `id` — `snake_case` латиницей (`[a-z][a-z0-9_]*`), уникален в своей таблице. Все события из всех файлов — одна таблица.
2. Каждая ссылка (`archetype`, `fav_game`, `event_pool`, `schedule_event.id`, параметры переменных...) указывает на существующую запись.
3. Каждый ключ строки из записей есть в `strings_ru.json`. Ключ из `strings_ru.json`, на который ничего не ссылается, — **не** ошибка (их использует UI из кода).
4. Диапазоны и обязательные поля — по таблицам §4–§5. Неизвестное поле в записи — ошибка (ловит опечатки).
5. Лимиты — §2.
6. Коллизии `STRK` — §3.

## 7. Пример

Минимальный pak: одна игра, без событий.

```
00000000  50 43 43 44 01 00 05 00  ...           magic "PCCD", version 1, 5 секций
00000010  47 41 4D 45 80 00 00 00  34 00 00 00  01 00 00 00  34 00 00 00   GAME @0x80, 52 байта, 1 × 52
...       45 43 4F 4E ...                                                    ECON
...       53 54 52 53 ...                                                    STRS
...       53 54 52 4B ...                                                    STRK
...       4E 41 4D 45 ...                                                    NAME
```

Чтение в ядре:

```cpp
const core::GameRec& g = data.games[0];
const char* title = core::data_string(data, g.title);          // «Контр-Удар 1.3»
const core::Ymd rel = core::ymd_from_date(g.release);           // 2001-06-01
```
