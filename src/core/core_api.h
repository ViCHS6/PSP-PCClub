#pragma once
// КОНТРАКТ (T020). Ядро симуляции: состояние мира, тик, запросы и команды игрока.
// Источник истины — GDD §4–§7. Реализация — src/core/ (T022; события — T023).
// Менять — только задачей «меняет контракт».
//
// КТО ВЫЗЫВАЕТ
//   - game/ (главный цикл): world_init, tick по таймеру, take_notices, команды от взаимодействий;
//   - ui/ (программы ОС): читает World напрямую (const), вызывает cmd_* и запросы;
//   - balance-runner (T025): то же без графики, тики в цикле;
//   - тесты: что угодно.
// Все функции — из одного потока. Ядро не знает о платформе, рендере и UI.
//
// ПАМЯТЬ
//   World — один большой тривиально копируемый объект с массивами фиксированного размера.
//   Его хранит вызывающий (static или член), ядро память не выделяет никогда. World хранит
//   указатель на GameData — тот должен пережить World (см. data_api.h). Копия World через
//   memcpy — полноценный снимок (balance-runner может так ветвить прогоны).
//
// ЧТЕНИЕ И ИЗМЕНЕНИЕ
//   Поля World публичны для чтения. Менять их можно только через cmd_* и tick — иначе
//   нарушаются инварианты (очередь, занятость ПК, статистика). Блок priv — внутреннее
//   состояние T022/T023 (core/world_private.h), снаружи не трогать.
//
// ВРЕМЯ (GDD §4)
//   Тик = 1 игровая минута. Ядро не знает реального времени: вызывающий накапливает
//   реальные секунды и делает minutes_per_real_second(world) тиков в секунду.
//   День: 8:00 утро (закрыто) → 9:00 открытие → 24:00 закрытие (фаза kClosed, тики
//   ничего не делают) → cmd_end_day → ночь 0:00–8:00 (если куплен ночной тариф)
//   или сразу 8:00 следующего дня.
//
// ПРИМЕР (главный цикл игры)
//
//     static core::World world;
//     core::world_init(world, data, seed);
//     float acc = 0.0f;
//     while (platform::poll()) {
//         acc += dt * core::minutes_per_real_second(world);
//         while (acc >= 1.0f) { core::tick(world); acc -= 1.0f; }
//         core::Notice notices[16];
//         const int n = core::take_notices(world, notices, 16);
//         for (int i = 0; i < n; ++i) { /* звук, всплывашка, анимация */ }
//         if (world.clock.phase == core::Phase::kClosed) { /* итоги дня → cmd_end_day */ }
//     }
//
// ПРИМЕР (balance-runner)
//
//     core::world_init(world, data, seed);
//     while (world.clock.day_index <= 90 && world.clock.phase != core::Phase::kGameOver) {
//         if (world.clock.phase == core::Phase::kClosed) { write_csv(world.stats.days[...]); core::cmd_end_day(world); }
//         bot_act(world);   // cmd_serve для очереди, cmd_reboot для зависших, закупки утром
//         core::tick(world);
//     }
#include <cstdint>

#include "core/data_api.h"
#include "core/data_types.h"
#include "core/rng_api.h"
#include "core/world_private.h"

namespace core {

// ---------------------------------------------------------------------------
// Лимиты мира

constexpr int kMaxPcs = 16;              // GDD §5.3: до 16 мест
constexpr int kMaxVisitors = 32;         // одновременно в клубе, включая очередь
constexpr int kMaxQueue = 8;             // очередь у стойки; полная — новые не входят
constexpr int kMaxOrders = 32;           // заказов, ждущих доставки
constexpr int kMaxModifiers = 32;        // активных модификаторов событий и рекламы
constexpr int kMaxActiveEvents = 16;     // событий, ждущих ответа игрока
constexpr int kMaxMail = 64;             // писем в ящике; новое вытесняет самое старое прочитанное
constexpr int kMaxScheduled = 32;        // запланированных событий
constexpr int kMaxNotices = 64;          // кольцевой буфер уведомлений
constexpr int kStatsDays = 30;           // история итогов дня (графики Бухгалтерии)
constexpr int kRecordTop = 10;           // топ-10 на мини-игру (§7.11)
constexpr int kGameBitsetWords = kMaxGames / 32;
constexpr int kFlagBitsetWords = kMaxFlags / 32;
constexpr int kUpgradeBitsetWords = kMaxUpgrades / 32;

constexpr int kMinutesPerDay = 24 * 60;
constexpr int kMorningMinute = 8 * 60;   // 8:00 — утро, клуб закрыт
constexpr int kOpenMinute = 9 * 60;      // 9:00 — открытие
// Закрытие — 24:00 (minute == kMinutesPerDay не бывает: в этот момент фаза kClosed, minute = 0).

// Переходы посетителя занимают фиксированное число игровых минут: рендер интерполирует
// движение по state_minutes / kTransitMinutes.
constexpr int kTransitMinutes = 2;

constexpr uint8_t kNoPc = 0xFF;
constexpr uint8_t kNoVisitor = 0xFF;
constexpr uint16_t kRecordPlayer = 0xFFFE;  // RecordEntry::who — рекорд игрока

// ---------------------------------------------------------------------------
// Время

enum class Phase : uint8_t {
    kMorning,   // 8:00–9:00: закрыто, доставка, почта, сохранение разрешено
    kOpen,      // 9:00–24:00: посетители
    kClosed,    // 24:00: итоги дня; тики стоят, ждём cmd_end_day; сохранение разрешено
    kNight,     // 0:00–8:00 при ночном тарифе, ускорение ×8
    kGameOver,  // клуб закрыт за долги (GDD §5.4); тики стоят
};

struct Clock {
    DayNum date = 0;          // текущая дата
    int32_t day_index = 1;    // номер игрового дня, день старта = 1 (переменная событий `day`)
    uint16_t minute = 0;      // минута суток 0..1439
    uint8_t weekday = 0;      // 0 пн … 6 вс
    Phase phase = Phase::kMorning;
    uint8_t speed = 1;        // выбранное ускорение 1/2/4 (cmd_set_speed)
    uint8_t holidays = 0;     // 1 — сегодня каникулы (период с тегом holidays)
    uint8_t session = 0;      // 1 — сегодня сессия
    uint8_t holiday = 0;      // 1 — праздничный день (calendar.holidays)
};

// ---------------------------------------------------------------------------
// Компьютеры (GDD §6.2)

enum class PcState : uint8_t {
    kEmpty,   // место без ПК или не хватает обязательного компонента (cpu…keyboard)
    kOff,     // выключен игроком
    kIdle,    // работает, свободен
    kBusy,    // занят посетителем (Pc::visitor)
    kHung,    // завис — нужна перезагрузка (§7.7); посетитель сидит и злится
    kBroken,  // сломан компонент (wear ≥ 100 или поломка) — ремонт или замена
};

struct Pc {
    Index component[kComponentCount];   // индекс HARD или kNone; headphones и chair — не обязательны
    float wear[kComponentCount];        // 0..100 (§7.7)
    float dust = 0.0f;                  // 0..100
    PcState state = PcState::kEmpty;
    uint8_t virus = 0;                  // 0/1 (§7.8, вне среза)
    uint8_t visitor = kNoVisitor;       // индекс в World::visitors, если kBusy/kHung
    uint8_t broken_component = 0xFF;    // Component при kBroken
    Index running_game = kNone;         // что запущено сейчас
    uint16_t load_minutes_today = 0;    // минут нагрузки за день (электричество)
    uint32_t installed[kGameBitsetWords];         // бит g — игра установлена
    uint32_t installed_pirate[kGameBitsetWords];  // бит g — установка с пиратского диска
};

// ---------------------------------------------------------------------------
// Посетители (GDD §6.1)

enum class VisitorState : uint8_t {
    kNone,       // слот свободен
    kEntering,   // входит (kTransitMinutes)
    kToDesk,     // идёт к стойке (kTransitMinutes)
    kWaiting,    // в очереди у стойки, тратит терпение; первый в queue — у самой стойки
    kToSeat,     // идёт к месту (kTransitMinutes)
    kPlaying,    // играет; может иметь просьбу (request)
    kLeaving,    // уходит (kTransitMinutes), затем слот освобождается
};

enum class LeaveReason : uint8_t {
    kNone,
    kDone,          // время вышло, не продлил
    kNoMoney,       // не хватило даже на час (plan_hours = 0)
    kNoGame,        // нет нужной игры, альтернатива не устроила (§7.3.1)
    kPrice,         // не согласился с ценой (§7.3.2)
    kPatience,      // кончилось терпение в очереди
    kUnhappy,       // sat < 20 (§7.5)
    kQueueFull,     // очередь полна — развернулся у входа
    kClosing,       // клуб закрылся
    kEvent,         // увёл эффект события («мама пришла»)
};

enum class Request : uint8_t {
    kNone,
    kHang,     // ПК завис — решается cmd_reboot
    kBroken,   // ПК сломался — решается ремонтом/заменой или пересадкой
    kFood,     // хочет товар request_arg (GOOD) — cmd_resolve_request (продажа)
    kGame,     // просит поставить игру request_arg — решается cmd_install на его ПК
    kHelp,     // «как тут…» — cmd_resolve_request
};

struct Visitor {
    VisitorState state = VisitorState::kNone;
    LeaveReason leave_reason = LeaveReason::kNone;
    Request request = Request::kNone;
    uint8_t pc = kNoPc;               // место (kToSeat, kPlaying)
    uint16_t serial = 0;              // уникальный номер визита: рендер сопоставляет модели
    Index archetype = kNone;
    Index regular = kNone;            // постоянный клиент или kNone
    uint8_t model = 0;                // индекс в ArchetypeRec::models
    uint8_t plan_hours = 0;           // T_plan (§7.2)
    uint16_t state_minutes = 0;       // минут в текущем состоянии
    uint16_t patience = 0;            // осталось терпения, минут (§7.2)
    uint16_t paid_minutes_left = 0;   // оплаченного времени осталось
    uint16_t request_minutes = 0;     // сколько висит текущая просьба
    Index want_game = kNone;          // g* (§7.2)
    Index game = kNone;               // во что играет на самом деле
    Index request_arg = kNone;        // товар или игра просьбы
    int32_t money = 0;                // осталось денег
    int32_t paid_total = 0;           // всего заплатил за визит
    float skill = 0.0f;               // 0..1
    float sat = 0.0f;                 // 0..100 (§7.5)
};

// ---------------------------------------------------------------------------
// Экономика и статистика

enum Expense : uint8_t {
    kExpRent,
    kExpInternet,
    kExpElectricity,
    kExpTax,
    kExpWages,
    kExpLoan,        // проценты по займу
    kExpPurchases,   // заказы и апгрейды
    kExpEvents,      // эффекты money < 0
    kExpenseCount
};

enum Revenue : uint8_t {
    kRevHours,       // оплата часов и продлений
    kRevNight,       // ночные пакеты
    kRevGoods,       // буфет
    kRevSales,       // продажа б/у
    kRevEvents,      // эффекты money > 0
    kRevenueCount
};

// Итоги одного дня (Бухгалтерия, балансовые прогоны).
struct DayStats {
    DayNum date = 0;
    int32_t revenue[kRevenueCount];
    int32_t expenses[kExpenseCount];
    int32_t money_end = 0;           // деньги на момент закрытия
    int32_t debt_end = 0;
    float rep_end = 0.0f;
    float sat_sum = 0.0f;            // Σ sat ушедших обслуженных — для средней
    uint16_t arrived = 0;            // вошли в клуб
    uint16_t served = 0;             // сели играть
    uint16_t extended = 0;           // продлений
    uint16_t left[10];               // по LeaveReason (индекс = значение)
    uint16_t load_minutes = 0;       // Σ минут нагрузки всех ПК
    uint16_t pc_hangs = 0;
    uint16_t pc_failures = 0;
};
static_assert(static_cast<int>(LeaveReason::kEvent) < 10, "DayStats::left");

struct Stats {
    DayStats today;                  // текущий день, обнуляется в cmd_end_day
    DayStats days[kStatsDays];       // кольцо: days[(head − 1 − i) mod 30] — i дней назад
    uint8_t head = 0;                // куда писать следующий
    uint8_t count = 0;               // сколько дней заполнено (≤ 30)
    int32_t month_revenue = 0;       // выручка с 1-го числа (налог §7.14)
    uint32_t visits_archetype[kMaxArchetypes];  // всего визитов по архетипам (переменная событий)
};

struct Economy {
    int32_t money = 0;
    int32_t debt = 0;                // займ «у знакомого» (§5.4), ≥ 0
    uint16_t debt_over_limit_days = 0;  // подряд дней с debt > debt_limit
    float rep = 0.0f;                // 0..100 (§7.15)
    int32_t price_hour = 0;          // ₽/ч
    int32_t price_night = 0;         // ₽ за ночной пакет
    int32_t goods_price[kMaxGoods];  // цена продажи по товарам
    float online_minutes_today = 0;  // минут онлайн-игр (диал-ап)
};

// ---------------------------------------------------------------------------
// Склад, библиотека, заказы, апгрейды

enum class OrderKind : uint8_t {
    kHardware,  // ref — HARD
    kGoods,     // ref — GOOD
    kLicense,   // ref — GAME, count лицензий
    kPirate,    // ref — GAME, диск на все места (count = 1)
};

struct Order {
    OrderKind kind = OrderKind::kHardware;
    uint8_t pad = 0;
    Index ref = kNone;
    uint16_t count = 0;
    DayNum arrives = 0;  // доставка в 8:00 этого дня
};

struct Inventory {
    uint16_t hardware[kMaxHardware];  // новые детали на складе
    uint16_t goods[kMaxGoods];        // товары буфета
    Order orders[kMaxOrders];
    uint8_t order_count = 0;
};

struct Library {
    uint8_t licenses[kMaxGames];            // куплено лицензий (мест) по играм
    uint32_t pirate[kGameBitsetWords];      // бит g — есть пиратский диск
};

struct Club {
    uint8_t seats = 0;                      // мест в зале (из апгрейда kUpgHall)
    uint8_t internet_level = 0;             // UpgradeRec::level текущего internet
    uint8_t network_level = 0;
    uint8_t pad = 0;
    uint32_t upgrades[kUpgradeBitsetWords]; // бит u — апгрейд куплен и действует
};

// ---------------------------------------------------------------------------
// Модификаторы, события, почта

struct Modifier {
    uint8_t target = 0;      // ModTarget; kModTargetCount — слот пуст
    uint8_t pad = 0;
    Index arg = kNone;       // архетип / игра / kNone
    float mult = 1.0f;
    DayNum until = 0;        // действует по этот день включительно
};

// Событие, показанное игроку и ждущее выбора варианта.
struct ActiveEvent {
    Index event = kNone;     // kNone — слот пуст
    uint8_t visitor = kNoVisitor;  // для present.kind = visitor — кто пришёл с репликой
    uint8_t mail = 0xFF;     // для mail — индекс письма в Mailbox
    DayNum date = 0;         // когда показано
    uint16_t minute = 0;
};

enum class MailKind : uint8_t {
    kEvent,     // письмо-событие; ref — событие (EVNT); ждущий ответа ActiveEvent — тот, у кого mail = индекс письма
    kReport,    // итоги вчерашнего дня (утром)
    kNewGames,  // «новинки недели» (понедельник): игры, вышедшие за неделю
    kBill,      // счёт 1-го числа; amount — сумма
    kDelivery,  // доставка пришла
};

struct Mail {
    MailKind kind = MailKind::kEvent;
    uint8_t read = 0;
    uint8_t answered = 0;     // для kEvent: 1 — выбор сделан
    uint8_t choice = 0xFF;    // какой вариант выбран
    Index ref = kNone;        // kEvent: событие (EVNT)
    uint16_t pad = 0;
    DayNum date = 0;
    int32_t amount = 0;
};

struct Mailbox {
    Mail mail[kMaxMail];
    uint8_t count = 0;        // письма 0..count−1, новые в конце
    uint8_t unread = 0;
};

// СИСТЕМА СОБЫТИЙ (GDD §8.6, T023)
//   Событие «срабатывает» — значит показывается игроку: занимает слот events.active,
//   для present.kind = mail ещё и письмо (MailKind::kEvent), уведомление kEventPresented.
//   Эффекты применяются только при ответе (cmd_event_choose).
//
//   Допуск (проверяется при каждой попытке сработать, для всех триггеров):
//     - once и уже было → нет; day_index < cooldown_until → нет;
//     - conditions выполнены (все через И);
//     - событие из event_pool постоянного клиента — только пока он в клубе; тогда
//       ActiveEvent::visitor — его слот (реплика у стойки, его модель);
//     - есть свободный слот events.active (нет — не срабатывает, кулдаун не ставится);
//     - бросок chance: p = clamp(base + Σ coef · var, 0, 1); при p ≥ 1 или p ≤ 0 ГПСЧ
//       не тратится.
//   Итог попытки: «сработало», «отказ» (once, conditions, бросок) или «ждёт» (кулдаун,
//   постоянного клиента нет в клубе, нет слота active).
//   Сработало → cooldown_until = day_index + cooldown_days, once помечается.
//
//   Триггеры. Час обрабатывается один раз — на первом тике внутри него (обычно тик,
//   после которого minute = ЧЧ:00; 8:00 утра, которое ставит cmd_end_day, — на тике 8:01). Часы, которые симуляция пропускает (0–7 без ночного тарифа), не наступают.
//     daily_roll — в trigger_hour; из всех, у кого выпал бросок, срабатывает одно —
//                  выбор по weight (не больше одного случайного события в час);
//     date       — в trigger_date, час trigger_hour или kEventDefaultHour;
//     on_state   — раз в час, пока условия выполняются: попытка, если с последнего
//                  перехода условий из «ложь» в «истина» ещё не было «сработало»/«отказа»
//                  («ждёт» — попробует через час);
//     on_action  — сразу, из места действия (команда или тик), все допущенные;
//     scheduled  — только из events.scheduled: в свой день и час или позже (час не
//                  наступил — на ближайшем обработанном). «Ждёт» — остаётся в очереди
//                  и пробует каждый час; «отказ» — снимается без показа.
//   Порядок внутри часа: scheduled → date → on_state → daily_roll; внутри — по индексу.
//
//   Эффекты (раскладка — data_pak.md §5): money < 0 — статья kExpEvents, при минусе —
//   займ (§5.4); > 0 — kRevEvents. modifier действует days дней, считая сегодняшний;
//   нет свободного слота — вытесняет истекающий раньше всех. spawn_visitor — только в
//   kOpen/kNight; постоянный клиент заодно открывается. schedule_event: hour 0xFF →
//   trigger_hour события → kEventDefaultHour; нет слота — пропадает. uninstall не трогает
//   игру, в которую сейчас играют. damage_pc ломает компонент (kBroken, ремонт возможен);
//   случайный выбор — среди собранных несломанных мест и стоящих в них компонентов.
//   unlock игры — +1 лицензия. start_minigame — уведомление kEventMinigame, запускает
//   её игра (game/). Все броски — World::rng, в порядке эффектов.
constexpr int kEventDefaultHour = 9;  // час для date/schedule_event без hour — открытие клуба

struct ScheduledEvent {
    Index event = kNone;      // kNone — слот пуст
    uint8_t hour = 0;
    uint8_t pad = 0;
    DayNum date = 0;
};

struct Events {
    ActiveEvent active[kMaxActiveEvents];
    ScheduledEvent scheduled[kMaxScheduled];
    uint16_t cooldown_until[kMaxEvents];   // день (day_index), до которого событие не срабатывает
    uint32_t fired_once[kMaxEvents / 32];  // бит — once-событие уже было
    uint32_t flags[kFlagBitsetWords];      // флаги событий
};

// ---------------------------------------------------------------------------
// Постоянные клиенты, рекорды, конкурент

struct RegularState {
    int8_t relation = 0;      // −100…100 (§6.1)
    uint8_t unlocked = 0;     // 1 — открыт и может приходить
    uint8_t in_club = 0;      // 1 — сейчас в клубе
    uint8_t pad = 0;
    uint16_t visits = 0;
    uint16_t pad2 = 0;
    float sat_sum = 0.0f;     // для средней sat
};

struct RecordEntry {
    int32_t score = 0;
    DayNum date = 0;
    uint16_t who = kNone;     // индекс REGU, kRecordPlayer или kNone (анонимный посетитель)
    Index archetype = kNone;  // для анонимного — его архетип (подпись на доске)
};

struct RecordBoard {
    RecordEntry top[kRecordTop];  // по убыванию качества (с учётом MinigameRec::invert)
    uint8_t count = 0;
};

// §7.10 — вне вертикального среза; пока active = 0 и F_comp = 1.
struct Competitor {
    uint8_t active = 0;
    float rep = 0.0f;
    float perf = 0.0f;
    float lib = 0.0f;
    int32_t price = 0;
};

// ---------------------------------------------------------------------------
// Уведомления: ядро → игра/UI. Кольцевой буфер; переполнение затирает самые старые
// и увеличивает Notices::dropped.

enum class NoticeKind : uint8_t {
    kVisitorEntered,   // visitor
    kVisitorAtDesk,    // visitor — встал первым в очередь, ждёт обслуживания
    kVisitorSeated,    // visitor, pc
    kVisitorLeft,      // visitor, a = LeaveReason
    kVisitorExtended,  // visitor, value = доплата
    kRequest,          // visitor, a = Request, ref = request_arg
    kRequestResolved,  // visitor, a = Request
    kPcHung,           // pc
    kPcBroken,         // pc, a = Component
    kEventPresented,   // ref = событие, a = индекс ActiveEvent
    kMailArrived,      // a = индекс письма
    kDelivery,         // заказы доставлены (8:00)
    kDayClosed,        // 24:00 — показать итоги дня (stats.today)
    kMonthPaid,        // 1-го числа списаны аренда/интернет/налог, value = сумма
    kDebtTaken,        // деньги ушли в минус → займ, value = долг
    kRecord,           // ref = мини-игра, a = место в топе (0 = первое)
    kRegularUnlocked,  // ref = постоянный клиент
    kGoodsOut,         // ref = товар
    kGameOver,         // клуб закрыт за долги
    kEventMinigame,    // эффект start_minigame: a = 0 — ref = MINI, a = 1 — ref = ServiceGame
};

struct Notice {
    NoticeKind kind = NoticeKind::kVisitorEntered;
    uint8_t a = 0;
    uint8_t visitor = kNoVisitor;
    uint8_t pc = kNoPc;
    Index ref = kNone;
    uint16_t pad = 0;
    int32_t value = 0;
};

struct Notices {
    Notice ring[kMaxNotices];
    uint8_t head = 0;      // индекс самого старого
    uint8_t count = 0;
    uint16_t dropped = 0;  // затёрто без чтения с начала игры
};

// ---------------------------------------------------------------------------
// Мир

struct World {
    const GameData* data = nullptr;
    Rng rng;
    Clock clock;
    Economy economy;
    Club club;
    Pc pcs[kMaxPcs];                         // 0..club.seats−1 — места зала
    Visitor visitors[kMaxVisitors];          // слоты; свободный — state == kNone
    uint8_t queue[kMaxQueue];                // индексы visitors в очереди, [0] — у стойки
    uint8_t queue_len = 0;
    uint16_t next_serial = 1;
    Inventory inventory;
    Library library;
    Modifier modifiers[kMaxModifiers];
    Events events;
    Mailbox mailbox;
    RegularState regulars[kMaxRegulars];
    RecordBoard records[kMaxMinigames];
    Competitor competitor;
    Stats stats;
    Notices notices;
    detail::WorldPrivate priv;               // внутреннее T022/T023
};

// ---------------------------------------------------------------------------
// Жизненный цикл

// Новая игра: обнуляет world, сид ГПСЧ, дата economy.start_date, 8:00, фаза kMorning.
// Из EconomyRec: деньги, репутация, цены, стартовые апгрейды (зал, интернет...),
// start_pcs собранных ПК из start_pc (износ и пыль 0, игр не установлено).
// Цены буфета — GoodsRec::sell_price. Постоянные клиенты без условий unlock открыты сразу.
void world_init(World& world, const GameData& data, uint32_t seed);

// Один тик = одна игровая минута. Порядок внутри тика (детерминирован):
//   1. время: minute += 1; смена фаз и дат (см. ниже);
//   2. посетители: появление (§7.1 для каждого архетипа), переходы, терпение в очереди
//      (§7.3.3), оплаченное время, продление (§7.5), уход; раз в час игры посетителя —
//      Δsat (§7.5), буфет (§7.13), просьбы;
//   3. ПК: раз в час нагрузки — пыль, износ, поломки, зависания (§7.7);
//   4. события (T023): on_state раз в час, daily_roll/date в свой час, scheduled;
//   5. уведомления.
// Переходы фаз:
//   8:00 kMorning — доставка заказов, письмо с итогами вчера, модификаторы с истёкшим until
//     снимаются; по понедельникам — письмо «новинки недели» и недельные проценты займа;
//   9:00 → kOpen;
//   24:00 → kClosed: оставшиеся посетители уходят (kClosing, без возврата денег), кроме
//     взявших ночной пакет — они остаются на kNight,
//     расходы дня (электричество, зарплаты), дрейф репутации (§7.15), итоги в stats.today,
//     уведомление kDayClosed. Дальше тики не идут до cmd_end_day.
//   kNight (ночной тариф): 0:00–8:00, затем kMorning.
// В kClosed и kGameOver ничего не делает. Без аллокаций.
void tick(World& world);

// Скорость: сколько тиков (игровых минут) делать за реальную секунду.
// kOpen/kMorning: 2 · effective_speed; kNight: 16 (×8); kClosed/kGameOver: 0.
float minutes_per_real_second(const World& world);

// Действующее ускорение: clock.speed, но 1, если в очереди кто-то есть (GDD §4).
int effective_speed(const World& world);

// Забрать до max уведомлений (от старых к новым) в out; возвращает число. Буфер очищается
// на прочитанное. Вызывать раз в кадр; balance-runner может не вызывать (кольцо перезапишется).
int take_notices(World& world, Notice* out, int max);

// ---------------------------------------------------------------------------
// Команды игрока. Возвращают kOk или причину отказа; при отказе мир не меняется.
// Деньги списываются сразу; если денег не хватает — kNoMoney (займ берётся только
// за обязательные расходы, не за покупки).

enum class CmdResult : uint8_t {
    kOk,
    kInvalid,      // индекс вне диапазона или объект не в том состоянии
    kWrongPhase,   // не в эту фазу дня
    kNoMoney,
    kNoStock,      // нет детали/товара на складе
    kNoLicense,    // нет свободной лицензии и пиратского диска
    kNoSpace,      // не хватает места на HDD (§7.4)
    kBusy,         // ПК занят посетителем
    kFailed,       // мини-игра провалена (quality = 0) — ничего не изменилось
    kLimit,        // кончились слоты (заказы, модификаторы)
};

// Ускорение 1, 2 или 4 (GDD §4). Иное → kInvalid. Действует с учётом effective_speed.
CmdResult cmd_set_speed(World& world, int speed);

// После показа итогов дня (фаза kClosed): архивирует stats.today, 1-го числа — аренда,
// интернет, налог (§7.14), on_action day_closed/month_started; переход в kNight (если куплен
// ночной тариф) или сразу на 8:00 следующего дня. Не в kClosed → kWrongPhase.
// Если деньги < 0 после расходов — разница уходит в долг (§5.4), долг > лимита 14 дней
// подряд → kGameOver.
CmdResult cmd_end_day(World& world);

// Цены (Бухгалтерия). ≤ 0 → kInvalid. Действуют на следующих обслуживаемых.
CmdResult cmd_set_price_hour(World& world, int32_t price);
CmdResult cmd_set_price_night(World& world, int32_t price);
CmdResult cmd_set_goods_price(World& world, Index goods, int32_t price);

enum class Tariff : uint8_t {
    kHourly,  // price_hour · T_plan
    kNight,   // price_night: сидит до 8:00; только при купленном ночном тарифе (kUpgNightTariff)
};

enum class ServeResult : uint8_t {
    kSeated,         // сел за ПК с нужной игрой, заплатил
    kSeatedAlt,      // согласился на альтернативу (sat −15), заплатил
    kLeftNoGame,     // нет нужной игры, альтернатива не устроила — ушёл (rep −0,2)
    kLeftPrice,      // не устроила цена — ушёл
    kWrongPc,        // нужная игра есть на другом свободном ПК — посетитель не сел; мир не меняется
    kPcUnavailable,  // ПК не kIdle или игра на нём не запускается (perf < 0,7); мир не меняется
    kInvalid,        // не первый в очереди / нет тарифа / не та фаза; мир не меняется
};

// Обслужить посетителя у стойки (первый в queue) на место pc. Порядок решения — GDD §7.3:
//   1. игра: g* на этом ПК с perf ≥ 0,7 → играет в g*. Иначе если g* есть на другом
//      свободном ПК → kWrongPc. Иначе лучшая по value_a установленная на этом ПК игра
//      с perf ≥ 0,7; бросок p_alt_a → kSeatedAlt или kLeftNoGame;
//   2. цена: бросок p_accept (§7.3.2) → дальше или kLeftPrice;
//   3. оплата вперёд, посетитель → kToSeat, ПК → kBusy.
// Броски тратят ГПСЧ только в шагах, которые реально выполняются.
ServeResult cmd_serve(World& world, uint8_t visitor, uint8_t pc, Tariff tariff);

// Решить просьбу посетителя у стойки или у его ПК:
//   kFood — продать товар request_arg по goods_price (нет на складе → kNoStock);
//   kHelp — просто решено. kHang/kBroken/kGame решаются своими командами → kInvalid.
// Решённая просьба даёт +5 к sat в этом часу (§7.5).
CmdResult cmd_resolve_request(World& world, uint8_t visitor);

// Обслуживание ПК. quality — результат мини-игры 0..1 (§6.5); 0 → kFailed.
// Перезагрузка: kHung → kBusy/kIdle, снимает просьбу kHang. Иначе kInvalid.
CmdResult cmd_reboot(World& world, uint8_t pc, float quality);
// Чистка: dust · (1 − quality). Занятый ПК → kBusy.
CmdResult cmd_clean_dust(World& world, uint8_t pc, float quality);
// Ремонт компонента мини-игрой: wear − (30 + 30·quality) (§7.7). wear ≥ 100 → kInvalid
// (только замена). Если компонент был причиной kBroken — ПК снова kIdle. Занятый → kBusy.
CmdResult cmd_repair(World& world, uint8_t pc, Component comp, float quality);
// Поставить деталь hardware со склада в слот её типа (wear = 0). Старая деталь выбрасывается
// (продать её — cmd_sell_component до замены). Пустое место + все обязательные компоненты →
// ПК собран (kIdle, on_action pc_bought). Занятый → kBusy, нет на складе → kNoStock,
// quality = 0 → kFailed (деталь остаётся на складе).
CmdResult cmd_replace_component(World& world, uint8_t pc, Index hardware, float quality);
// Снять деталь и продать б/у (§7.9). Обязательный компонент → ПК становится kEmpty.
CmdResult cmd_sell_component(World& world, uint8_t pc, Component comp);
// Удаление вируса (§7.8, вне среза): virus = 0.
CmdResult cmd_remove_virus(World& world, uint8_t pc, float quality);
// Включить/выключить ПК. Выключить занятый → kBusy.
CmdResult cmd_set_pc_power(World& world, uint8_t pc, bool on);

// Установить игру. pirate = false — тратит лицензию (licenses[g] больше числа лицензионных
// установок), true — нужен пиратский диск. Проверки: игра вышла, место на HDD (§7.4).
// Пиратская установка — on_action pirate_installed. Снимает просьбу kGame на этом ПК.
// Занятый ПК можно (установка «из-за плеча»), игра посетителя не прерывается.
// quality — мини-игра «Установка»; 0 → kFailed. Уже установлена → kInvalid.
CmdResult cmd_install(World& world, uint8_t pc, Index game, bool pirate, float quality);
// Удалить игру; освобождает лицензию. Если в неё сейчас играют → kBusy.
CmdResult cmd_uninstall(World& world, uint8_t pc, Index game);

// Заказ в Магазине: оплата сразу, доставка в 8:00 следующего дня (заказ в любую фазу,
// включая утро, приходит только завтра). Цена: железо — current_hardware_price,
// товары — buy_price, лицензии — price_license · count, пират — price_pirate (count = 1).
// Игра ещё не вышла → kInvalid.
CmdResult cmd_order(World& world, OrderKind kind, Index ref, uint16_t count);

// Купить апгрейд сразу: оплата price, requires_upgrade должен быть куплен. Апгрейд того же kind
// с меньшим level заменяется (зал 6 → 10). kUpgAd — модификатор на days дней.
CmdResult cmd_buy_upgrade(World& world, Index upgrade);

// Погасить часть долга: amount ≤ min(money, debt).
CmdResult cmd_repay_debt(World& world, int32_t amount);

// Ответить на событие: active — индекс в events.active, choice — вариант. Проверяет requires
// варианта (не выполнены → kInvalid), применяет эффекты по порядку, освобождает слот,
// помечает письмо (answered, choice, read). Пустой слот / нет варианта → kInvalid,
// kGameOver → kWrongPhase. Любая фаза, кроме kGameOver.
CmdResult cmd_event_choose(World& world, uint8_t active, uint8_t choice);

// Пометить письмо прочитанным.
CmdResult cmd_mail_read(World& world, uint8_t mail);

// Результат игрока в мини-игре на админском ПК. Попал в топ — kOk и kRecord;
// побит рекорд постоянного клиента — on_action record_beaten. Не попал — kOk без записи.
CmdResult cmd_submit_record(World& world, Index minigame, int32_t score);

// ---------------------------------------------------------------------------
// Запросы (const, без побочных эффектов, без ГПСЧ)

// Производительность ПК для игры с учётом перегрева и вируса (§7.4). Пустой ПК → 0.
float pc_perf(const World& world, uint8_t pc, Index game);
// Перегрев ПК сейчас (§7.7).
float pc_overheat(const World& world, uint8_t pc);
// Свободно на HDD, МБ.
int32_t pc_free_hdd_mb(const World& world, uint8_t pc);
bool pc_has_game(const World& world, uint8_t pc, Index game);
// Сколько ПК в состоянии kIdle/kBusy/kHung (переменная `pcs`).
int pcs_working(const World& world);

// Лучшее место для посетителя (место по умолчанию в Кассе, §9.2): свободный ПК с g* и
// наибольшим perf; нет — с лучшей альтернативой; нет — kNoPc.
uint8_t best_pc_for(const World& world, uint8_t visitor);

// Популярность игры сегодня с модификаторами pop:<game> (§7.6).
float game_popularity_today(const World& world, Index game);
bool game_released(const World& world, Index game);
// Привлекательность библиотеки для архетипа L_a (§7.6).
float library_appeal_for(const World& world, Index archetype);
// Ожидаемый поток архетипа в текущий час, посетителей/ч (§7.1).
float arrival_rate_now(const World& world, Index archetype);

// Текущая цена детали в Магазине с модификатором hw_price (§7.9).
int32_t current_hardware_price(const World& world, Index hardware);
// Сколько дадут за деталь в ПК (§7.9).
int32_t component_sell_price(const World& world, uint8_t pc, Component comp);

// Лицензий игры, не занятых установками.
int free_licenses(const World& world, Index game);
// Доля пиратских установок 0..1 (переменная `pirate_share`); установок нет → 0.
float pirate_share(const World& world);
int pirate_installs(const World& world);

bool upgrade_owned(const World& world, Index upgrade);
bool flag_set(const World& world, Index flag);

// Можно ли сейчас выбрать вариант choice события в слоте active (requires выполнены).
// UI показывает недоступные варианты серыми. Пустой слот / нет варианта → false.
bool event_choice_available(const World& world, uint8_t active, uint8_t choice);

// Итоги дня i дней назад (0 — вчера). Нет такого — nullptr.
const DayStats* stats_days_ago(const World& world, int days_ago);

// Можно ли сохраниться сейчас (§8.8): kMorning или kClosed.
bool can_save(const World& world);

}  // namespace core
