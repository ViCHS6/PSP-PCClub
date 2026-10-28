// T010: игрок — ходьба крестовиной, обзор стиком, наклон, коллизия, притяжение прицела.
#include <doctest/doctest.h>

#include <cmath>

#include "game/player.h"

using game::Player;
using game::Colliders;
namespace controls = game::controls;

namespace {

platform::InputState pad(uint32_t held, float x = 0.0f, float y = 0.0f) {
    platform::InputState in;
    in.held = held;
    in.stick_x = x;
    in.stick_y = y;
    return in;
}

platform::InputState stick(float x, float y) { return pad(0, x, y); }

void run(Player& p, const platform::InputState& in, const Colliders& c, float seconds,
         const core::Vec3* aim = nullptr) {
    const float dt = 1.0f / 30.0f;
    for (float t = 0.0f; t < seconds; t += dt) p.update(dt, in, c, aim);
}

float flat_distance(const Player& p) {
    return std::sqrt(p.position().x * p.position().x + p.position().z * p.position().z);
}

}  // namespace

TEST_CASE("player: yaw 0 смотрит в -Z, крестовина ↑ идёт в -Z") {
    Player p;
    Colliders c;
    p.reset({0, 0, 0}, 0.0f, 0.0f);
    CHECK(p.forward().z == doctest::Approx(-1.0f));
    run(p, pad(platform::kButtonUp), c, 1.0f);
    CHECK(p.position().z == doctest::Approx(-controls::kWalkSpeed).epsilon(0.05));
    CHECK(p.position().x == doctest::Approx(0.0f));
    CHECK(p.eye().y == doctest::Approx(controls::kEyeHeight));
    CHECK(p.yaw() == doctest::Approx(0.0f));
}

TEST_CASE("player: R ускоряет, ↓ — назад, ↑+↓ — стоим") {
    Player p;
    Colliders c;
    p.reset({0, 0, 0}, 0.0f);
    run(p, pad(platform::kButtonUp | platform::kButtonR), c, 1.0f);
    CHECK(p.position().z == doctest::Approx(-controls::kRunSpeed).epsilon(0.05));
    p.reset({0, 0, 0}, 0.0f);
    run(p, pad(platform::kButtonDown), c, 1.0f);
    CHECK(p.position().z == doctest::Approx(controls::kWalkSpeed).epsilon(0.05));
    p.reset({0, 0, 0}, 0.0f);
    run(p, pad(platform::kButtonUp | platform::kButtonDown), c, 1.0f);
    CHECK(flat_distance(p) == doctest::Approx(0.0f));
}

TEST_CASE("player: крестовина ←/→ — шаг вбок без поворота, медленнее ходьбы") {
    Player p;
    Colliders c;
    p.reset({0, 0, 0}, 0.0f);
    run(p, pad(platform::kButtonRight), c, 1.0f);
    CHECK(p.position().x == doctest::Approx(controls::kStrafeSpeed).epsilon(0.05));
    CHECK(p.position().z == doctest::Approx(0.0f));
    CHECK(p.yaw() == doctest::Approx(0.0f));
    CHECK(controls::kStrafeSpeed < controls::kWalkSpeed);

    // Повёрнуты налево (взгляд в −X): «вправо» — это −Z.
    p.reset({0, 0, 0}, core::kPi * 0.5f);
    run(p, pad(platform::kButtonRight), c, 1.0f);
    CHECK(p.position().z < -0.7f);
    p.reset({0, 0, 0}, 0.0f);
    run(p, pad(platform::kButtonLeft | platform::kButtonRight), c, 1.0f);
    CHECK(p.position().x == doctest::Approx(0.0f));
    // Шаг вбок упирается в препятствие, как и ходьба.
    REQUIRE(c.add({{0.5f, 0, -1}, {1.0f, 1.0f, 1}}));
    p.reset({0, 0, 0}, 0.0f);
    run(p, pad(platform::kButtonRight), c, 2.0f);
    CHECK(p.position().x == doctest::Approx(0.5f - controls::kRadius).epsilon(0.01));
}

TEST_CASE("player: диагональ ↑+← не быстрее прямого шага и идёт вперёд-влево") {
    Player p;
    Colliders c;
    p.reset({0, 0, 0}, 0.0f);
    run(p, pad(platform::kButtonUp | platform::kButtonLeft), c, 1.0f);
    CHECK(flat_distance(p) == doctest::Approx(controls::kWalkSpeed).epsilon(0.05));
    CHECK(p.position().z < 0.0f);
    CHECK(p.position().x < 0.0f);
    // Пропорция сохраняется: вперёд быстрее, чем вбок.
    CHECK(-p.position().z > -p.position().x);
}

TEST_CASE("player: стик X — поворот без шага, стик Y — наклон без шага") {
    Player p;
    Colliders c;
    p.reset({0, 0, 0}, 0.0f, 0.0f);
    run(p, stick(1, 0), c, 0.5f);
    CHECK(p.yaw() < -0.5f);
    CHECK(p.forward().x > 0.4f);  // смотрим в сторону +X
    CHECK(flat_distance(p) == doctest::Approx(0.0f));

    p.reset({0, 0, 0}, 0.0f, 0.0f);
    run(p, stick(0, 1), c, 0.3f);
    CHECK(p.pitch() > 0.3f);
    CHECK(p.yaw() == doctest::Approx(0.0f));
    CHECK(flat_distance(p) == doctest::Approx(0.0f));
}

TEST_CASE("player: наклон ограничен") {
    Player p;
    Colliders c;
    p.reset({0, 0, 0}, 0.0f, 0.0f);
    run(p, stick(0, 1), c, 5.0f);
    CHECK(p.pitch() == doctest::Approx(controls::kPitchMax));
    run(p, stick(0, -1), c, 5.0f);
    CHECK(p.pitch() == doctest::Approx(controls::kPitchMin));
}

TEST_CASE("player: ходьба и обзор одновременно") {
    Player p;
    Colliders c;
    p.reset({0, 0, 0}, 0.0f, 0.0f);
    run(p, pad(platform::kButtonUp, 1.0f, 0.0f), c, 1.0f);
    CHECK(p.yaw() < -1.0f);         // повернулись
    CHECK(flat_distance(p) > 1.0f);  // и прошли, а не стояли на месте
    CHECK(p.position().x > 0.2f);    // путь загибается вправо вслед за взглядом
}

TEST_CASE("player: L больше ничего не делает") {
    Player a, b;
    Colliders c;
    a.reset({0, 0, 0}, 0.0f, 0.0f);
    b.reset({0, 0, 0}, 0.0f, 0.0f);
    run(a, pad(platform::kButtonUp, 0.5f, 0.5f), c, 1.0f);
    run(b, pad(platform::kButtonUp | platform::kButtonL, 0.5f, 0.5f), c, 1.0f);
    CHECK(a.yaw() == doctest::Approx(b.yaw()));
    CHECK(a.pitch() == doctest::Approx(b.pitch()));
    CHECK(a.position().x == doctest::Approx(b.position().x));
    CHECK(a.position().z == doctest::Approx(b.position().z));
}

TEST_CASE("player: stick_response — мёртвая зона, знак, монотонность, точность у центра") {
    CHECK(game::stick_response(0.0f) == 0.0f);
    CHECK(game::stick_response(controls::kLookDeadZone) == 0.0f);
    CHECK(game::stick_response(1.0f) == doctest::Approx(1.0f));
    CHECK(game::stick_response(-1.0f) == doctest::Approx(-1.0f));
    CHECK(game::stick_response(1.5f) == doctest::Approx(1.0f));
    CHECK(game::stick_response(-0.5f) == doctest::Approx(-game::stick_response(0.5f)));
    CHECK(game::stick_response(0.5f) < 0.5f);   // медленнее линейного — точнее доворот
    CHECK(game::stick_response(0.15f) > 0.0f);  // но малый наклон всё же двигает
    float prev = 0.0f;
    for (int i = 1; i <= 100; ++i) {
        const float r = game::stick_response(i * 0.01f);
        CHECK(r >= prev);
        prev = r;
    }
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
    run(p, pad(platform::kButtonUp), c, 3.0f);
    CHECK(p.position().z == doctest::Approx(-1.15f + controls::kRadius).epsilon(0.01));

    // Наискосок вдоль грани: Z упирается, X продолжает меняться.
    p.reset({0, 0, -0.9f}, core::kPi * 0.25f);  // смотрим в (-X, -Z)
    run(p, pad(platform::kButtonUp), c, 0.5f);  // за 0,5 с до угла стола не доходим
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

TEST_CASE("player: липкий прицел тянет взгляд к цели, пока стик отпущен") {
    Player p;
    Colliders c;
    p.reset({0, 0, 0}, 0.15f, -0.2f);
    const core::Vec3 aim = {0.0f, 1.0f, -1.5f};  // ровно по −Z, ниже глаз
    run(p, stick(0, 0), c, 1.0f, &aim);
    CHECK(p.yaw() == doctest::Approx(0.0f).epsilon(0.02));
    CHECK(p.pitch() == doctest::Approx(std::atan2(-0.6f, 1.5f)).epsilon(0.02));

    // При ходьбе крестовиной прицел тоже тянет — стик ведь свободен.
    p.reset({0, 0, 0.5f}, 0.15f, -0.2f);
    run(p, pad(platform::kButtonUp), c, 0.3f, &aim);
    CHECK(std::fabs(p.yaw()) < 0.1f);
}

TEST_CASE("player: прицел не борется с ручным обзором") {
    Colliders c;
    const core::Vec3 aim = {0.0f, 1.6f, -1.5f};

    // Стик отклонён сильнее kAimPullFade: поворот как без цели, только с трением, и никакого
    // возврата к цели — взгляд уходит от неё монотонно.
    Player free_look, held;
    free_look.reset({0, 0, 0}, 0.0f, 0.0f);
    held.reset({0, 0, 0}, 0.0f, 0.0f);
    const platform::InputState slow = stick(controls::kAimPullFade + 0.05f, 0);
    float prev = 0.0f;
    const float dt = 1.0f / 30.0f;
    for (int i = 0; i < 15; ++i) {
        free_look.update(dt, slow, c, nullptr);
        held.update(dt, slow, c, &aim);
        CHECK(held.yaw() < prev);  // каждый кадр дальше от цели
        prev = held.yaw();
    }
    CHECK(held.yaw() == doctest::Approx(free_look.yaw() * controls::kAimFriction).epsilon(0.02));
    CHECK(held.pitch() == doctest::Approx(0.0f));

    // Лёгкое касание стика внутри kAimPullFade: притяжение ослаблено, а не полное.
    Player touch, idle;
    touch.reset({0, 0, 0}, 0.2f, 0.0f);
    idle.reset({0, 0, 0}, 0.2f, 0.0f);
    const float dt2 = 1.0f / 30.0f;
    touch.update(dt2, stick(0, controls::kAimPullFade * 0.5f), c, &aim);
    idle.update(dt2, stick(0, 0), c, &aim);
    CHECK(touch.yaw() < 0.2f);           // тянет
    CHECK(touch.yaw() > idle.yaw());     // но слабее, чем при отпущенном стике
}
