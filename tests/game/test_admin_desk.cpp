// T033: въезд камеры в монитор стойки (поза) и замер обслуживания у стойки.
#include <doctest/doctest.h>

#include <cmath>

#include "game/admin_desk.h"

using core::VisitorState;
using game::CameraPose;
using game::ServiceTimer;

namespace {

core::World g_world;  // статически: World большой

// Посетитель vi с номером визита serial встал первым в очередь.
void arrive(core::World& w, uint8_t vi, uint16_t serial) {
    w.visitors[vi] = core::Visitor{};
    w.visitors[vi].state = VisitorState::kWaiting;
    w.visitors[vi].serial = serial;
    w.queue[w.queue_len++] = vi;
}

// Первый в очереди ушёл от стойки в состояние state.
void leave_desk(core::World& w, VisitorState state, core::LeaveReason reason = core::LeaveReason::kNone) {
    const uint8_t vi = w.queue[0];
    for (int i = 0; i + 1 < w.queue_len; ++i) w.queue[i] = w.queue[i + 1];
    --w.queue_len;
    w.visitors[vi].state = state;
    w.visitors[vi].leave_reason = reason;
}

core::World& fresh_world() {
    g_world = core::World{};
    return g_world;
}

void run(ServiceTimer& t, const core::World& w, float seconds) {
    const float dt = 0.1f;
    for (int i = 0; i < static_cast<int>(seconds / dt + 0.5f); ++i) t.update(dt, w);
}

}  // namespace

TEST_CASE("стойка: камера в конце въезда — перед центром экрана, взгляд в экран") {
    // Экран смотрит в +X (поворот на 90° вокруг Y), центр — (1; 1,3; 2).
    const core::Mat4 screen = core::mat4_translate({1.0f, 1.3f, 2.0f}) * core::mat4_rotate_y(core::deg_to_rad(90.0f));
    const CameraPose p = game::screen_pose(screen, 0.2f);
    CHECK(p.eye.x == doctest::Approx(1.2f).epsilon(1e-4));
    CHECK(p.eye.y == doctest::Approx(1.3f).epsilon(1e-4));
    CHECK(p.eye.z == doctest::Approx(2.0f).epsilon(1e-4));
    CHECK(std::fabs(p.pitch) < 1e-4f);
    // В пространстве камеры центр экрана — прямо по взгляду (−Z) на 0,2 м.
    const core::Vec3 c = core::mat4_transform_point(game::pose_view(p), {1.0f, 1.3f, 2.0f});
    CHECK(std::fabs(c.x) < 1e-4f);
    CHECK(std::fabs(c.y) < 1e-4f);
    CHECK(c.z == doctest::Approx(-0.2f).epsilon(1e-4));
}

TEST_CASE("стойка: экран монитора заполняет кадр по высоте в конце въезда") {
    // GDD §9.2: «камера въезжает в монитор» — экран 0,24 м по высоте не меньше угла обзора 60°.
    const float half_height = 0.12f;
    CHECK(half_height / game::desk::kScreenDistance >= std::tan(core::deg_to_rad(30.0f)) * 0.98f);
}

TEST_CASE("стойка: интерполяция позы — концы, плавность, поворот по короткой дуге") {
    const CameraPose a{{0.0f, 1.6f, 0.0f}, 3.0f, -0.1f};
    const CameraPose b{{1.0f, 1.0f, 0.0f}, -3.0f, 0.0f};
    const CameraPose p0 = game::pose_lerp(a, b, 0.0f);
    const CameraPose p1 = game::pose_lerp(a, b, 1.0f);
    CHECK(p0.eye.x == doctest::Approx(0.0f));
    CHECK(p0.yaw == doctest::Approx(3.0f));
    CHECK(p1.eye.x == doctest::Approx(1.0f));
    CHECK(p1.pitch == doctest::Approx(0.0f));
    CHECK(std::fabs(game::angle_delta(p1.yaw, -3.0f)) < 1e-4f);
    // Между 3 и −3 рад — через π, а не через 0.
    const CameraPose mid = game::pose_lerp(a, b, 0.5f);
    CHECK(std::fabs(mid.yaw) > 3.0f);
    // smoothstep: в начале пути медленнее линейного.
    CHECK(game::pose_lerp(a, b, 0.1f).eye.x < 0.1f);
    CHECK(game::pose_lerp(a, b, 2.0f).eye.x == doctest::Approx(1.0f));
}

TEST_CASE("замер: с × у стойки до посадки") {
    core::World& w = fresh_world();
    arrive(w, 3, 7);
    ServiceTimer t;
    t.start(w);
    CHECK(t.armed());
    CHECK(t.timing());
    run(t, w, 2.5f);
    CHECK(t.current() == doctest::Approx(2.5f).epsilon(0.01));
    CHECK(t.count() == 0);
    leave_desk(w, VisitorState::kToSeat);
    t.update(0.1f, w);
    CHECK(t.count() == 1);
    CHECK(t.last() == doctest::Approx(2.5f).epsilon(0.01));
    CHECK(!t.timing());
    CHECK(t.armed());
}

TEST_CASE("замер: ответ Кассы и отказ считаются, ушёл сам — нет") {
    using core::LeaveReason;
    struct Case {
        LeaveReason reason;
        bool counted;
    };
    const Case cases[] = {{LeaveReason::kNoGame, true},   {LeaveReason::kPrice, true},
                          {LeaveReason::kRefused, true},  {LeaveReason::kPatience, false},
                          {LeaveReason::kClosing, false}, {LeaveReason::kEvent, false}};
    for (const Case& c : cases) {
        core::World& w = fresh_world();
        arrive(w, 0, 1);
        ServiceTimer t;
        t.start(w);
        run(t, w, 1.0f);
        leave_desk(w, VisitorState::kLeaving, c.reason);
        t.update(0.1f, w);
        CAPTURE(static_cast<int>(c.reason));
        CHECK(t.count() == (c.counted ? 1 : 0));
        CHECK(!t.timing());
    }
}

TEST_CASE("замер: пустая очередь — ждём, отсчёт с прихода посетителя") {
    core::World& w = fresh_world();
    ServiceTimer t;
    t.start(w);
    CHECK(t.armed());
    CHECK(!t.timing());
    run(t, w, 5.0f);  // ждали у пустой стойки — в замер не входит
    arrive(w, 2, 9);
    t.update(0.1f, w);
    CHECK(t.timing());
    CHECK(t.current() == doctest::Approx(0.0f));
    run(t, w, 1.5f);
    leave_desk(w, VisitorState::kToSeat);
    t.update(0.1f, w);
    CHECK(t.last() == doctest::Approx(1.5f).epsilon(0.01));
}

TEST_CASE("замер: следующий в очереди — сразу свой отсчёт; статистика") {
    core::World& w = fresh_world();
    arrive(w, 0, 1);
    arrive(w, 1, 2);
    ServiceTimer t;
    t.start(w);
    run(t, w, 3.0f);
    leave_desk(w, VisitorState::kToSeat);
    t.update(0.1f, w);
    CHECK(t.timing());  // второй уже у стойки
    run(t, w, 5.0f);
    leave_desk(w, VisitorState::kLeaving, core::LeaveReason::kRefused);
    t.update(0.1f, w);
    CHECK(t.count() == 2);
    CHECK(t.last() == doctest::Approx(5.0f).epsilon(0.01));
    CHECK(t.best() == doctest::Approx(3.0f).epsilon(0.01));
    CHECK(t.worst() == doctest::Approx(5.0f).epsilon(0.01));
    CHECK(t.average() == doctest::Approx(4.0f).epsilon(0.01));
}

TEST_CASE("замер: выход из ОС отбрасывает текущий, слот занят другим визитом — тоже") {
    core::World& w = fresh_world();
    arrive(w, 4, 11);
    ServiceTimer t;
    t.start(w);
    run(t, w, 1.0f);
    t.stop();
    CHECK(!t.armed());
    leave_desk(w, VisitorState::kToSeat);
    t.update(0.1f, w);
    CHECK(t.count() == 0);

    // Слот посетителя переиспользован (другой serial) — это не обслуживание.
    core::World& w2 = fresh_world();
    arrive(w2, 4, 11);
    t.start(w2);
    w2.queue_len = 0;
    w2.visitors[4].serial = 12;
    w2.visitors[4].state = VisitorState::kToSeat;
    t.update(0.1f, w2);
    CHECK(t.count() == 0);
    CHECK(!t.timing());
}

TEST_CASE("стойка: путь камеры — концы на месте, из-за стойки — над монитором") {
    // Экран смотрит в −X (как у стойки в scene_club.json), глаз в конце — на 0,21 м перед ним.
    const core::Mat4 screen = core::mat4_translate({1.1f, 1.3f, 1.25f}) * core::mat4_rotate_y(core::deg_to_rad(270.0f));
    const CameraPose end = game::screen_pose(screen);
    CHECK(end.eye.x == doctest::Approx(1.1f - game::desk::kScreenDistance).epsilon(1e-4));

    const CameraPose front{{0.45f, 1.6f, 1.2f}, end.yaw, -0.2f};  // админ за стойкой
    const CameraPose back{{2.6f, 1.6f, 1.2f}, end.yaw + core::kPi, -0.2f};  // со стороны зала
    for (const CameraPose& from : {front, back}) {
        const CameraPose p0 = game::desk_pose(from, end, 0.0f);
        const CameraPose p1 = game::desk_pose(from, end, 1.0f);
        CHECK(p0.eye.x == doctest::Approx(from.eye.x));
        CHECK(p0.eye.y == doctest::Approx(from.eye.y));
        CHECK(p1.eye.x == doctest::Approx(end.eye.x));
        CHECK(p1.eye.y == doctest::Approx(end.eye.y));
    }
    // Со стороны зала камера пересекает плоскость монитора (x = 1,1) выше его верха (~1,45 м).
    float prev_x = back.eye.x;
    bool crossed = false;
    for (int i = 1; i <= 200; ++i) {
        const CameraPose p = game::desk_pose(back, end, i / 200.0f);
        if (prev_x > 1.1f && p.eye.x <= 1.1f) {
            crossed = true;
            CHECK(p.eye.y > 1.5f);
        }
        prev_x = p.eye.x;
    }
    CHECK(crossed);
}
