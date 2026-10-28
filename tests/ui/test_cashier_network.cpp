// T031: Касса и Сеть на настоящих данных (build/<target>/data.pak) и настоящем ядре.
// Виджеты ui заменены заглушками: ввод задаётся по id, текст собирается для проверок.
#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/core_api.h"
#include "core/data_api.h"
#include "core/formulas_api.h"
#include "ui/apps/cashier.h"
#include "ui/apps/network.h"

namespace {

// Id виджетов программ (enum в cashier.cpp / network.cpp).
enum : ui::WidgetId { kTariff = 1, kSeats, kServe, kRefuse, kSessions };
enum : ui::WidgetId { kReboot = 1, kClean, kPower, kInstall, kRepair, kGame, kDoInstall, kBack, kTileBase = 100 };

struct Input {
    ui::WidgetId click = 0;
    ui::WidgetId select_widget = 0;
    int select = -1;
    bool activate = false;
    uint32_t pressed = 0;
    ui::WidgetId focus = 0;
};
Input g_in;
std::vector<std::string> g_texts;
std::vector<ui::WidgetId> g_disabled;
std::vector<std::string> g_missing;
std::vector<ui::AppDesc> g_apps;
const core::GameData* g_data = nullptr;

bool has_text(const std::string& text) {
    for (const std::string& t : g_texts) if (t.find(text) != std::string::npos) return true;
    return false;
}
bool disabled(ui::WidgetId id) {
    for (ui::WidgetId d : g_disabled) if (d == id) return true;
    return false;
}

// Мини-игры обслуживания: сразу «сыграны» с заданным качеством.
struct FakeRunner {
    bool have[core::kServiceGameCount] = {};
    int started = 0;
    core::ServiceGame game = core::kSvcReboot;
    float quality = 1.0f;
    bool aborted = false;
    bool pending = false;
} g_run;

bool fake_available(core::ServiceGame game) { return g_run.have[game]; }
bool fake_start(core::ServiceGame game, float, uint8_t, uint32_t) {
    ++g_run.started;
    g_run.game = game;
    g_run.pending = true;
    return true;
}
bool fake_finished(float* quality, bool* aborted) {
    if (!g_run.pending) return false;
    g_run.pending = false;
    *quality = g_run.quality;
    *aborted = g_run.aborted;
    return true;
}

std::vector<uint8_t> read_pak() {
    std::vector<uint8_t> bytes;
    std::FILE* f = std::fopen(PCCLUB_DATA_PAK, "rb");
    if (!f) return bytes;
    uint8_t buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
    std::fclose(f);
    return bytes;
}

struct Club {
    std::vector<uint8_t> pak;
    core::GameData data{};
    core::World world{};
    ui::AppDesc cashier{}, network{};

    Club() {
        pak = read_pak();
        REQUIRE(!pak.empty());
        pak.resize((pak.size() + 15) & ~size_t(15));
        REQUIRE(core::data_load(pak.data(), pak.size(), &data) == core::DataError::kOk);
        g_data = &data;
        g_apps.clear();
        g_run = FakeRunner{};
        for (bool& h : g_run.have) h = true;
        REQUIRE(ui::cashier_register() != ui::kNoApp);
        ui::ServiceRunner runner;
        runner.available = fake_available;
        runner.start = fake_start;
        runner.finished = fake_finished;
        REQUIRE(ui::network_register(runner) != ui::kNoApp);
        REQUIRE(g_apps.size() == 2);
        cashier = g_apps[0];
        network = g_apps[1];
    }

    ui::Context context() {
        ui::Context ctx{};
        ctx.client = {2, 16, ui::kMaxClientWidth, ui::kMaxClientHeight};
        ctx.is_active = true;
        ctx.world = &world;
        ctx.data = &data;
        return ctx;
    }

    // Новый мир и тики до первого посетителя у стойки, чья игра идёт на ПК 1 (место 0).
    void until_desk(uint32_t seed) {
        core::world_init(world, data, seed);
        for (int i = 0; i < 2 * core::kMinutesPerDay && world.queue_len == 0; ++i) {
            if (world.clock.phase == core::Phase::kClosed) core::cmd_end_day(world);
            core::tick(world);
        }
        REQUIRE(world.queue_len > 0);
        REQUIRE(world.visitors[world.queue[0]].state == core::VisitorState::kWaiting);
    }

    bool want_runs_on(uint8_t pc) const {
        const core::Visitor& v = world.visitors[world.queue[0]];
        return v.want_game != core::kNone && core::formula::perf_runs(core::pc_perf(world, pc, v.want_game));
    }

    void open(ui::AppDesc& app) {
        ui::Context ctx = context();
        g_missing.clear();
        app.on_open(ctx, app.user);
        CHECK(g_missing.empty());
        for (const std::string& key : g_missing) MESSAGE("нет строки: " << key);
    }

    void frame(ui::AppDesc& app, const Input& in = Input{}) {
        g_in = in;
        g_texts.clear();
        g_disabled.clear();
        ui::Context ctx = context();
        app.on_frame(ctx, app.user);
    }
};

}  // namespace

namespace ui {
AppId register_app(const AppDesc& desc) {
    g_apps.push_back(desc);
    return static_cast<AppId>(g_apps.size() - 1);
}
const char* str(const char* key) {
    const core::StrId id = g_data ? core::data_find_string(*g_data, key) : core::kNone;
    if (id != core::kNone) return core::data_string(*g_data, id);
    g_missing.push_back(key);
    return key;
}
void label(Context&, Rect r, const char* text, Color, Font, Align) {
    CHECK(r.x >= 0); CHECK(r.y >= 0);
    CHECK(r.x + r.w <= kMaxClientWidth); CHECK(r.y + r.h <= kMaxClientHeight);
    g_texts.push_back(text ? text : "");
}
void fill(Context&, Rect r, Color) {
    CHECK(r.w > 0); CHECK(r.h > 0);
    CHECK(r.x + r.w <= kMaxClientWidth); CHECK(r.y + r.h <= kMaxClientHeight);
}
void outline(Context&, Rect, Color) {}
void progress(Context&, Rect, float, Color, const char*) {}
void set_focus(Context&, WidgetId) {}
bool is_focused(const Context& ctx, WidgetId id) { return ctx.is_active && g_in.focus == id; }
uint32_t consume(Context& ctx, uint32_t mask) {
    const uint32_t taken = ctx.is_active ? g_in.pressed & mask : 0;
    g_in.pressed &= ~taken;
    return taken;
}
bool button(Context& ctx, WidgetId id, Rect, const char* text, uint8_t flags) {
    g_texts.push_back(text);
    if (flags & kWidgetDisabled) { g_disabled.push_back(id); return false; }
    return ctx.is_active && g_in.click == id;
}
bool hotspot(Context& ctx, WidgetId id, Rect, uint8_t) { return ctx.is_active && g_in.click == id; }
bool choice(Context& ctx, WidgetId id, Rect, int* index, int count, ItemTextFn text, void* user, uint8_t flags) {
    bool changed = false;
    if (ctx.is_active && !(flags & kWidgetDisabled) && g_in.select_widget == id && g_in.select >= 0 &&
        g_in.select < count) {
        changed = *index != g_in.select;
        *index = g_in.select;
    }
    if (flags & kWidgetDisabled) g_disabled.push_back(id);
    char buf[96];
    for (int i = 0; i < count; ++i) g_texts.push_back(text(i, buf, sizeof(buf), user));
    return changed;
}
ListEvent table(Context& ctx, WidgetId id, Rect, ListState& state, const Column*, int columns, int count,
                CellTextFn cell, void* user) {
    if (state.selected >= count) state.selected = static_cast<int16_t>(count - 1);
    if (ctx.is_active && g_in.select_widget == id && g_in.select >= 0 && g_in.select < count)
        state.selected = static_cast<int16_t>(g_in.select);
    for (int row = 0; row < count; ++row) {
        std::string line;
        for (int col = 0; col < columns; ++col) {
            char buf[96]{};
            line += cell(row, col, buf, sizeof(buf), user);
            line += '|';
        }
        g_texts.push_back(line);
    }
    return ctx.is_active && g_in.select_widget == id && g_in.activate ? ListEvent::kActivated : ListEvent::kNone;
}
ListEvent list(Context& ctx, WidgetId id, Rect r, ListState& state, int count, ItemTextFn text, void* user) {
    struct Adapter { ItemTextFn fn; void* user; } adapter{text, user};
    return table(ctx, id, r, state, nullptr, 1, count,
                 [](int row, int, char* buf, int cap, void* u) {
                     const auto& a = *static_cast<Adapter*>(u);
                     return a.fn(row, buf, cap, a.user);
                 }, &adapter);
}
}  // namespace ui

TEST_CASE("T031: без мира и без мест Касса и Сеть не падают") {
    Club c;
    c.open(c.cashier);
    c.open(c.network);
    g_in = Input{};
    ui::Context ctx = c.context();
    ctx.world = nullptr;
    c.cashier.on_frame(ctx, c.cashier.user);
    c.network.on_frame(ctx, c.network.user);
    core::world_init(c.world, c.data, 1);
    c.frame(c.cashier);
    CHECK(has_text(ui::str("ui.cashier.closed")));
    CHECK(disabled(kServe));
    CHECK(disabled(kRefuse));
}

TEST_CASE("T031: посетителя обслуживают из ОС — игра ставится в Сети, место и оплата в Кассе") {
    Club c;
    uint32_t seed = 1;
    for (;; ++seed) {
        REQUIRE(seed < 50);
        c.until_desk(seed);
        if (c.want_runs_on(0)) break;
    }
    const uint8_t vi = c.world.queue[0];
    const core::Index want = c.world.visitors[vi].want_game;
    REQUIRE(core::cmd_set_price_hour(c.world, 1) == core::CmdResult::kOk);  // p_accept = 1 (§7.3.2)
    c.world.library.licenses[want] = 1;                                      // лицензия «пришла утром»

    // Сеть: ПК 1 → «Установить игру…» → нужная игра → «Установить» → мини-игра → cmd_install.
    c.open(c.network);
    c.frame(c.network, {kTileBase + 0});
    c.frame(c.network, {kInstall});
    c.frame(c.network);
    // Варианты choice — вышедшие и не установленные игры по возрастанию индекса.
    int game_row = 0;
    for (int g = 0; g < want; ++g)
        if (core::game_released(c.world, static_cast<core::Index>(g))) ++game_row;
    Input pick;
    pick.select_widget = kGame;
    pick.select = game_row;
    c.frame(c.network, pick);
    CHECK(has_text(std::string(ui::str("ui.network.src_license")).substr(0, 8)));
    c.frame(c.network, {kDoInstall});
    REQUIRE(g_run.started == 1);
    CHECK(g_run.game == core::kSvcInstall);
    c.frame(c.network);  // мини-игра закончилась — результат применён
    REQUIRE(core::pc_has_game(c.world, 0, want));
    CHECK(core::free_licenses(c.world, want) == 0);

    // Касса: посетитель у стойки, место по умолчанию — ПК 1, × по месту — посадить.
    const int32_t money = c.world.economy.money;
    const int32_t revenue = c.world.stats.today.revenue[core::kRevHours];
    c.open(c.cashier);
    c.frame(c.cashier);
    CHECK(has_text(core::data_string(c.data, c.data.games[want].title)));
    CHECK(has_text(ui::str("ui.cashier.fit.want")));
    Input seat;
    seat.select_widget = kSeats;
    seat.select = 0;
    seat.activate = true;
    c.frame(c.cashier, seat);

    const core::Visitor& v = c.world.visitors[vi];
    CHECK(v.state == core::VisitorState::kToSeat);
    CHECK(v.pc == 0);
    CHECK(v.game == want);
    CHECK(c.world.pcs[0].state == core::PcState::kBusy);
    CHECK(c.world.queue_len == 0);
    REQUIRE(v.paid_total > 0);
    CHECK(c.world.economy.money == money + v.paid_total);
    CHECK(c.world.stats.today.revenue[core::kRevHours] == revenue + v.paid_total);
    CHECK(c.world.stats.today.served == 1);
    CHECK(has_text("ПК 1"));

    c.frame(c.cashier);
    CHECK(has_text(std::string("1|") + core::data_string(c.data, c.data.games[want].title)));
}

TEST_CASE("T031: □ в Кассе — отказ: посетитель уходит, причина kRefused") {
    Club c;
    c.until_desk(3);
    const uint8_t vi = c.world.queue[0];
    const uint8_t len = c.world.queue_len;
    const float rep = c.world.economy.rep;
    c.open(c.cashier);
    c.frame(c.cashier);
    Input in;
    in.pressed = platform::kButtonSquare;
    c.frame(c.cashier, in);
    CHECK(c.world.visitors[vi].state == core::VisitorState::kLeaving);
    CHECK(c.world.visitors[vi].leave_reason == core::LeaveReason::kRefused);
    CHECK(c.world.queue_len == len - 1);
    CHECK(c.world.stats.today.left[static_cast<int>(core::LeaveReason::kRefused)] == 1);
    CHECK(c.world.economy.rep <= rep);  // §7.3: как уход по терпению — только вниз
    CHECK(core::cmd_refuse(c.world, vi) == core::CmdResult::kInvalid);
}

TEST_CASE("T031: Касса подсказывает другой ПК, если игра посетителя стоит там") {
    Club c;
    uint32_t seed = 1;
    for (;; ++seed) {
        REQUIRE(seed < 50);
        c.until_desk(seed);
        if (c.want_runs_on(1) && c.world.club.seats >= 2) break;
    }
    const core::Index want = c.world.visitors[c.world.queue[0]].want_game;
    c.world.library.licenses[want] = 1;
    REQUIRE(core::cmd_install(c.world, 1, want, false, 1.0f) == core::CmdResult::kOk);
    c.open(c.cashier);
    c.frame(c.cashier);
    Input in;
    in.select_widget = kSeats;
    in.select = 0;
    c.frame(c.cashier, in);
    c.frame(c.cashier, {kServe});
    CHECK(c.world.queue_len > 0);
    CHECK(has_text("ПК 2"));
}

TEST_CASE("T031: Сеть — BSOD перезагружается мини-игрой, без мини-игры кнопка серая") {
    Club c;
    core::world_init(c.world, c.data, 1);
    c.world.pcs[0].state = core::PcState::kHung;
    c.open(c.network);
    c.frame(c.network);
    CHECK(has_text(ui::str("ui.network.tile.hung")));
    CHECK(!disabled(kReboot));

    g_run.have[core::kSvcReboot] = false;
    c.frame(c.network);
    CHECK(disabled(kReboot));
    g_run.have[core::kSvcReboot] = true;

    g_run.quality = 0.8f;
    c.frame(c.network, {kReboot});
    REQUIRE(g_run.started == 1);
    CHECK(g_run.game == core::kSvcReboot);
    c.frame(c.network);
    CHECK(c.world.pcs[0].state == core::PcState::kIdle);
    CHECK(has_text("80"));
}

TEST_CASE("T031: Сеть — питание, чистка, выход из мини-игры ничего не меняет") {
    Club c;
    core::world_init(c.world, c.data, 1);
    c.open(c.network);
    c.frame(c.network, {kTileBase + 1});
    c.frame(c.network, {kPower});
    CHECK(c.world.pcs[1].state == core::PcState::kOff);
    c.frame(c.network);
    CHECK(has_text(ui::str("ui.network.power_on")));
    c.frame(c.network, {kPower});
    CHECK(c.world.pcs[1].state == core::PcState::kIdle);

    c.world.pcs[1].dust = 60.0f;
    g_run.aborted = true;
    c.frame(c.network, {kClean});
    c.frame(c.network);
    CHECK(c.world.pcs[1].dust == doctest::Approx(60.0f));
    CHECK(has_text(ui::str("ui.network.aborted")));

    g_run.aborted = false;
    g_run.quality = 0.5f;
    c.frame(c.network, {kClean});
    c.frame(c.network);
    CHECK(c.world.pcs[1].dust == doctest::Approx(30.0f));

    // Сломанный ПК: ремонт — мини-игра svc_replace; нет её — кнопка серая.
    c.world.pcs[1].state = core::PcState::kBroken;
    c.world.pcs[1].broken_component = core::kCompHdd;
    g_run.have[core::kSvcReplace] = false;
    c.frame(c.network);
    CHECK(disabled(kRepair));
    CHECK(has_text(ui::str("ui.network.comp.hdd")));
}
