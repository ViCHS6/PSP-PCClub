#include <doctest/doctest.h>

#include "core/math.h"

using namespace core;

namespace {

void check_vec(Vec3 a, Vec3 b) {
    CHECK(a.x == doctest::Approx(b.x).epsilon(1e-4));
    CHECK(a.y == doctest::Approx(b.y).epsilon(1e-4));
    CHECK(a.z == doctest::Approx(b.z).epsilon(1e-4));
}

}  // namespace

TEST_CASE("identity не меняет точку") {
    check_vec(mat4_transform_point(mat4_identity(), {1, 2, 3}), {1, 2, 3});
}

TEST_CASE("перенос, затем поворот: порядок умножения справа налево") {
    // M = T · R: сначала поворот вокруг Y на 90°, потом перенос.
    const Mat4 m = mat4_translate({10, 0, 0}) * mat4_rotate_y(deg_to_rad(90));
    check_vec(mat4_transform_point(m, {1, 0, 0}), {10, 0, -1});
}

TEST_CASE("перспектива: near → z = −1, far → z = +1") {
    const Mat4 p = mat4_perspective(deg_to_rad(60), 480.0f / 272.0f, 0.1f, 50.0f);
    CHECK(mat4_transform_point(p, {0, 0, -0.1f}).z == doctest::Approx(-1.0f).epsilon(1e-3));
    CHECK(mat4_transform_point(p, {0, 0, -50.0f}).z == doctest::Approx(1.0f).epsilon(1e-3));
}

TEST_CASE("look_at: цель оказывается на оси −Z камеры") {
    const Mat4 v = mat4_look_at({0, 2, 5}, {0, 2, 0}, {0, 1, 0});
    check_vec(mat4_transform_point(v, {0, 2, 0}), {0, 0, -5});
    check_vec(mat4_transform_point(v, {1, 2, 5}), {1, 0, 0});
}
