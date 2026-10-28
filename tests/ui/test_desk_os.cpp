// T033: стойка ↔ ОС на настоящем ui.cpp (заглушки render/platform/audio — в test_ui_flow.cpp).
// ○ в Кассе — рабочий стол, ○ на столе — выезд; L/R после входа через стойку.
#include <doctest/doctest.h>

#include "game/admin_desk.h"
#include "ui/ui_api.h"

namespace {

int g_cashier_frames = 0;
int g_cashier_opened = 0;

void cashier_open(ui::Context&, void*) { ++g_cashier_opened; }
void cashier_frame(ui::Context& ctx, void*) {
    ++g_cashier_frames;
    ui::button(ctx, 1, {4, 4, 60, 16}, "Посадить");
}
void network_frame(ui::Context& ctx, void*) { ui::button(ctx, 1, {4, 4, 60, 16}, "Сеть"); }

struct DeskFixture {
    ui::AppId cashier = ui::kNoApp, network = ui::kNoApp;
    game::AdminDesk desk;

    DeskFixture() {
        g_cashier_frames = 0;
        g_cashier_opened = 0;
        REQUIRE(ui::init({}));
        ui::AppDesc c{};
        c.id = "cashier";
        c.flags = ui::kAppOnDesktop | ui::kAppMaximized;  // как в src/ui/apps/cashier.cpp
        c.on_open = cashier_open;
        c.on_frame = cashier_frame;
        cashier = ui::register_app(c);
        ui::AppDesc n{};
        n.id = "network";
        n.flags = ui::kAppOnDesktop | ui::kAppMaximized;
        n.on_frame = network_frame;
        network = ui::register_app(n);
        REQUIRE(cashier != ui::kNoApp);
        REQUIRE(network != ui::kNoApp);
        desk.init(core::mat4_identity(), cashier);
    }
    ~DeskFixture() { ui::shutdown(); }

    // Кадр в порядке main: стойка, затем ui::update и ui::draw.
    void frame(uint32_t pressed = 0) {
        platform::InputState in;
        in.pressed = pressed;
        desk.update(1.0f / 30.0f, nullptr);
        ui::update(1.0f / 30.0f, in);
        ui::draw();
    }
    void run_until(game::AdminDesk::State state) {
        for (int i = 0; i < 120 && desk.state() != state; ++i) frame();
        REQUIRE(desk.state() == state);
    }
    void enter() {
        desk.enter(nullptr);
        CHECK(desk.state() == game::AdminDesk::State::kEntering);
        CHECK_FALSE(ui::active());
        run_until(game::AdminDesk::State::kOs);
        CHECK(ui::active());
        CHECK(ui::active_app() == cashier);
    }
};

}  // namespace

TEST_CASE_FIXTURE(DeskFixture, "стойка: ○ в Кассе — рабочий стол, ○ на столе — выезд из ОС") {
    using State = game::AdminDesk::State;
    enter();
    CHECK(g_cashier_opened == 1);

    frame(platform::kButtonCircle);
    CHECK(ui::active());
    CHECK_FALSE(ui::is_open(cashier));
    CHECK(ui::active_app() == ui::kNoApp);
    frame();
    CHECK(desk.state() == State::kOs);

    frame(platform::kButtonCircle);
    CHECK_FALSE(ui::active());
    frame();
    CHECK(desk.state() == State::kLeaving);
    CHECK(desk.camera(game::Player{}) != nullptr);
    run_until(State::kWorld);
    CHECK(desk.world_input());

    // Повторный вход — снова с Кассой.
    enter();
    CHECK(g_cashier_opened == 2);
}

TEST_CASE_FIXTURE(DeskFixture, "стойка: L/R после входа — рабочий стол виден, Касса не рисуется под ним") {
    enter();
    frame(platform::kButtonR);
    CHECK(ui::active_app() == ui::kNoApp);
    const int before = g_cashier_frames;
    frame();
    CHECK(g_cashier_frames == before);  // развёрнутая Касса спрятана, видны значки
    CHECK(ui::is_open(cashier));

    // Со стола — другая программа, L/R по кругу: стол → Касса → Сеть → стол.
    ui::open_app(network);
    frame();
    CHECK(ui::active_app() == network);
    frame(platform::kButtonR);
    CHECK(ui::active_app() == ui::kNoApp);
    frame(platform::kButtonR);
    CHECK(ui::active_app() == cashier);
    frame();
    CHECK(g_cashier_frames > before);
    frame(platform::kButtonR);
    CHECK(ui::active_app() == network);
    frame(platform::kButtonL);
    CHECK(ui::active_app() == cashier);

    // Выход не ломается: ○ закрывает окна по одному, затем стол — выезд.
    frame(platform::kButtonCircle);
    CHECK(ui::active_app() == network);
    frame(platform::kButtonCircle);
    CHECK(ui::active_app() == ui::kNoApp);
    frame(platform::kButtonCircle);
    CHECK_FALSE(ui::active());
    frame();
    CHECK(desk.state() == game::AdminDesk::State::kLeaving);
}
