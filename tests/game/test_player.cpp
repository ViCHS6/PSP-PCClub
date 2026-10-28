// T010: игрок — поворот, ходьба, наклон, коллизия, притяжение прицела.
#include <doctest/doctest.h>

#include <cmath>

#include "game/player.h"

using game::Player;
using game::Colliders;
namespace controls = game::controls;

namespace {

platform::InputState stick(float x, float y, uint32_t held = 0) {
    platform::InputState in;
    in.stick_x = x;
    in.stick_y = y;
    in.held = held;
    return in;
}

void run(Player& p, const platform::InputState& in, const Colliders& c, float seconds,
         const core::Vec3* aim = nullptr) {
    const float dt = 1.0f / 30.0f;
    for (float t = 0.0f; t < seconds; t += dt) p.update(dt, in, c, aim);
}

}  // namespace

TEST_CASE("player: yaw 0 смотрит в -Z, стик вперёд идёт в -Z") {
    Player p;
    Colliders c;
    p.reset({0, 0, 0}, 0.0f, 0.0f);
    CHECK(p.forward().z == doctest::Approx(-1.0f));
    run(p, stick(0, 1), c, 1.0f);
    CHECK(p.position().z == doctest::Approx(-controls::kWalkSpeed).epsilon(0.05));
    CHECK(p.position().x == doctest::Approx(0.0f));
    CHECK(p.eye().y == doctest::Approx(controls::kEyeHeight));
}

TEST_CASE("player: R ускоряет, стик назад — назад") {
    Player p;
    Colliders c;
    p.reset({0, 0, 0}, 0.0f);
    run(p, stick(0, 1, platform::kButtonR), c, 1.0f);
    CHECK(p.position().z == doctest::Approx(-controls::kRunSpeed).epsilon(0.05));
    p.reset({0, 0, 0}, 0.0f);
    run(p, stick(0, -1), c, 1.0f);
    CHECK(p.position().z > 1.0f);
}

TEST_CASE("player: крестовина ←/→ — шаг вбок без поворота, медленнее ходьбы") {
    Player p;
    Colliders c;
    p.reset({0, 0, 0}, 0.0f);
    run(p, stick(0, 0, platform::kButtonRight), c, 1.0f);
    CHECK(p.position().x == doctest::Approx(controls::kStrafeSpeed).epsilon(0.05));
    CHECK(p.position().z == doctest::Approx(0.0f));
    CHECK(p.yaw() == doctest::Approx(0.0f));
    CHECK(controls::kStrafeSpeed < controls::kWalkSpeed);

    // Повёрнуты налево (взгляд в −X): «вправо» — это −Z.
    p.reset({0, 0, 0}, core::kPi * 0.5f);
    run(p, stick(0, 0, platform::kButtonRight), c, 1.0f);
    CHECK(p.position().z < -0.7f);
    p.reset({0, 0, 0}, 0.0f);
    run(p, stick(0, 0, platform::kButtonLeft | platform::kButtonRight), c, 1.0f);
    CHECK(p.position().x == doctest::Approx(0.0f));
    // Шаг вбок упирается в препятствие, как и ходьба.
    REQUIRE(c.add({{0.5f, 0, -1}, {1.0f, 1.0f, 1}}));
    p.reset({0, 0, 0}, 0.0f);
    run(p, stick(0, 0, platform::kButtonLeft), c, 0.1f);
    run(p, stick(0, 0, platform::kButtonRight), c, 2.0f);
    CHECK(p.position().x == doctest::Approx(0.5f - controls::kRadius).epsilon(0.01));
}

TEST_CASE("player: стик вправо поворачивает направо (yaw убывает)") {
    Player p;
    Colliders c;
    p.reset({0, 0, 0}, 0.0f);
    run(p, stick(1, 0), c, 0.5f);
    CHECK(p.yaw() < -0.5f);
    CHECK(p.forward().x > 0.4f);  // смотрим в сторону +X
    CHECK(p.position().x == doctest::Approx(0.0f));  // поворот без шага
}

TEST_CASE("player: L + стик — обзор без ходьбы, наклон ограничен") {
    Player p;
    Colliders c;
    p.reset({0, 0, 0}, 0.0f, 0.0f);
    run(p, stick(0, 1, platform::kButtonL), c, 5.0f);
    CHECK(p.position().z == doctest::Approx(0.0f));
    CHECK(p.pitch() == doctest::Approx(controls::kPitchMax));
    run(p, stick(0, -1, platform::kButtonL), c, 5.0f);
    CHECK(p.pitch() == doctest::Approx(controls::kPitchMin));
}

TEST_CASE("player: при ходьбе без цели взгляд возвращается к горизонту") {
    Player p;
    Colliders c;
    p.reset({0, 0, 0}, 0.0f, controls::kPitchMin);
    run(p, stick(0, 1), c, 3.0f);
    CHECK(p.pitch() == doctest::Approx(controls::kPitchNeutral).epsilon(0.05));
}

TEST_CASE("player: stick_response сохраняет знак и сглаживает малые отклонения") {
    CHECK(game::stick_response(1.0f) == doctest::Approx(1.0f));
    CHECK(game::stick_response(-1.0f) == doctest::Approx(-1.0f));
    CHECK(game::stick_response(0.5f) < 0.5f);
    CHECK(game::stick_response(-0.5f) == doctest::Approx(-game::stick_response(0.5f)));
}

TEST_CASE("player: angle_delta в (-pi, pi]") {
    CHECK(game::angle_delta(0.1f, 2.0f * core::kPi) == doctest::Approx(0.1f).epsilon(0.001));
    CHECK(game::angle_delta(3.0f, -3.0f) == doctest::Approx(6.0f - 2.0f * core::kPi));
}

TEST_CASE("colliders: стол не пускает, по стене скользим") {
    Colliders c;
    REQUIRE(c.add({{-0.6f, 0, -1.85f}, {0.6f, 0.75f, -1.15f}}));
    Player p;
    p.reset({0, 0, 0}, 0.0f);
    run(p, stick(0, 1), c, 3.0f);
    CHECK(p.position().z == doctest::Approx(-1.15f + controls::kRadius).epsilon(0.01));

    // Наискосок вдоль грани: Z упирается, X продолжает меняться.
    p.reset({0, 0, -0.9f}, core::kPi * 0.25f);  // смотрим в (-X, -Z)
    run(p, stick(0, 1), c, 0.5f);  // за 0,5 с до угла стола не доходим
    CHECK(p.position().z >= -1.15f + controls::kRadius - 0.01f);
    CHECK(p.position().x < -0.4f);
}

TEST_CASE("colliders: низкие препятствия и предметы над головой не мешают") {
    Colliders c;
    c.add({{-1, 0, -1}, {1, 0.1f, 1}});     // порог ниже kStepHeight
    c.add({{-1, 2.0f, -1}, {1, 2.2f, 1}});  // балка выше роста
    const core::Vec3 p = c.resolve({0, 0, 0}, controls::kRadius);
    CHECK(p.x == doctest::Approx(0.0f));
    CHECK(p.z == doctest::Approx(0.0f));
}

TEST_CASE("colliders: центр внутри ящика — к ближайшей грани") {
    Colliders c;
    c.add({{-1, 0, -1}, {1, 1, 1}});
    const core::Vec3 p = c.resolve({0.9f, 0, 0}, 0.25f);
    CHECK(p.x == doctest::Approx(1.25f));
}

TEST_CASE("colliders: пул фиксированный") {
    Colliders c;
    for (int i = 0; i < Colliders::kMax; ++i) REQUIRE(c.add({{0, 0, 0}, {1, 1, 1}}));
    CHECK_FALSE(c.add({{0, 0, 0}, {1, 1, 1}}));
    CHECK(c.count() == Colliders::kMax);
}

TEST_CASE("player: липкий прицел тянет взгляд к цели и отпускает при сильном повороте") {
    Player p;
    Colliders c;
    p.reset({0, 0, 0}, 0.15f, -0.2f);
    const core::Vec3 aim = {0.0f, 1.0f, -1.5f};  // ровно по −Z, ниже глаз
    run(p, stick(0, 0), c, 1.0f, &aim);
    CHECK(p.yaw() == doctest::Approx(0.0f).epsilon(0.02));
    CHECK(p.pitch() == doctest::Approx(std::atan2(-0.6f, 1.5f)).epsilon(0.02));

    // Стик до упора — притяжения нет, но поворот замедлен трением.
    Player free_turn;
    free_turn.reset({0, 0, 0}, 0.0f);
    run(free_turn, stick(1, 0), c, 0.5f);
    p.reset({0, 0, 0}, 0.0f);
    run(p, stick(1, 0), c, 0.5f, &aim);
    CHECK(p.yaw() < -0.3f);
    CHECK(p.yaw() > free_turn.yaw());
}
