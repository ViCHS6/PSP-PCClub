# Формат сохранения

**Контракт (T060).** Пишет — `core::save_world()`, читает — `core::load_world()` (`src/core/save_api.h`, реализация `src/core/save.cpp`). Хранит байты — `platform::save_write/save_read` (`src/platform/save_api.h`). Источник схемы — GDD §8.8. Изменение формата = задача «меняет контракт»: правится этот файл, `save.cpp` и `kSaveVersion` одновременно.

## 1. Общее

- Один слот, один файл, **меньше 64 КБ** (`kSaveMaxSize`; на срезе — около 16 КБ).
- Порядок байтов — little-endian, `float` — IEEE 754 binary32, без выравнивания (поля идут вплотную). PSP и PC пишут одинаковые байты: сохранение переносится между ними.
- Типы и смысл полей — как в `World` (`src/core/core_api.h`). Индексы записей — `u16` в таблицах `data.pak`, `0xFFFF` — нет. Даты — `i32` `DayNum`. Деньги — `i32`.
- **Когда.** Только при `can_save()`: утром (`kMorning`, до открытия) и в момент закрытия (`kClosed`, до `cmd_end_day`). В другие фазы `save_world` → `kWrongPhase`.
- **Посетители не сохраняются** (GDD §8.8). При сохранении они «уходят»: занятый ПК (`kBusy`) пишется как `kIdle`, у всех ПК `visitor = kNoVisitor`, `running_game = kNone`; `RegularState::in_club = 0`; `ActiveEvent::visitor = kNoVisitor`. Очередь, уведомления, внутренние поля посетителей (`priv.vis_*`, `ev_visitor_serial`) не пишутся. Ночные посетители при сохранении в `kClosed` после загрузки пропадают — деньги за ночь уже в `stats.today`.
- **Точность.** Остальное пишется без потерь, включая ГПСЧ и внутренние кэши: после загрузки утреннего сохранения тики дают тот же мир, что без сохранения.

## 2. Раскладка файла

```
SaveHeader    32 байта
секция DATA   { tag u32, size u32, payload[size] }
секция CLCK
...           строго в порядке §4; неизвестная или пропущенная секция — ошибка
```

### `SaveHeader`

| Смещение | Тип | Поле | Значение |
|---|---|---|---|
| 0 | u32 | `magic` | `'P','C','C','B'` (`kSaveMagic`) |
| 4 | u16 | `version` | `1` (`kSaveVersion`) |
| 6 | u16 | `header_size` | `32` |
| 8 | u32 | `file_size` | размер файла целиком |
| 12 | u32 | `crc32` | CRC-32 байтов `[16, file_size)` — как `zlib.crc32` |
| 16 | u32 | `seed` | текущее состояние ГПСЧ (`World::rng.state`) — сид продолжения |
| 20 | i32 | `date` | `clock.date` |
| 24 | u16 | `minute` | `clock.minute` |
| 26 | u8 | `phase` | `clock.phase`: 0 `kMorning` или 2 `kClosed` |
| 27 | u8 | — | 0 |
| 28 | i32 | `day_index` | `clock.day_index` |

`save_peek()` читает только заголовок (после проверки CRC) — для меню «Продолжить».

## 3. Совместимость

- `version != kSaveVersion` → `kBadVersion`. Миграций нет: формат поменялся — старые сохранения не читаются (до релиза это нормально).
- **data.pak.** Секция `DATA` хранит для 9 таблиц число записей и отпечаток — FNV-1a 32 (`fnv1a32` из `data_types.h`) по служебным id первых `count` записей, каждый id с завершающим нулём. При загрузке отпечаток пересчитывается по текущему `data.pak` на те же `count` записей:
  - таблица дописана в конец — загрузка проходит, новые записи получают значения `world_init` (цены буфета, открытые постоянные клиенты…);
  - записи переставлены, переименованы, удалены, `count` больше текущего → `kDataMismatch`.
- Массивы «по записям таблицы» пишутся длиной сохранённого `count` (обозначения ниже: **G** игры, **A** архетипы, **R** постоянные клиенты, **H** железо, **O** товары, **U** апгрейды, **M** мини-игры, **E** события, **F** флаги). Битсеты и пулы — фиксированной длины (`kMax*` из `core_api.h`).
- Загрузка проверяет каждый индекс (`< count` своей таблицы или `0xFFFF`, где допустимо) и каждое перечисление → `kBadValue`.

## 4. Секции

`size` — длина payload в байтах; чтение секции должно съесть ровно `size`, иначе `kBadSection`.

### `DATA` — отпечаток data.pak

9 раз `{ count u16, hash u32 }` в порядке: games, archetypes, regulars, hardware, goods, upgrades, minigames, events, flags.

### `CLCK` — время (дата, минута, фаза, день — в заголовке)

| Тип | Поле |
|---|---|
| u8 | `weekday` 0..6 |
| u8 | `speed` 1/2/4 |
| u8 | `holidays`, `session`, `holiday` (3 байта) |
| u16 | `next_serial` |

### `ECON` — экономика (GDD §8.8 Economy)

`money i32`, `debt i32`, `debt_over_limit_days u16`, `rep f32`, `price_hour i32`, `price_night i32`, `online_minutes_today f32`, `goods_price i32[O]`.

### `CLUB` — клуб (GDD §8.8 Club)

`seats u8`, `internet_level u8`, `network_level u8`, `upgrades u32[kMaxUpgrades/32 = 2]`.

### `PCS ` — компьютеры (GDD §8.8 PC[16])

`count u8` = 16, затем 16 раз:

| Тип | Поле |
|---|---|
| u16[10] | `component` — индекс H или 0xFFFF, по `Component` |
| f32[10] | `wear` |
| f32 | `dust` |
| u8 | `state` (`PcState` 0..5; `kBusy` не встречается) |
| u8 | `virus` |
| u8 | `broken_component` (`Component` или 0xFF) |
| u16 | `load_minutes_today` |
| u32[8] | `installed` — битсет игр |
| u32[8] | `installed_pirate` |
| u8 | минут нагрузки с последнего часового шага (`priv.pc_load_min`) |

### `INVT` — склад и заказы (GDD §8.8 Inventory)

`hardware u16[H]`, `goods u16[O]`, `order_count u8` (≤ 32), затем `order_count` раз `{ kind u8 (OrderKind 0..3), ref u16 (H/O/G по kind), count u16, arrives i32 }`.

### `LIBR` — библиотека (GDD §8.8 Library)

`licenses u8[G]`, `pirate u32[8]`.

### `REGU` — постоянные клиенты (GDD §8.8 Regulars)

R раз: `relation i8`, `unlocked u8`, `visits u16`, `sat_sum f32`, `last_visit i32` (`priv.reg_last_visit`).

### `RECS` — рекорды (GDD §8.8 Records)

M раз: `count u8` (≤ 10), затем `count` раз `{ score i32, date i32, who u16 (R, 0xFFFE игрок или 0xFFFF), archetype u16 (A или 0xFFFF) }`.

### `EVNT` — события (GDD §8.8 Events)

| Тип | Поле |
|---|---|
| u16[E] | `cooldown_until` |
| u32[16] | `fired_once` |
| u32[16] | `flags` |
| 32 × `{ event u16, hour u8, date i32 }` | `scheduled` — все слоты по порядку, пустой — event 0xFFFF |
| 16 × `{ event u16, mail u8, date i32, minute u16 }` | `active` — все слоты; `visitor` не пишется |
| i32, u8 | `priv.ev_hour_date`, `priv.ev_hour` |
| u32[16] | `priv.ev_state_done` |

### `MODS` — модификаторы (GDD §8.8 Modifiers)

32 слота `{ target u8 (ModTarget, kModTargetCount — пусто), arg u16, mult f32, until i32 }`, затем `ad_until i32[U]` (`priv.ad_until`, реклама).

### `COMP` — конкурент (GDD §8.8 Competitor)

`active u8`, `rep f32`, `perf f32`, `lib f32`, `price i32`.

### `MAIL` — почта

`count u8` (≤ 64), `unread u8`, затем `count` раз `{ kind u8 (MailKind 0..4), read u8, answered u8, choice u8, ref u16, date i32, amount i32 }`.

### `STAT` — статистика

`today DayStats`, `days DayStats[30]` (всё кольцо), `head u8`, `count u8`, `month_revenue i32`, `visits_archetype u32[A]`.

`DayStats`: `date i32`, `revenue i32[5]`, `expenses i32[8]`, `money_end i32`, `debt_end i32`, `rep_end f32`, `sat_sum f32`, `arrived u16`, `served u16`, `extended u16`, `left u16[11]`, `load_minutes u16`, `pc_hangs u16`, `pc_failures u16` — 104 байта.

### `DAY ` — внутридневные счётчики и кэши

`open_minutes_today u16`, `cache_valid u8`, `pop_today f32[G]`, `lib_appeal f32[A]`. Если при загрузке `count` игр или архетипов в data.pak больше сохранённого — `cache_valid = 0` (кэш пересчитается).

## 5. Ошибки загрузки

Проверка идёт до изменения мира: при любой ошибке `World` остаётся как был, игра начинает новую (`world_init`) или показывает, что сохранение повреждено. Порядок проверок: размер ≥ 32 → `magic` → `version` → `header_size`/`file_size` (= размеру данных, ≤ 64 КБ) → CRC → секции по порядку → диапазоны.

## 6. Хранилище (platform)

- **PSP:** `sceUtilitySavedata`, папка `ms0:/PSP/SAVEDATA/PCCB00001SAVE/` (на Go — `ef0:`; путь выбирает система), файл `DATA.BIN` без шифрования, `PARAM.SFO` с подписями из `SaveMeta`. Автосохранение и загрузка при старте — `AUTOSAVE`/`AUTOLOAD` (без вопросов), явные — `SAVE`/`LOAD` с системным подтверждением. Значка `ICON0.PNG` пока нет — список показывает стандартный.
- **PC:** `save.bin` рядом с исполняемым файлом; запись через `save.tmp` + переименование.
