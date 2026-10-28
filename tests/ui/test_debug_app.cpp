// DBG: программа «Отладка» на настоящих данных и ядре. Виджеты ui — заглушки: нажатие по id,
// текст собирается для проверок (как в test_cashier_network.cpp).
#include <doctest/doctest.h>

#include <cstdio>
#include <string>
#include <vector>

#include "core/core_api.h"
#include "core/data_api.h"
#include "ui/apps/debug.h"

namespace {

// Id виджетов (enum в debug.cpp).
enum : ui::WidgetId {
    kMoney1k = 1, kMoney10k, kToOpen, kToClose, kToMorning, kSpeed,
    kVisitor, kInstallAll, kPc, kBreak, kHang, kDust, kFix,
    kSave, kLoad, kErase, kEvent, kFire,
};

struct Input {
    ui::WidgetId click = 0;
    ui::WidgetId select_widget = 0;
    int select = -1;
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

struct FakeHost {
    int saves = 0, loads = 0, erases = 0;
    bool ok = true;
    int32_t loaded_day = 0;
} g_host;

bool fake_save(const core::World&) { ++g_host.saves; return g_host.ok; }
bool fake_load(core::World& w) {
    ++g_host.loads;
    if (g_host.ok) w.clock.day_index = g_host.loaded_day;
    return g_host.ok;
}
bool fake_erase() { ++g_host.erases; return g_host.ok; }
int fake_code() { return g_host.ok ? 0 : static_cast<int>(0x80110385u); }

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

struct Fixture {
    std::vector<uint8_t> pak;
    core::GameData data{};
    core::World world{};
    ui::AppDesc app{};

    explicit Fixture(bool with_host = true) {
        pak = read_pak();
        REQUIRE(!pak.empty());
        pak.resize((pak.size() + 15) & ~size_t(15));
        REQUIRE(core::data_load(pak.data(), pak.size(), &data) == core::DataError::kOk);
        g_data = &data;
        g_apps.clear();
        g_host = FakeHost{};
        ui::DebugHost host;
        if (with_host) {
            host.save = fake_save;
            host.load = fake_load;
            host.erase = fake_erase;
            host.last_code = fake_code;
        }
        REQUIRE(ui::debug_register(host) != ui::kNoApp);
        REQUIRE(g_apps.size() == 1);
        app = g_apps[0];
        core::world_init(world, data, 1);
        ui::Context ctx = context();
        g_missing.clear();
        app.on_open(ctx, app.user);
        CHECK(g_missing.empty());
        for (const std::string& key : g_missing) MESSAGE("нет строки: " << key);
    }

    ui::Context context() {
        ui::Context ctx{};
        ctx.client = {2, 16, ui::kMaxClientWidth, ui::kMaxClientHeight};
        ctx.is_active = true;
        ctx.world = &world;
        ctx.data = &data;
        return ctx;
    }

    void frame(const Input& in = Input{}) {
        g_in = in;
        g_texts.clear();
        g_disabled.clear();
        ui::Context ctx = context();
        app.on_frame(ctx, app.user);
    }
    void click(ui::WidgetId id) {
        Input in;
        in.click = id;
        in.focus = id;
        frame(in);
    }
    void select(ui::WidgetId id, int value) {
        Input in;
        in.select_widget = id;
        in.select = value;
        in.focus = id;
        frame(in);
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
void group(Context&, Rect r, const char* caption) {
    CHECK(r.x + r.w <= kMaxClientWidth); CHECK(r.y + r.h <= kMaxClientHeight);
    g_texts.push_back(caption);
}
bool is_focused(const Context& ctx, WidgetId id) { return ctx.is_active && g_in.focus == id; }
uint32_t pressed(const Context&) { return 0; }
bool button(Context& ctx, WidgetId id, Rect r, const char* text, uint8_t flags) {
    CHECK(r.x + r.w <= kMaxClientWidth); CHECK(r.y + r.h <= kMaxClientHeight);
    g_texts.push_back(text);
    if (flags & kWidgetDisabled) { g_disabled.push_back(id); return false; }
    return ctx.is_active && g_in.click == id;
}
bool choice(Context& ctx, WidgetId id, Rect r, int* index, int count, ItemTextFn text, void* user, uint8_t flags) {
    CHECK(r.x + r.w <= kMaxClientWidth); CHECK(r.y + r.h <= kMaxClientHeight);
    bool changed = false;
    if (ctx.is_active && !(flags & kWidgetDisabled) && g_in.select_widget == id && g_in.select >= 0 &&
        g_in.select < count) {
        changed = *index != g_in.select;
        *index = g_in.select;
    }
    char buf[96];
    g_texts.push_back(text(*index, buf, sizeof(buf), user));
    return changed;
}
}  // namespace ui

TEST_CASE("DBG: без мира не падает, все строки есть") {
    Fixture f;
    ui::Context ctx = f.context();
    ctx.world = nullptr;
    f.app.on_frame(ctx, f.app.user);
    f.frame();
    CHECK(has_text(ui::str("app.debug.save")));
}

TEST_CASE("DBG: кнопки меняют мир — деньги, время, игры, посетитель, ПК, событие") {
    Fixture f;
    core::World& w = f.world;
    const int32_t money = w.economy.money;
    f.click(kMoney1k);
    f.click(kMoney10k);
    CHECK(w.economy.money == money + 11000);

    f.click(kToOpen);
    CHECK(w.clock.phase == core::Phase::kOpen);
    f.click(kInstallAll);
    CHECK(core::pc_has_game(w, 0, 0));
    CHECK(has_text("Установлено игр:"));
    f.click(kVisitor);
    CHECK(has_text(ui::str("app.debug.result.ok")));

    f.select(kPc, 2);
    f.click(kHang);
    CHECK(w.pcs[2].state == core::PcState::kHung);
    f.click(kFix);
    CHECK(w.pcs[2].state == core::PcState::kIdle);
    f.click(kBreak);
    CHECK(w.pcs[2].state == core::PcState::kBroken);
    f.click(kDust);
    CHECK(w.pcs[2].dust >= 70.0f);
    CHECK(w.pcs[0].state == core::PcState::kIdle);  // остальные не тронуты

    const uint8_t mails = w.mailbox.count;
    f.select(kEvent, 1);
    f.click(kFire);
    CHECK(w.mailbox.count == mails + 1);
    CHECK(has_text(core::data_name(f.data, f.data.events[1].id)));

    f.click(kToClose);
    CHECK(w.clock.phase == core::Phase::kClosed);
    f.click(kToMorning);
    CHECK(w.clock.phase == core::Phase::kMorning);
    CHECK(w.clock.day_index == 2);
}

TEST_CASE("DBG: скорость ×1/×4/×16 видна главному циклу") {
    Fixture f;
    f.select(kSpeed, 0);
    CHECK(ui::debug_speed() == 1);
    f.select(kSpeed, 2);
    CHECK(ui::debug_speed() == 16);
    ui::Context ctx = f.context();
    f.app.on_open(ctx, f.app.user);  // переоткрыли окно — ускорение осталось
    CHECK(ui::debug_speed() == 16);
    f.select(kSpeed, 1);
    CHECK(ui::debug_speed() == 4);
    f.select(kSpeed, 0);
}

TEST_CASE("DBG: сохранение — только между кадрами, только утром; стирание со второго нажатия") {
    Fixture f;
    core::World& w = f.world;
    REQUIRE(w.clock.phase == core::Phase::kMorning);
    f.click(kSave);
    CHECK(g_host.saves == 0);              // в кадре не пишем
    CHECK(disabled(kSave) == false);
    f.frame();
    CHECK(disabled(kSave));                // пока ждёт — серая
    ui::debug_after_frame(&w);
    CHECK(g_host.saves == 1);
    f.frame();
    CHECK(has_text(ui::str("app.debug.status.saved")));

    f.click(kToOpen);
    f.click(kSave);
    ui::debug_after_frame(&w);
    CHECK(g_host.saves == 1);
    f.frame();
    CHECK(has_text(ui::str("app.debug.status.cant_save")));

    g_host.loaded_day = 5;
    f.click(kLoad);
    ui::debug_after_frame(&w);
    CHECK(g_host.loads == 1);
    CHECK(w.clock.day_index == 5);
    f.frame();
    CHECK(has_text("день 5"));

    f.click(kErase);
    ui::debug_after_frame(&w);
    CHECK(g_host.erases == 0);             // первое нажатие — только «Точно стереть?»
    Input focus;
    focus.focus = kErase;
    f.frame(focus);
    CHECK(has_text(ui::str("app.debug.erase_confirm")));
    f.click(kErase);
    ui::debug_after_frame(&w);
    CHECK(g_host.erases == 1);

    f.click(kErase);                       // взвели и ушли фокусом — снято
    f.frame();
    f.click(kErase);
    ui::debug_after_frame(&w);
    CHECK(g_host.erases == 1);

    g_host.ok = false;
    w.clock.phase = core::Phase::kMorning;
    f.click(kSave);
    ui::debug_after_frame(&w);
    f.frame();
    CHECK(has_text(ui::str("app.debug.status.not_saved")));
    CHECK(has_text("0x80110385"));
}

TEST_CASE("DBG: без носителя кнопки сохранения серые") {
    Fixture f(false);
    f.frame();
    CHECK(disabled(kSave));
    CHECK(disabled(kLoad));
    CHECK(disabled(kErase));
    ui::debug_after_frame(&f.world);
}
