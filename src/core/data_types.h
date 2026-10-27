#pragma once
// КОНТРАКТ (T020). Бинарные таблицы data.pak: записи фиксированного размера, которые
// tools/build_data.py пишет, а ядро читает прямо из буфера файла, без разбора и копирования.
// Формат файла, порядок секций и правила перевода из JSON/CSV — docs/formats/data_pak.md.
// Источник схемы — GDD §8. Менять — только задачей «меняет контракт», вместе с data_pak.md,
// версией kPakVersion и build_data.py.
//
// Правила раскладки (их проверяют static_assert ниже и build_data.py):
//   - little-endian, float — IEEE 754 binary32;
//   - поля выровнены естественно (u16 — на 2, u32/i32/float — на 4), размер записи кратен 4;
//   - паддинг явный (поля pad*), build_data.py пишет туда нули;
//   - ссылки между таблицами — индексы u16 в целевой таблице, kNone — «нет ссылки».
#include <cstddef>
#include <cstdint>

namespace core {

// ---------------------------------------------------------------------------
// Базовые типы

// Индекс записи в таблице (игра, архетип, событие...). Порядок записей = порядок в исходнике.
using Index = uint16_t;
// Индекс строки игрока в секции STRS (тексты из strings_ru.json). Текст — data_string().
using StrId = uint16_t;
// Индекс служебного имени в секции NAME: id записей, имена моделей и текстур, флагов.
using NameId = uint16_t;

constexpr uint16_t kNone = 0xFFFF;

// Дата — число дней от 2000-01-01 (это суббота). Отрицательные — раньше 2000 года
// (железо 1998 года выпуска). Тот же счёт в data.pak, сохранениях и ядре.
using DayNum = int32_t;

// Месяц и день без года — для периодов и праздников календаря: month * 100 + day (1103 = 3 ноября).
using MonthDay = uint16_t;

// ---------------------------------------------------------------------------
// Календарь: перевод дат. Алгоритм days_from_civil (Howard Hinnant), пролептический григорианский.

constexpr DayNum date_from_ymd(int year, int month, int day) {
    const int y = year - (month <= 2 ? 1 : 0);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int mp = (month + 9) % 12;
    const int doy = (153 * mp + 2) / 5 + day - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468 - 10957;  // 10957 — дней от 1970-01-01 до 2000-01-01
}

struct Ymd {
    int16_t year;
    uint8_t month;  // 1..12
    uint8_t day;    // 1..31
};

constexpr Ymd ymd_from_date(DayNum date) {
    const int z = date + 10957 + 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const int doe = z - era * 146097;
    const int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int mp = (5 * doy + 2) / 153;
    const int d = doy - (153 * mp + 2) / 5 + 1;
    const int m = mp < 10 ? mp + 3 : mp - 9;
    const int y = yoe + era * 400 + (m <= 2 ? 1 : 0);
    return {static_cast<int16_t>(y), static_cast<uint8_t>(m), static_cast<uint8_t>(d)};
}

// День недели: 0 — понедельник … 6 — воскресенье (как `weekday` в archetypes.json).
constexpr int weekday_of(DayNum date) {
    // 2000-01-01 — суббота (5).
    const int w = (date + 5) % 7;
    return w < 0 ? w + 7 : w;
}

constexpr MonthDay month_day_of(DayNum date) {
    const Ymd ymd = ymd_from_date(date);
    return static_cast<MonthDay>(ymd.month * 100 + ymd.day);
}

// ---------------------------------------------------------------------------
// Перечисления. Строковые значения в исходниках — в комментариях; build_data.py переводит
// их в числа строго по этому списку (порядок = значение).

// games.json: genre. Порядок задаёт и индекс в ArchetypeRec::affinity.
enum Genre : uint8_t {
    kGenreShooterTeam,   // shooter_team
    kGenreShooterArena,  // shooter_arena
    kGenreShooter3p,     // shooter_3p
    kGenreRts,           // rts
    kGenreTbs,           // tbs
    kGenreArpg,          // arpg
    kGenreMmo,           // mmo
    kGenreSim,           // sim
    kGenreAction,        // action
    kGenreRace,          // race
    kGenreSport,         // sport
    kGenreCasual,        // casual
    kGenreMinigame,      // minigame
    kGenreCount
};

// games.json: net.
enum NetReq : uint8_t {
    kNetNone,        // none
    kNetLan,         // lan
    kNetOnlineLow,   // online_low — хватает диал-апа
    kNetOnlineHigh,  // online_high — нужна выделенка
};

// games.json: tags (битовая маска).
enum GameTag : uint16_t {
    kTagTournament = 1u << 0,  // tournament — можно устроить турнир
    kTagTeam = 1u << 1,        // team
    kTagEvergreen = 1u << 2,   // evergreen
    kTagKids = 1u << 3,        // kids
    kTagAdult = 1u << 4,       // adult
};

// Компоненты ПК (GDD §6.2). hardware.csv: type. Порядок задаёт индекс в Pc::component/wear.
enum Component : uint8_t {
    kCompCpu,         // cpu
    kCompGpu,         // gpu
    kCompRam,         // ram
    kCompHdd,         // hdd
    kCompPsu,         // psu
    kCompMonitor,     // monitor
    kCompMouse,       // mouse
    kCompKeyboard,    // keyboard
    kCompHeadphones,  // headphones
    kCompChair,       // chair
    kComponentCount
};

// archetypes.json: flags (битовая маска).
enum ArchetypeFlag : uint32_t {
    kArchNoisy = 1u << 0,          // noisy — шумит: environment соседей хуже
    kArchMinigameLover = 1u << 1,  // minigame_lover — охотно играет в мини-игры (§7.11)
    kArchLikesGraphics = 1u << 2,  // likes_graphics — бонус +5/ч при perf ≥ 1,5 (§7.4)
    kArchSmoker = 1u << 3,         // smoker — курит в зале: пыль ×1,5 (§7.7)
    kArchNight = 1u << 4,          // night — берёт ночной пакет
};

// regulars.json: traits (битовая маска).
enum RegularTrait : uint32_t {
    kTraitCompetitive = 1u << 0,   // competitive
    kTraitLoud = 1u << 1,          // loud
    kTraitShy = 1u << 2,           // shy
    kTraitGenerous = 1u << 3,      // generous
    kTraitTroublemaker = 1u << 4,  // troublemaker
};

// upgrades.csv: kind. Что значат level/arg/value/days — в таблице data_pak.md.
enum UpgradeKind : uint8_t {
    kUpgHall,          // hall — зал: value = число мест
    kUpgInternet,      // internet — level 0 нет, 1 диал-ап, 2+ выделенка; value = кбит/с
    kUpgNetwork,       // network — level 1 хаб, 2 свитч, 3 свитч + файл-сервер
    kUpgAircon,        // aircon — value = множитель перегрева (0,5)
    kUpgSoundproof,    // soundproof — value = прибавка к environment
    kUpgRenovation,    // renovation — value = прибавка к comfort
    kUpgSign,          // sign — постоянная реклама: value = бонус в F_ad, arg = архетип/kNone
    kUpgAd,            // ad — временная реклама на days дней: value = бонус, arg = архетип/kNone
    kUpgNightTariff,   // night_tariff — открывает ночной режим
    kUpgStaffAdmin,    // staff_admin — второй администратор
    kUpgStaffGuard,    // staff_guard — охранник
    kUpgStaffCleaner,  // staff_cleaner — уборщица: value = множитель пыли (0,6)
    kUpgAntivirus,     // antivirus — value = AV (§7.8)
    kUpgradeKindCount
};

// minigames.json: where (битовая маска).
enum MinigameWhere : uint8_t {
    kMgAdmin = 1u << 0,     // admin — на ПК администратора
    kMgVisitors = 1u << 1,  // visitors — на ПК посетителей без покупки
};

// Мини-игры обслуживания (GDD §6.5) — код, а не данные. Нужны эффекту start_minigame.
enum ServiceGame : uint8_t {
    kSvcReboot,   // reboot
    kSvcClean,    // clean
    kSvcReplace,  // replace
    kSvcCrimp,    // crimp
    kSvcInstall,  // install
    kSvcVirus,    // virus
    kSvcNetwork,  // network
    kServiceGameCount
};

// calendar.json: periods[].tag.
enum PeriodTag : uint8_t {
    kPeriodHolidays,  // holidays
    kPeriodSession,   // session
};

// events: category.
enum EventCategory : uint8_t {
    kCatCrisis,       // crisis — неприятность, требует решения
    kCatOpportunity,  // opportunity — выгодное предложение
    kCatCalendar,     // calendar — праздник, сезон
    kCatRegular,      // regular — связано с постоянным клиентом
    kCatInfo,         // info — новость, рассылка, без последствий
    kCatStory,        // story — сюжетный режим (позже)
};

// events: trigger.type (GDD §8.6).
enum TriggerType : uint8_t {
    kTrigDailyRoll,  // daily_roll — раз в день в trigger_hour, бросок chance
    kTrigDate,       // date — в trigger_date, в trigger_hour
    kTrigOnAction,   // on_action — после действия игрока trigger_action
    kTrigOnState,    // on_state — условия выполнились впервые (проверка раз в игровой час)
    kTrigScheduled,  // scheduled — только через эффект schedule_event
};

// events: trigger.action для on_action. Ядро сообщает о них системе событий.
enum ActionId : uint8_t {
    kActRecordBeaten,      // record_beaten — игрок побил рекорд постоянного клиента
    kActPcBought,          // pc_bought — на пустом месте собран рабочий ПК
    kActTournamentEnd,     // tournament_end
    kActGameInstalled,     // game_installed
    kActPirateInstalled,   // pirate_installed
    kActUpgradeBought,     // upgrade_bought
    kActPcBroken,          // pc_broken — сломался компонент
    kActPcHung,            // pc_hung
    kActVisitorLeftAngry,  // visitor_left_angry — ушёл без обслуживания или рано
    kActRegularUnlocked,   // regular_unlocked
    kActDayClosed,         // day_closed — после итогов дня
    kActMonthStarted,      // month_started — после платежей 1-го числа
    kActGoodsOut,          // goods_out — товар буфета кончился
    kActionCount
};

// events: present.kind.
enum PresentKind : uint8_t {
    kPresentMail,     // mail — письмо в почте
    kPresentVisitor,  // visitor — посетитель у стойки с репликой
    kPresentPhone,    // phone — звонок
    kPresentWorld,    // world — изменение в мире
};

// Переменные условий и шансов (GDD §8.6). arg — индекс в таблице, указанной в комментарии.
enum CondVar : uint8_t {
    kVarMoney,            // money — деньги, ₽
    kVarRep,              // rep — репутация 0..100
    kVarDay,              // day — номер игрового дня, день старта = 1
    kVarHour,             // hour — 0..23
    kVarWeekday,          // weekday — 0 пн … 6 вс
    kVarDate,             // date — DayNum (val в исходнике — строка "YYYY-MM-DD")
    kVarPcs,              // pcs — число рабочих ПК
    kVarPirateShare,      // pirate_share — доля пиратских установок 0..1
    kVarPirateInstalls,   // pirate_installs — число пиратских установок
    kVarFlag,             // flag:<id> — arg = индекс флага (секция FLAG), значение 0/1
    kVarRelation,         // relation:<regular> — arg = постоянный клиент
    kVarGameInstalled,    // game_installed:<id> — arg = игра, значение = число ПК с игрой
    kVarVisitsArchetype,  // visits_archetype:<a> — arg = архетип, всего визитов
    kVarCompetitorActive, // competitor_active — 0/1
    kCondVarCount
};

enum CondOp : uint8_t {
    kOpEq,  // ==
    kOpNe,  // !=
    kOpLt,  // <
    kOpLe,  // <=
    kOpGt,  // >
    kOpGe,  // >=
};

// Эффекты (GDD §8.6). Раскладка параметров по полям EffectRec — таблица в data_pak.md.
enum EffectType : uint8_t {
    kEffMoney,          // money
    kEffRep,            // rep
    kEffFlag,           // flag
    kEffRelation,       // relation
    kEffModifier,       // modifier
    kEffSpawnVisitor,   // spawn_visitor
    kEffScheduleEvent,  // schedule_event
    kEffUninstall,      // uninstall
    kEffDamagePc,       // damage_pc
    kEffItem,           // item
    kEffUnlock,         // unlock
    kEffChance,         // chance
    kEffStartMinigame,  // start_minigame
    kEffectTypeCount
};

// Цели модификатора (эффект modifier, target).
enum ModTarget : uint8_t {
    kModArrivals,  // arrivals (arg = kNone) или arrivals:<a> (arg = архетип) — множитель в F_evt_a
    kModHwPrice,   // hw_price — множитель цен железа (F_fx, §7.9)
    kModPop,       // pop:<game> — множитель популярности игры (§7.6)
    kModDust,      // dust — множитель накопления пыли (§7.7)
    kModTargetCount
};

// ---------------------------------------------------------------------------
// Лимиты таблиц. build_data.py отказывается собирать pak, если они превышены.

constexpr int kMaxGames = 256;       // installed bitset[256] в сохранении (§8.8)
constexpr int kMaxArchetypes = 16;
constexpr int kMaxRegulars = 64;
constexpr int kMaxHardware = 256;
constexpr int kMaxGoods = 32;
constexpr int kMaxUpgrades = 64;
constexpr int kMaxMinigames = 16;
constexpr int kMaxEvents = 512;
constexpr int kMaxFlags = 512;       // flags bitset[512] в сохранении (§8.8)
constexpr int kMaxArchetypeModels = 4;

// ---------------------------------------------------------------------------
// Заголовок и каталог секций (раскладка файла — data_pak.md)

constexpr uint32_t fourcc(char a, char b, char c, char d) {
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

constexpr uint32_t kPakMagic = fourcc('P', 'C', 'C', 'D');
constexpr uint16_t kPakVersion = 1;

struct PakHeader {
    uint32_t magic;          // kPakMagic
    uint16_t version;        // kPakVersion
    uint16_t section_count;
    uint32_t file_size;      // размер всего файла в байтах
    uint32_t crc32;          // CRC-32 (как zlib.crc32) байтов [16, file_size)
};

struct PakSection {
    uint32_t tag;       // fourcc, см. kSec*
    uint32_t offset;    // от начала файла, кратно 16
    uint32_t size;      // байт
    uint32_t count;     // число записей (для блобов — см. data_pak.md)
    uint16_t rec_size;  // sizeof записи; загрузчик сверяет с sizeof(T)
    uint16_t pad;
};

constexpr uint32_t kSecGames = fourcc('G', 'A', 'M', 'E');       // GameRec
constexpr uint32_t kSecArchetypes = fourcc('A', 'R', 'C', 'H');  // ArchetypeRec
constexpr uint32_t kSecRegulars = fourcc('R', 'E', 'G', 'U');    // RegularRec
constexpr uint32_t kSecHardware = fourcc('H', 'A', 'R', 'D');    // HardwareRec
constexpr uint32_t kSecGoods = fourcc('G', 'O', 'O', 'D');       // GoodsRec
constexpr uint32_t kSecUpgrades = fourcc('U', 'P', 'G', 'R');    // UpgradeRec
constexpr uint32_t kSecMinigames = fourcc('M', 'I', 'N', 'I');   // MinigameRec
constexpr uint32_t kSecEvents = fourcc('E', 'V', 'N', 'T');      // EventRec
constexpr uint32_t kSecChoices = fourcc('C', 'H', 'O', 'I');     // ChoiceRec
constexpr uint32_t kSecEffects = fourcc('E', 'F', 'F', 'E');     // EffectRec
constexpr uint32_t kSecConds = fourcc('C', 'O', 'N', 'D');       // CondRec
constexpr uint32_t kSecChance = fourcc('C', 'H', 'N', 'C');      // ChanceTermRec
constexpr uint32_t kSecLists = fourcc('L', 'I', 'S', 'T');       // uint16_t
constexpr uint32_t kSecPeriods = fourcc('P', 'E', 'R', 'I');     // PeriodRec
constexpr uint32_t kSecHolidays = fourcc('H', 'O', 'L', 'I');    // MonthDay
constexpr uint32_t kSecFlags = fourcc('F', 'L', 'A', 'G');       // NameId: имя флага по индексу
constexpr uint32_t kSecEconomy = fourcc('E', 'C', 'O', 'N');     // EconomyRec, ровно 1
constexpr uint32_t kSecStrings = fourcc('S', 'T', 'R', 'S');     // строки игрока (UTF-8)
constexpr uint32_t kSecStringKeys = fourcc('S', 'T', 'R', 'K');  // StringKeyRec
constexpr uint32_t kSecNames = fourcc('N', 'A', 'M', 'E');       // служебные имена (ASCII)

// ---------------------------------------------------------------------------
// Записи таблиц. Комментарий справа — поле исходника и смещение.

// data/games.json (GDD §8.2)
struct GameRec {
    NameId id;                    //  0 id
    StrId title;                  //  2 title
    uint8_t genre;                //  4 genre → Genre
    uint8_t net;                  //  5 net → NetReq
    uint16_t tags;                //  6 tags → GameTag
    DayNum release;               //  8 release
    int32_t price_license;        // 12 price_license, ₽ за одно место
    int32_t price_pirate;         // 16 price_pirate, ₽ за диск
    uint16_t req_cpu;             // 20 req.cpu, индекс
    uint16_t req_gpu;             // 22 req.gpu, индекс
    uint16_t req_ram_mb;          // 24 req.ram
    Index minigame;               // 26 minigame → MINI или kNone
    uint32_t req_hdd_mb;          // 28 req.hdd_mb
    float pop_peak;               // 32 pop.peak
    float pop_floor;              // 36 pop.floor
    uint16_t pop_rise_days;       // 40 pop.rise_days (R)
    uint16_t pop_half_life_days;  // 42 pop.half_life_days (H)
    NameId box_tex;               // 44 box_tex
    uint16_t pad;                 // 46
    float score_scale;            // 48 score_scale (S_g, §7.11), 0 — не мини-игра
};

// data/archetypes.json (GDD §8.3)
struct ArchetypeRec {
    NameId id;                        //   0 id
    StrId name;                       //   2 name
    float base_rate;                  //   4 base_rate (B_a), посетителей в час
    float hourly[24];                 //   8 hourly (H_a)
    float weekday[7];                 // 104 weekday (W_a), с понедельника
    float season_holidays;            // 132 season.holidays (S_a в каникулы), по умолчанию 1
    float season_session;             // 136 season.session (S_a в сессию), по умолчанию 1
    int32_t money_min;                // 140 money[0], ₽
    int32_t money_max;                // 144 money[1]
    int32_t price_expect;             // 148 price_expect (P_exp_a), ₽/ч
    float elasticity;                 // 152 elasticity (e_a)
    uint16_t patience_min;            // 156 patience[0], игровых минут
    uint16_t patience_max;            // 158 patience[1]
    float skill_min;                  // 160 skill[0], 0..1
    float skill_max;                  // 164 skill[1]
    uint8_t session_hours_min;        // 168 session_hours[0]
    uint8_t session_hours_max;        // 169 session_hours[1]
    uint8_t model_count;              // 170 len(models)
    uint8_t pad;                      // 171
    float extend;                     // 172 extend (ext_a)
    float alt_accept;                 // 176 alt_accept (p_alt_a), по умолчанию 0,6
    float affinity[kGenreCount];      // 180 affinity по Genre; жанр не указан → 1,0
    float virus;                      // 232 virus (v_a)
    float hunger;                     // 236 hunger (hunger_a)
    float rep_weight;                 // 240 rep_weight (w_a)
    uint32_t flags;                   // 244 flags → ArchetypeFlag
    NameId models[kMaxArchetypeModels];  // 248 models; лишние — kNone
};

// data/regulars.json (GDD §8.4)
struct RegularRec {
    NameId id;                  //  0 id
    StrId name;                 //  2 name
    StrId nick;                 //  4 nick — литерал, кладётся в STRS как есть
    Index archetype;            //  6 archetype → ARCH
    NameId model;               //  8 model
    Index fav_game;             // 10 fav_game → GAME или kNone
    Index fav_minigame;         // 12 fav_minigame → MINI или kNone
    uint8_t palette;            // 14 palette
    uint8_t unlock_count;       // 15 len(unlock)
    uint16_t unlock_first;      // 16 unlock → COND, первая запись
    uint16_t event_pool_first;  // 18 event_pool → LIST (индексы EVNT), первая запись
    uint8_t event_pool_count;   // 20 len(event_pool)
    uint8_t pad[3];             // 21
    uint32_t traits;            // 24 traits → RegularTrait
    float skill;                // 28 skill
};

// data/hardware.csv (GDD §8.5). Пустая ячейка CSV → 0.
struct HardwareRec {
    NameId id;           //  0 id
    StrId name;          //  2 name
    uint8_t type;        //  4 type → Component
    int8_t comfort;      //  5 comfort, слагаемое comfort(c) (§7.5)
    uint16_t score;      //  6 score — индекс CPU/GPU (§7.4)
    DayNum release;      //  8 release
    int32_t base_price;  // 12 base_price, ₽ (§7.9)
    uint16_t ram_mb;     // 16 ram_mb
    uint16_t power_w;    // 18 power_w
    uint32_t hdd_mb;     // 20 hdd_mb — ёмкость диска
    float wear_rate;     // 24 wear_rate (r_k, §7.7), за час нагрузки
    float fail_base;     // 28 fail_base (f0_k, §7.7)
};

// data/goods.csv (GDD §8.9)
struct GoodsRec {
    NameId id;           //  0 id
    StrId name;          //  2 name
    int32_t buy_price;   //  4 buy_price — закупка, ₽
    int32_t sell_price;  //  8 sell_price — стартовая цена продажи, ₽
    float weight;        // 12 weight — вес при выборе товара голодным посетителем
};

// data/upgrades.csv (GDD §8.9). Пустая ячейка → 0 (requires/arg → kNone).
struct UpgradeRec {
    NameId id;            //  0 id
    StrId name;           //  2 name
    uint8_t kind;         //  4 kind → UpgradeKind
    uint8_t level;        //  5 level — ступень внутри kind
    Index arg;            //  6 archetype → ARCH или kNone (для sign/ad)
    Index requires_upgrade;  //  8 requires → UPGR или kNone
    uint16_t days;        // 10 days — срок действия (ad), 0 — постоянно
    int32_t price;        // 12 price — разовая покупка, ₽
    int32_t cost_month;   // 16 cost_month — 1-го числа, ₽
    int32_t cost_day;     // 20 cost_day — ежедневно, ₽
    int32_t cost_hour;    // 24 cost_hour — за час работы клуба, ₽
    float cost_minute;    // 28 cost_minute — за минуту онлайн-игры (диал-ап), ₽
    float value;          // 32 value — параметр kind
};

// data/minigames.json (GDD §8.9)
struct MinigameRec {
    NameId id;          //  0 id — по нему код находит модуль мини-игры (sdk.h)
    StrId name;         //  2 name
    uint8_t invert;     //  4 invert — 1: меньше очков = лучше (время, Сапёр)
    uint8_t where;      //  5 where → MinigameWhere
    uint16_t pad;       //  6
    float score_scale;  //  8 score_scale (S_g, §7.11)
};

// data/events/*.json (GDD §8.6)
struct EventRec {
    NameId id;                //  0 id
    uint8_t category;         //  2 category → EventCategory
    uint8_t trigger;          //  3 trigger.type → TriggerType
    uint8_t trigger_hour;     //  4 trigger.hour; не задан → 0xFF
    uint8_t trigger_action;   //  5 trigger.action → ActionId (только on_action)
    uint8_t once;             //  6 once
    uint8_t present_kind;     //  7 present.kind → PresentKind
    DayNum trigger_date;      //  8 trigger.date (только date)
    float weight;             // 12 weight
    float chance_base;        // 16 chance.base; нет chance → 1
    uint16_t cooldown_days;   // 20 cooldown_days
    uint16_t cond_first;      // 22 conditions → COND
    uint16_t chance_first;    // 24 chance.per → CHNC
    uint16_t choice_first;    // 26 choices → CHOI
    uint8_t cond_count;       // 28
    uint8_t chance_count;     // 29
    uint8_t choice_count;     // 30 ≥ 1
    uint8_t pad;              // 31
    NameId present_model;     // 32 present.model или kNone
    StrId present_text;       // 34 present.text
};

struct ChoiceRec {
    StrId text;             // 0 text
    uint8_t req_count;      // 2 len(requires)
    uint8_t effect_count;   // 3 len(effects)
    uint16_t req_first;     // 4 requires → COND
    uint16_t effect_first;  // 6 effects → EFFE
};

// Условие: {var, op, val} (GDD §8.6).
struct CondRec {
    uint8_t var;  // 0 → CondVar
    uint8_t op;   // 1 → CondOp
    Index arg;    // 2 параметр переменной (flag:<id> и т. п.) или kNone
    float val;    // 4
};

// Слагаемое шанса: chance = base + Σ coef · var (chance.per).
struct ChanceTermRec {
    uint8_t var;  // 0 → CondVar
    uint8_t pad;  // 1
    Index arg;    // 2
    float coef;   // 4
};

// Эффект. Значение полей зависит от type — таблица в data_pak.md.
struct EffectRec {
    uint8_t type;          //  0 → EffectType
    uint8_t sub;           //  1 подтип
    uint8_t hour;          //  2 schedule_event: час, 0xFF — час триггера события
    uint8_t child_count;   //  3 chance: число эффектов then
    Index ref;             //  4 главный индекс
    Index arg;             //  6 второй индекс
    uint16_t child_first;  //  8 chance: первый эффект then в EFFE
    uint16_t pad;          // 10
    int32_t ival;          // 12
    float fval;            // 16
};

// data/calendar.json: periods (GDD §8.7). Период может переходить через Новый год (to < from).
struct PeriodRec {
    NameId id;         // 0 id
    uint8_t tag;       // 2 tag → PeriodTag
    uint8_t pad;       // 3
    MonthDay from_md;  // 4 from, включительно
    MonthDay to_md;    // 6 to, включительно
};

// data/economy.json + calendar.json:start (GDD §8.9). В секции ровно одна запись.
struct EconomyRec {
    DayNum start_date;                   //  0 calendar.json: start
    int32_t start_money;                 //  4 start_money, ₽
    float start_rep;                     //  8 start_rep
    int32_t start_price_hour;            // 12 start_price_hour, ₽/ч
    int32_t start_price_night;           // 16 start_price_night, ₽ за ночной пакет
    float tax_rate;                      // 20 tax_rate — доля месячной выручки
    float electricity_price;             // 24 electricity_price, ₽ за кВт·ч
    float loan_rate_week;                // 28 loan_rate_week — доля долга в неделю
    int32_t debt_limit;                  // 32 debt_limit, ₽
    uint16_t debt_days_to_lose;          // 36 debt_days_to_lose
    uint16_t start_upgrades_first;       // 38 start_upgrades → LIST (индексы UPGR)
    uint8_t start_upgrades_count;        // 40
    uint8_t start_pcs;                   // 41 start_pcs — собранных ПК на старте
    Index start_pc[kComponentCount];     // 42 start_pc → HARD по Component, kNone — пусто
    uint16_t pad;                        // 62
};

// Индекс ключей строк: сортирован по hash, для data_find_string().
struct StringKeyRec {
    uint32_t hash;  // 0 fnv1a32(ключ)
    StrId str;      // 4
    uint16_t pad;   // 6
};

// FNV-1a 32 бита по байтам UTF-8 ключа. Тот же алгоритм в build_data.py.
constexpr uint32_t fnv1a32(const char* s) {
    uint32_t h = 2166136261u;
    for (; *s; ++s) {
        h ^= static_cast<uint8_t>(*s);
        h *= 16777619u;
    }
    return h;
}

// Размеры записей — часть формата. Изменился размер → поднять kPakVersion и data_pak.md.
static_assert(sizeof(PakHeader) == 16, "PakHeader");
static_assert(sizeof(PakSection) == 20, "PakSection");
static_assert(sizeof(GameRec) == 52, "GameRec");
static_assert(sizeof(ArchetypeRec) == 256, "ArchetypeRec");
static_assert(offsetof(ArchetypeRec, affinity) == 180, "ArchetypeRec::affinity");
static_assert(offsetof(ArchetypeRec, models) == 248, "ArchetypeRec::models");
static_assert(sizeof(RegularRec) == 32, "RegularRec");
static_assert(sizeof(HardwareRec) == 32, "HardwareRec");
static_assert(sizeof(GoodsRec) == 16, "GoodsRec");
static_assert(sizeof(UpgradeRec) == 36, "UpgradeRec");
static_assert(sizeof(MinigameRec) == 12, "MinigameRec");
static_assert(sizeof(EventRec) == 36, "EventRec");
static_assert(sizeof(ChoiceRec) == 8, "ChoiceRec");
static_assert(sizeof(CondRec) == 8, "CondRec");
static_assert(sizeof(ChanceTermRec) == 8, "ChanceTermRec");
static_assert(sizeof(EffectRec) == 20, "EffectRec");
static_assert(sizeof(PeriodRec) == 8, "PeriodRec");
static_assert(sizeof(EconomyRec) == 64, "EconomyRec");
static_assert(offsetof(EconomyRec, start_pc) == 42, "EconomyRec::start_pc");
static_assert(sizeof(StringKeyRec) == 8, "StringKeyRec");

}  // namespace core
