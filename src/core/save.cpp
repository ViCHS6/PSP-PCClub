// Сохранение мира (контракт — save_api.h, формат — docs/formats/save.md, GDD §8.8).
// Один список полей (io_world) для записи и чтения: Writer и Reader с одинаковыми методами.
#include "core/save_api.h"

#include <cstring>

namespace core {

namespace {

// Поля, которые пишет io_world. Изменилась структура → обнови io_world, save.md и kSaveVersion.
static_assert(sizeof(Clock) == 16, "Clock: обнови сохранение");
static_assert(sizeof(Economy) == 28 + 4 * kMaxGoods, "Economy: обнови сохранение");
static_assert(sizeof(Club) == 4 + 4 * kUpgradeBitsetWords, "Club: обнови сохранение");
static_assert(sizeof(Pc) == 72 + 8 * kGameBitsetWords, "Pc: обнови сохранение");
static_assert(sizeof(Order) == 12, "Order: обнови сохранение");
static_assert(sizeof(Library) == kMaxGames + 4 * kGameBitsetWords, "Library: обнови сохранение");
static_assert(sizeof(Modifier) == 12, "Modifier: обнови сохранение");
static_assert(sizeof(ActiveEvent) == 12, "ActiveEvent: обнови сохранение");
static_assert(sizeof(ScheduledEvent) == 8, "ScheduledEvent: обнови сохранение");
static_assert(sizeof(Mail) == 16, "Mail: обнови сохранение");
static_assert(sizeof(RegularState) == 12, "RegularState: обнови сохранение");
static_assert(sizeof(RecordEntry) == 12, "RecordEntry: обнови сохранение");
static_assert(sizeof(Competitor) == 20, "Competitor: обнови сохранение");
static_assert(sizeof(DayStats) == 108, "DayStats: обнови сохранение");
static_assert(sizeof(detail::WorldPrivate) ==
                  4 * 32 + 4 * 32 + 16 + 4 + 4 * kMaxRegulars + 4 * kMaxUpgrades + 8 +
                      4 * (kMaxEvents / 32) + 2 * 16 + 4 * kMaxGames + 4 * kMaxArchetypes,
              "WorldPrivate: обнови сохранение");

constexpr uint16_t kHeaderSize = 32;

// CRC-32 (полином zlib), как в data.cpp: только при сохранении и загрузке.
uint32_t crc32(const uint8_t* p, size_t n) {
    static constexpr uint32_t kNibble[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u,
        0x4DB26158u, 0x5005713Cu, 0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
        0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c ^= p[i];
        c = (c >> 4) ^ kNibble[c & 15u];
        c = (c >> 4) ^ kNibble[c & 15u];
    }
    return c ^ 0xFFFFFFFFu;
}

uint32_t get_le(const uint8_t* p, int n) {
    uint32_t v = 0;
    for (int i = n - 1; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

void put_le(uint8_t* p, uint32_t v, int n) {
    for (int i = 0; i < n; ++i, v >>= 8) p[i] = static_cast<uint8_t>(v);
}

// --- Таблицы data.pak для секции DATA (save.md §3)

enum Table : uint8_t { kTGames, kTArch, kTRegu, kTHard, kTGoods, kTUpgr, kTMini, kTEvents, kTFlags, kTableCount };

uint16_t table_count(const GameData& d, int t) {
    switch (t) {
        case kTGames: return d.game_count;
        case kTArch: return d.archetype_count;
        case kTRegu: return d.regular_count;
        case kTHard: return d.hardware_count;
        case kTGoods: return d.goods_count;
        case kTUpgr: return d.upgrade_count;
        case kTMini: return d.minigame_count;
        case kTEvents: return d.event_count;
        default: return d.flag_count;
    }
}

NameId table_id(const GameData& d, int t, int i) {
    switch (t) {
        case kTGames: return d.games[i].id;
        case kTArch: return d.archetypes[i].id;
        case kTRegu: return d.regulars[i].id;
        case kTHard: return d.hardware[i].id;
        case kTGoods: return d.goods[i].id;
        case kTUpgr: return d.upgrades[i].id;
        case kTMini: return d.minigames[i].id;
        case kTEvents: return d.events[i].id;
        default: return d.flags[i];
    }
}

// FNV-1a по id первых n записей, каждый с завершающим нулём.
uint32_t table_hash(const GameData& d, int t, int n) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; ++i) {
        const char* s = data_name(d, table_id(d, t, i));
        do {
            h ^= static_cast<uint8_t>(*s);
            h *= 16777619u;
        } while (*s++);
    }
    return h;
}

// --- Запись и чтение. Ошибка «залипает»: дальнейшие операции ничего не делают.

class Writer {
public:
    static constexpr bool kReading = false;

    Writer(uint8_t* dst, size_t capacity) : p_(dst), cap_(capacity) {}

    void u8(uint8_t& v) { put(v, 1); }
    void i8(int8_t& v) { put(static_cast<uint8_t>(v), 1); }
    void u16(uint16_t& v) { put(v, 2); }
    void u32(uint32_t& v) { put(v, 4); }
    void i32(int32_t& v) { put(static_cast<uint32_t>(v), 4); }
    void f32(float& v) {
        uint32_t bits;
        std::memcpy(&bits, &v, 4);
        put(bits, 4);
    }
    void index(uint16_t& v, int) { u16(v); }
    void check(bool) {}
    void fail(SaveError) {}

    void begin(uint32_t tag) {
        put(tag, 4);
        section_ = pos_;
        put(0, 4);
    }
    void end() {
        if (err_ == SaveError::kOk) put_le(p_ + section_, static_cast<uint32_t>(pos_ - section_ - 4), 4);
    }

    SaveError error() const { return err_; }
    size_t pos() const { return pos_; }
    uint8_t* data() { return p_; }

private:
    void put(uint32_t v, int n) {
        if (err_ != SaveError::kOk) return;
        if (pos_ + n > cap_) {
            err_ = SaveError::kNoSpace;
            return;
        }
        put_le(p_ + pos_, v, n);
        pos_ += n;
    }

    uint8_t* p_;
    size_t cap_;
    size_t pos_ = 0;
    size_t section_ = 0;
    SaveError err_ = SaveError::kOk;
};

class Reader {
public:
    static constexpr bool kReading = true;

    Reader(const uint8_t* src, size_t size, size_t pos) : p_(src), size_(size), pos_(pos), end_(pos) {}

    void u8(uint8_t& v) { v = static_cast<uint8_t>(get(1)); }
    void i8(int8_t& v) { v = static_cast<int8_t>(get(1)); }
    void u16(uint16_t& v) { v = static_cast<uint16_t>(get(2)); }
    void u32(uint32_t& v) { v = get(4); }
    void i32(int32_t& v) { v = static_cast<int32_t>(get(4)); }
    void f32(float& v) {
        const uint32_t bits = get(4);
        std::memcpy(&v, &bits, 4);
    }
    // Индекс записи таблицы с count записями или kNone.
    void index(uint16_t& v, int count) {
        u16(v);
        check(v == kNone || v < count);
    }
    void check(bool ok) {
        if (!ok) fail(SaveError::kBadValue);
    }
    void fail(SaveError e) {
        if (err_ == SaveError::kOk) err_ = e;
    }

    void begin(uint32_t tag) {
        end_ = pos_ + 8 <= size_ ? pos_ + 8 : size_;  // заголовок секции — в границах файла
        const uint32_t t = get(4);
        const uint32_t size = get(4);
        if (err_ != SaveError::kOk) return;
        if (t != tag || size > size_ - pos_) {
            fail(SaveError::kBadSection);
            return;
        }
        end_ = pos_ + size;
    }
    void end() {
        if (pos_ != end_) fail(SaveError::kBadSection);
    }

    SaveError error() const { return err_; }
    size_t pos() const { return pos_; }

private:
    uint32_t get(int n) {
        if (err_ != SaveError::kOk) return 0;
        if (pos_ + n > end_) {
            fail(SaveError::kBadSection);
            return 0;
        }
        const uint32_t v = get_le(p_ + pos_, n);
        pos_ += n;
        return v;
    }

    const uint8_t* p_;
    size_t size_;
    size_t pos_;
    size_t end_;
    SaveError err_ = SaveError::kOk;
};

// Перечисление в u8 с проверкой верхней границы при чтении.
template <class Io, class E>
void enum8(Io& io, E& e, int max) {
    uint8_t v = static_cast<uint8_t>(e);
    io.u8(v);
    io.check(v <= max);
    e = static_cast<E>(v);
}

template <class Io>
void words(Io& io, uint32_t* w, int n) {
    for (int i = 0; i < n; ++i) io.u32(w[i]);
}

template <class Io>
void io_day_stats(Io& io, DayStats& s) {
    io.i32(s.date);
    for (int i = 0; i < kRevenueCount; ++i) io.i32(s.revenue[i]);
    for (int i = 0; i < kExpenseCount; ++i) io.i32(s.expenses[i]);
    io.i32(s.money_end);
    io.i32(s.debt_end);
    io.f32(s.rep_end);
    io.f32(s.sat_sum);
    io.u16(s.arrived);
    io.u16(s.served);
    io.u16(s.extended);
    for (int i = 0; i < kLeaveReasonCount; ++i) io.u16(s.left[i]);
    io.u16(s.load_minutes);
    io.u16(s.pc_hangs);
    io.u16(s.pc_failures);
}

// Все секции save.md §4 по порядку. Writer только читает поля w (save_world передаёт
// const World через const_cast), Reader пишет в черновик.
template <class Io>
void io_world(Io& io, World& w, const GameData& d) {
    // DATA: n[t] — сколько записей таблицы в сохранении.
    uint16_t n[kTableCount];
    io.begin(fourcc('D', 'A', 'T', 'A'));
    for (int t = 0; t < kTableCount; ++t) {
        n[t] = Io::kReading ? 0 : table_count(d, t);
        uint32_t hash = Io::kReading ? 0 : table_hash(d, t, n[t]);
        io.u16(n[t]);
        io.u32(hash);
        if (Io::kReading && io.error() == SaveError::kOk &&
            (n[t] > table_count(d, t) || table_hash(d, t, n[t]) != hash)) {
            io.fail(SaveError::kDataMismatch);
        }
    }
    io.end();
    if (io.error() != SaveError::kOk) return;
    const int ng = n[kTGames], na = n[kTArch], nr = n[kTRegu], nh = n[kTHard], no = n[kTGoods],
              nu = n[kTUpgr], nm = n[kTMini], ne = n[kTEvents];

    Clock& c = w.clock;
    io.begin(fourcc('C', 'L', 'C', 'K'));
    io.u8(c.weekday);
    io.check(c.weekday <= 6);
    io.u8(c.speed);
    io.check(c.speed == 1 || c.speed == 2 || c.speed == 4);
    io.u8(c.holidays);
    io.u8(c.session);
    io.u8(c.holiday);
    io.u16(w.next_serial);
    io.end();

    Economy& e = w.economy;
    io.begin(fourcc('E', 'C', 'O', 'N'));
    io.i32(e.money);
    io.i32(e.debt);
    io.u16(e.debt_over_limit_days);
    io.f32(e.rep);
    io.i32(e.price_hour);
    io.i32(e.price_night);
    io.f32(e.online_minutes_today);
    for (int i = 0; i < no; ++i) io.i32(e.goods_price[i]);
    io.end();

    io.begin(fourcc('C', 'L', 'U', 'B'));
    io.u8(w.club.seats);
    io.check(w.club.seats <= kMaxPcs);
    io.u8(w.club.internet_level);
    io.u8(w.club.network_level);
    words(io, w.club.upgrades, kUpgradeBitsetWords);
    io.end();

    io.begin(fourcc('P', 'C', 'S', ' '));
    uint8_t pc_count = kMaxPcs;
    io.u8(pc_count);
    io.check(pc_count == kMaxPcs);
    for (int i = 0; i < kMaxPcs && io.error() == SaveError::kOk; ++i) {
        Pc& p = w.pcs[i];
        for (int k = 0; k < kComponentCount; ++k) io.index(p.component[k], nh);
        for (int k = 0; k < kComponentCount; ++k) io.f32(p.wear[k]);
        io.f32(p.dust);
        // Посетители уходят при сохранении: занятый ПК пишется свободным (save.md §1).
        PcState state = !Io::kReading && p.state == PcState::kBusy ? PcState::kIdle : p.state;
        enum8(io, state, static_cast<int>(PcState::kBroken));
        io.check(state != PcState::kBusy);
        p.state = state;
        io.u8(p.virus);
        io.u8(p.broken_component);
        io.check(p.broken_component < kComponentCount || p.broken_component == 0xFF);
        io.u16(p.load_minutes_today);
        words(io, p.installed, kGameBitsetWords);
        words(io, p.installed_pirate, kGameBitsetWords);
        io.u8(w.priv.pc_load_min[i]);
    }
    io.end();

    Inventory& inv = w.inventory;
    io.begin(fourcc('I', 'N', 'V', 'T'));
    for (int i = 0; i < nh; ++i) io.u16(inv.hardware[i]);
    for (int i = 0; i < no; ++i) io.u16(inv.goods[i]);
    io.u8(inv.order_count);
    io.check(inv.order_count <= kMaxOrders);
    for (int i = 0; i < inv.order_count && io.error() == SaveError::kOk; ++i) {
        Order& o = inv.orders[i];
        enum8(io, o.kind, static_cast<int>(OrderKind::kPirate));
        io.index(o.ref, o.kind == OrderKind::kHardware ? nh : o.kind == OrderKind::kGoods ? no : ng);
        io.u16(o.count);
        io.i32(o.arrives);
    }
    io.end();

    io.begin(fourcc('L', 'I', 'B', 'R'));
    for (int i = 0; i < ng; ++i) io.u8(w.library.licenses[i]);
    words(io, w.library.pirate, kGameBitsetWords);
    io.end();

    io.begin(fourcc('R', 'E', 'G', 'U'));
    for (int i = 0; i < nr; ++i) {
        RegularState& r = w.regulars[i];
        io.i8(r.relation);
        io.u8(r.unlocked);
        io.u16(r.visits);
        io.f32(r.sat_sum);
        io.i32(w.priv.reg_last_visit[i]);
    }
    io.end();

    io.begin(fourcc('R', 'E', 'C', 'S'));
    for (int m = 0; m < nm && io.error() == SaveError::kOk; ++m) {
        RecordBoard& b = w.records[m];
        io.u8(b.count);
        io.check(b.count <= kRecordTop);
        for (int i = 0; i < b.count && io.error() == SaveError::kOk; ++i) {
            RecordEntry& r = b.top[i];
            io.i32(r.score);
            io.i32(r.date);
            io.u16(r.who);
            io.check(r.who < nr || r.who == kRecordPlayer || r.who == kNone);
            io.index(r.archetype, na);
        }
    }
    io.end();

    Events& ev = w.events;
    io.begin(fourcc('E', 'V', 'N', 'T'));
    for (int i = 0; i < ne; ++i) io.u16(ev.cooldown_until[i]);
    words(io, ev.fired_once, kMaxEvents / 32);
    words(io, ev.flags, kFlagBitsetWords);
    for (ScheduledEvent& s : ev.scheduled) {
        io.index(s.event, ne);
        io.u8(s.hour);
        io.i32(s.date);
    }
    for (ActiveEvent& a : ev.active) {
        io.index(a.event, ne);
        io.u8(a.mail);
        io.check(a.mail < kMaxMail || a.mail == 0xFF);
        io.i32(a.date);
        io.u16(a.minute);
    }
    io.i32(w.priv.ev_hour_date);
    io.u8(w.priv.ev_hour);
    words(io, w.priv.ev_state_done, kMaxEvents / 32);
    io.end();

    io.begin(fourcc('M', 'O', 'D', 'S'));
    for (Modifier& m : w.modifiers) {
        io.u8(m.target);
        io.check(m.target <= kModTargetCount);
        io.u16(m.arg);
        io.f32(m.mult);
        io.i32(m.until);
    }
    for (int i = 0; i < nu; ++i) io.i32(w.priv.ad_until[i]);
    io.end();

    Competitor& comp = w.competitor;
    io.begin(fourcc('C', 'O', 'M', 'P'));
    io.u8(comp.active);
    io.f32(comp.rep);
    io.f32(comp.perf);
    io.f32(comp.lib);
    io.i32(comp.price);
    io.end();

    Mailbox& mb = w.mailbox;
    io.begin(fourcc('M', 'A', 'I', 'L'));
    io.u8(mb.count);
    io.check(mb.count <= kMaxMail);
    io.u8(mb.unread);
    for (int i = 0; i < mb.count && io.error() == SaveError::kOk; ++i) {
        Mail& m = mb.mail[i];
        enum8(io, m.kind, static_cast<int>(MailKind::kDelivery));
        io.u8(m.read);
        io.u8(m.answered);
        io.u8(m.choice);
        if (m.kind == MailKind::kEvent) io.index(m.ref, ne); else io.u16(m.ref);
        io.i32(m.date);
        io.i32(m.amount);
    }
    io.end();

    Stats& st = w.stats;
    io.begin(fourcc('S', 'T', 'A', 'T'));
    io_day_stats(io, st.today);
    for (DayStats& s : st.days) io_day_stats(io, s);
    io.u8(st.head);
    io.check(st.head < kStatsDays);
    io.u8(st.count);
    io.check(st.count <= kStatsDays);
    io.i32(st.month_revenue);
    for (int i = 0; i < na; ++i) io.u32(st.visits_archetype[i]);
    io.end();

    io.begin(fourcc('D', 'A', 'Y', ' '));
    io.u16(w.priv.open_minutes_today);
    io.u8(w.priv.cache_valid);
    for (int i = 0; i < ng; ++i) io.f32(w.priv.pop_today[i]);
    for (int i = 0; i < na; ++i) io.f32(w.priv.lib_appeal[i]);
    io.end();
    // Таблицы дописаны после сохранения — кэш на них не рассчитан.
    if (Io::kReading && (ng < d.game_count || na < d.archetype_count)) w.priv.cache_valid = 0;
}

// Заголовок и CRC (save.md §2, §5).
SaveError check_header(const uint8_t* p, size_t size) {
    if (p == nullptr || size < kHeaderSize) return SaveError::kTooSmall;
    if (get_le(p, 4) != kSaveMagic) return SaveError::kBadMagic;
    if (get_le(p + 4, 2) != kSaveVersion) return SaveError::kBadVersion;
    if (get_le(p + 6, 2) != kHeaderSize || get_le(p + 8, 4) != size || size > kSaveMaxSize) {
        return SaveError::kBadSize;
    }
    if (crc32(p + 16, size - 16) != get_le(p + 12, 4)) return SaveError::kBadCrc;
    return SaveError::kOk;
}

// Черновик загрузки: мир меняется только после полной проверки. Статически — World велик
// для стека PSP, а ядро память не выделяет.
World g_scratch;

}  // namespace

const char* save_error_text(SaveError err) {
    switch (err) {
        case SaveError::kOk: return "ok";
        case SaveError::kWrongPhase: return "save allowed only in morning or at closing";
        case SaveError::kNoSpace: return "buffer too small";
        case SaveError::kTooSmall: return "file too small";
        case SaveError::kBadMagic: return "bad magic";
        case SaveError::kBadVersion: return "unsupported save version";
        case SaveError::kBadSize: return "file size mismatch";
        case SaveError::kBadCrc: return "crc mismatch";
        case SaveError::kBadSection: return "bad section";
        case SaveError::kDataMismatch: return "save made with different data.pak";
        case SaveError::kBadValue: return "value out of range";
    }
    return "unknown";
}

SaveError save_world(const World& world, void* dst, size_t capacity, size_t* out_size) {
    if (out_size) *out_size = 0;
    if (world.data == nullptr || !can_save(world)) return SaveError::kWrongPhase;
    if (dst == nullptr) return SaveError::kNoSpace;
    World& w = const_cast<World&>(world);  // Writer поля только читает

    Writer io(static_cast<uint8_t*>(dst), capacity < kSaveMaxSize ? capacity : kSaveMaxSize);
    uint32_t magic = kSaveMagic, size = 0, crc = 0;
    uint16_t version = kSaveVersion, header = kHeaderSize, minute = w.clock.minute;
    uint8_t phase = static_cast<uint8_t>(w.clock.phase), pad = 0;
    io.u32(magic);
    io.u16(version);
    io.u16(header);
    io.u32(size);
    io.u32(crc);
    io.u32(w.rng.state);
    io.i32(w.clock.date);
    io.u16(minute);
    io.u8(phase);
    io.u8(pad);
    io.i32(w.clock.day_index);
    io_world(io, w, *world.data);
    if (io.error() != SaveError::kOk) return io.error();

    uint8_t* p = io.data();
    put_le(p + 8, static_cast<uint32_t>(io.pos()), 4);
    put_le(p + 12, crc32(p + 16, io.pos() - 16), 4);
    if (out_size) *out_size = io.pos();
    return SaveError::kOk;
}

SaveError load_world(World& world, const GameData& data, const void* src, size_t size) {
    const uint8_t* p = static_cast<const uint8_t*>(src);
    const SaveError header = check_header(p, size);
    if (header != SaveError::kOk) return header;
    const uint8_t phase = p[26];
    if (phase != static_cast<uint8_t>(Phase::kMorning) && phase != static_cast<uint8_t>(Phase::kClosed)) {
        return SaveError::kBadValue;
    }
    const uint16_t minute = static_cast<uint16_t>(get_le(p + 24, 2));
    if (minute >= kMinutesPerDay) return SaveError::kBadValue;

    // Новый мир даёт значения по умолчанию всему, чего нет в файле (посетители, новые записи
    // таблиц); затем поверх — сохранённое.
    World& w = g_scratch;
    world_init(w, data, 1);
    w.rng.state = get_le(p + 16, 4);
    w.clock.date = static_cast<DayNum>(get_le(p + 20, 4));
    w.clock.minute = minute;
    w.clock.phase = static_cast<Phase>(phase);
    w.clock.day_index = static_cast<int32_t>(get_le(p + 28, 4));

    Reader io(p, size, kHeaderSize);
    io_world(io, w, data);
    if (io.error() == SaveError::kOk && io.pos() != size) io.fail(SaveError::kBadSection);
    if (io.error() != SaveError::kOk) return io.error();
    w.notices = Notices{};
    world = w;
    return SaveError::kOk;
}

SaveError save_peek(const void* src, size_t size, SaveInfo* out) {
    const uint8_t* p = static_cast<const uint8_t*>(src);
    const SaveError err = check_header(p, size);
    if (err != SaveError::kOk || out == nullptr) return err;
    out->date = static_cast<DayNum>(get_le(p + 20, 4));
    out->minute = static_cast<uint16_t>(get_le(p + 24, 2));
    out->phase = static_cast<Phase>(p[26]);
    out->day_index = static_cast<int32_t>(get_le(p + 28, 4));
    return SaveError::kOk;
}

}  // namespace core
