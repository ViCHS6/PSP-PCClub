// T010: InteractSystem — выбор цели (луч, конус, гистерезис, дистанция), колбэки, пул.
#include <doctest/doctest.h>

#include <cmath>
#include <string>

#include "game/interact.h"

using game::InteractAction;
using game::InteractDesc;
using game::InteractId;
using game::InteractSystem;
using game::kNoInteract;

namespace {

struct Calls {
    int primary = 0;
    int secondary = 0;
    InteractId last = kNoInteract;
};

void on_call(InteractId id, InteractAction action, void* user) {
    Calls& c = *static_cast<Calls*>(user);
    (action == InteractAction::kPrimary ? c.primary : c.secondary)++;
    c.last = id;
}

// Ящик 0,2 м с центром center.
InteractDesc box(core::Vec3 center, Calls* calls, const char* key = "world.pc.reboot") {
    InteractDesc d;
    d.bounds_min = center - core::Vec3{0.1f, 0.1f, 0.1f};
    d.bounds_max = center + core::Vec3{0.1f, 0.1f, 0.1f};
    d.aim_point = center;
    d.hint_key = key;
    d.on_action = on_call;
    d.user = calls;
    return d;
}

platform::InputState press(uint32_t buttons) {
    platform::InputState in;
    in.pressed = buttons;
    in.held = buttons;
    return in;
}

const core::Vec3 kEye = {0, 1.6f, 0};
const core::Vec3 kForward = {0, 0, -1};

core::Vec3 dir_deg(float yaw_deg) {
    const float a = core::deg_to_rad(yaw_deg);
    return {-std::sin(a), 0, -std::cos(a)};
}

}  // namespace

TEST_CASE("ray_aabb: попадание, промах, дистанция, луч изнутри") {
    float t = 0;
    CHECK(game::ray_aabb({0, 0, 0}, {0, 0, -1}, {-1, -1, -5}, {1, 1, -3}, 10.0f, &t));
    CHECK(t == doctest::Approx(3.0f));
    CHECK_FALSE(game::ray_aabb({0, 0, 0}, {0, 0, -1}, {-1, -1, -5}, {1, 1, -3}, 2.0f, &t));
    CHECK_FALSE(game::ray_aabb({0, 0, 0}, {0, 0, 1}, {-1, -1, -5}, {1, 1, -3}, 10.0f, &t));
    CHECK_FALSE(game::ray_aabb({2, 0, 0}, {0, 0, -1}, {-1, -1, -5}, {1, 1, -3}, 10.0f, &t));
    CHECK(game::ray_aabb({0, 0, -4}, {0, 0, -1}, {-1, -1, -5}, {1, 1, -3}, 10.0f, &t));
    CHECK(t == doctest::Approx(0.0f));
}

TEST_CASE("interact: add проверяет описание, пул фиксированный") {
    InteractSystem s;
    Calls calls;
    InteractDesc bad = box({0, 1.6f, -1}, &calls);
    bad.hint_key = nullptr;
    CHECK(s.add(bad) == kNoInteract);
    bad = box({0, 1.6f, -1}, &calls);
    bad.on_action = nullptr;
    CHECK(s.add(bad) == kNoInteract);

    for (int i = 0; i < game::kMaxInteractables; ++i) REQUIRE(s.add(box({0, 0, 10.0f + i}, &calls)) != kNoInteract);
    CHECK(s.add(box({0, 0, 0}, &calls)) == kNoInteract);
    CHECK(s.count() == game::kMaxInteractables);
    s.clear();
    CHECK(s.count() == 0);
    CHECK(s.add(box({0, 0, 0}, &calls)) == 0);
}

TEST_CASE("interact: луч в предмет — цель, × вызывает колбэк") {
    InteractSystem s;
    Calls calls;
    const InteractId a = s.add(box({0, 1.6f, -1.0f}, &calls));
    s.update(kEye, kForward);
    REQUIRE(s.hovered() == a);
    CHECK(s.handle_input(press(platform::kButtonCross)));
    CHECK(calls.primary == 1);
    CHECK(calls.last == a);
    // □ без alt_key не обрабатывается.
    CHECK_FALSE(s.handle_input(press(platform::kButtonSquare)));
    CHECK(calls.secondary == 0);
    // Удержание без нажатия — не нажатие.
    platform::InputState held;
    held.held = platform::kButtonCross;
    CHECK_FALSE(s.handle_input(held));
    CHECK(calls.primary == 1);
}

TEST_CASE("interact: □ при заданном alt_key") {
    InteractSystem s;
    Calls calls;
    InteractDesc d = box({0, 1.6f, -1.0f}, &calls);
    d.alt_key = "world.pc.dust";
    s.add(d);
    s.update(kEye, kForward);
    CHECK(s.handle_input(press(platform::kButtonSquare)));
    CHECK(calls.secondary == 1);
    CHECK(s.hint().alt_key != nullptr);
}

TEST_CASE("interact: без цели нажатие не перехватывается") {
    InteractSystem s;
    Calls calls;
    s.add(box({0, 1.6f, 1.0f}, &calls));  // за спиной
    s.update(kEye, kForward);
    CHECK(s.hovered() == kNoInteract);
    CHECK_FALSE(s.handle_input(press(platform::kButtonCross)));
    CHECK(calls.primary == 0);
    core::Vec3 aim;
    CHECK_FALSE(s.aim_target(&aim));
}

TEST_CASE("interact: липкий конус — ~10° мимо ещё цель, 20° — уже нет") {
    InteractSystem s;
    Calls calls;
    const InteractId a = s.add(box({0, 1.6f, -1.5f}, &calls));
    s.update(kEye, dir_deg(10.0f));
    CHECK(s.hovered() == a);
    core::Vec3 aim;
    REQUIRE(s.aim_target(&aim));
    CHECK(aim.z == doctest::Approx(-1.5f));

    InteractSystem fresh;
    fresh.add(box({0, 1.6f, -1.5f}, &calls));
    fresh.update(kEye, dir_deg(20.0f));
    CHECK(fresh.hovered() == kNoInteract);
}

TEST_CASE("interact: гистерезис — захваченная цель держится в более широком конусе") {
    InteractSystem s;
    Calls calls;
    const InteractId a = s.add(box({0, 1.6f, -1.5f}, &calls));
    s.update(kEye, dir_deg(5.0f));
    REQUIRE(s.hovered() == a);
    s.update(kEye, dir_deg(15.0f));  // вне 12°, но внутри 17°
    CHECK(s.hovered() == a);
    s.update(kEye, dir_deg(25.0f));
    CHECK(s.hovered() == kNoInteract);
    s.update(kEye, dir_deg(15.0f));  // заново 15° уже не захватывает
    CHECK(s.hovered() == kNoInteract);
}

TEST_CASE("interact: из двух в конусе — ближе по углу; луч важнее конуса") {
    InteractSystem s;
    Calls calls;
    const InteractId left = s.add(box({-0.30f, 1.6f, -1.5f}, &calls));
    const InteractId right = s.add(box({0.15f, 1.6f, -1.5f}, &calls));
    s.update(kEye, dir_deg(1.0f));  // чуть влево, но right ближе к центру и под лучом нет никого
    CHECK(s.hovered() == right);
    s.update(kEye, core::normalize({-0.30f, 0.0f, -1.5f}));
    CHECK(s.hovered() == left);
}

TEST_CASE("interact: дальше max_distance и выключенные не выбираются") {
    InteractSystem s;
    Calls calls;
    InteractDesc far = box({0, 1.6f, -3.0f}, &calls);
    far.max_distance = 2.0f;
    s.add(far);
    s.update(kEye, kForward);
    CHECK(s.hovered() == kNoInteract);

    s.clear();
    const InteractId a = s.add(box({0, 1.6f, -1.0f}, &calls));
    s.update(kEye, kForward);
    REQUIRE(s.hovered() == a);
    s.set_enabled(a, false);
    CHECK(s.hovered() == kNoInteract);  // сразу, без ожидания update
    s.update(kEye, kForward);
    CHECK(s.hovered() == kNoInteract);
    s.set_enabled(a, true);
    s.update(kEye, kForward);
    CHECK(s.hovered() == a);
}

TEST_CASE("interact: set_hint меняет подсказку, номер доходит до hint()") {
    InteractSystem s;
    Calls calls;
    InteractDesc d = box({0, 1.6f, -1.0f}, &calls, "world.pc.power_on");
    d.hint_number = 4;
    const InteractId a = s.add(d);
    s.update(kEye, kForward);
    CHECK(std::string(s.hint().hint_key) == "world.pc.power_on");
    CHECK(s.hint().hint_number == 4);
    s.set_hint(a, "world.pc.power_off", 4);
    CHECK(std::string(s.hint().hint_key) == "world.pc.power_off");
    s.set_hint(a, nullptr);  // nullptr игнорируется
    CHECK(s.hint().hint_key != nullptr);
    s.set_hint(kNoInteract, "x");  // неверный id — без последствий
}

TEST_CASE("interact: колбэк может менять свой предмет") {
    struct Ctx {
        InteractSystem* sys;
        int calls = 0;
    };
    InteractSystem s;
    Ctx ctx{&s};
    InteractDesc d;
    d.bounds_min = {-0.1f, 1.5f, -1.1f};
    d.bounds_max = {0.1f, 1.7f, -0.9f};
    d.aim_point = {0, 1.6f, -1.0f};
    d.hint_key = "world.pc.reboot";
    d.user = &ctx;
    d.on_action = [](InteractId id, InteractAction, void* user) {
        Ctx& c = *static_cast<Ctx*>(user);
        ++c.calls;
        c.sys->set_enabled(id, false);
    };
    s.add(d);
    s.update(kEye, kForward);
    CHECK(s.handle_input(press(platform::kButtonCross)));
    CHECK(ctx.calls == 1);
    CHECK(s.hovered() == kNoInteract);
}

TEST_CASE("interact: highlight_rect — рамка вокруг цели по центру экрана") {
    InteractSystem s;
    Calls calls;
    s.add(box({0, 1.6f, -1.0f}, &calls));
    const core::Mat4 view = core::mat4_look_at(kEye, kEye + kForward, {0, 1, 0});
    const core::Mat4 proj = core::mat4_perspective(core::deg_to_rad(60.0f), 480.0f / 272.0f, 0.05f, 30.0f);
    game::ScreenRect r;
    CHECK_FALSE(s.highlight_rect(proj * view, &r));  // ещё нет цели
    s.update(kEye, kForward);
    REQUIRE(s.highlight_rect(proj * view, &r));
    CHECK(r.x + r.w / 2 == doctest::Approx(240).epsilon(0.02));
    CHECK(r.y + r.h / 2 == doctest::Approx(136).epsilon(0.02));
    CHECK(r.w > 20);
    CHECK(r.w < 200);
}
