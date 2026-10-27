#pragma once
// Минимальная 3D-математика: векторы и матрицы 4×4.
// Матрицы — по столбцам (column-major), как ждут и OpenGL, и GU на PSP:
// m[12], m[13], m[14] — перенос. Векторы умножаются справа: p' = M · p.
#include <cmath>

namespace core {

constexpr float kPi = 3.14159265358979f;

inline float deg_to_rad(float deg) { return deg * (kPi / 180.0f); }

struct Vec2 {
    float x = 0.0f, y = 0.0f;
};

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a) {
    const float len = length(a);
    return len > 0.0f ? a * (1.0f / len) : a;
}

// Выравнивание 16 — GU читает матрицы напрямую из памяти.
struct alignas(16) Mat4 {
    float m[16];
};

inline Mat4 mat4_identity() {
    return {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
}

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a.m[k * 4 + row] * b.m[col * 4 + k];
            r.m[col * 4 + row] = s;
        }
    }
    return r;
}

inline Mat4 mat4_translate(Vec3 t) {
    Mat4 r = mat4_identity();
    r.m[12] = t.x;
    r.m[13] = t.y;
    r.m[14] = t.z;
    return r;
}

inline Mat4 mat4_scale(Vec3 s) {
    Mat4 r = mat4_identity();
    r.m[0] = s.x;
    r.m[5] = s.y;
    r.m[10] = s.z;
    return r;
}

inline Mat4 mat4_rotate_x(float rad) {
    const float c = std::cos(rad), s = std::sin(rad);
    Mat4 r = mat4_identity();
    r.m[5] = c;
    r.m[6] = s;
    r.m[9] = -s;
    r.m[10] = c;
    return r;
}

inline Mat4 mat4_rotate_y(float rad) {
    const float c = std::cos(rad), s = std::sin(rad);
    Mat4 r = mat4_identity();
    r.m[0] = c;
    r.m[2] = -s;
    r.m[8] = s;
    r.m[10] = c;
    return r;
}

inline Mat4 mat4_rotate_z(float rad) {
    const float c = std::cos(rad), s = std::sin(rad);
    Mat4 r = mat4_identity();
    r.m[0] = c;
    r.m[1] = s;
    r.m[4] = -s;
    r.m[5] = c;
    return r;
}

// Перспектива как gluPerspective: z в клип-пространстве от −1 (near) до +1 (far).
inline Mat4 mat4_perspective(float fov_y_rad, float aspect, float z_near, float z_far) {
    const float f = 1.0f / std::tan(fov_y_rad * 0.5f);
    Mat4 r = {};
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (z_far + z_near) / (z_near - z_far);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * z_far * z_near) / (z_near - z_far);
    return r;
}

// Камера смотрит из eye в target; правая система координат, Y вверх.
inline Mat4 mat4_look_at(Vec3 eye, Vec3 target, Vec3 up) {
    const Vec3 f = normalize(target - eye);
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);
    Mat4 r = mat4_identity();
    r.m[0] = s.x;
    r.m[4] = s.y;
    r.m[8] = s.z;
    r.m[1] = u.x;
    r.m[5] = u.y;
    r.m[9] = u.z;
    r.m[2] = -f.x;
    r.m[6] = -f.y;
    r.m[10] = -f.z;
    r.m[12] = -dot(s, eye);
    r.m[13] = -dot(u, eye);
    r.m[14] = dot(f, eye);
    return r;
}

// Точка через матрицу с делением на w.
inline Vec3 mat4_transform_point(const Mat4& a, Vec3 p) {
    const float x = a.m[0] * p.x + a.m[4] * p.y + a.m[8] * p.z + a.m[12];
    const float y = a.m[1] * p.x + a.m[5] * p.y + a.m[9] * p.z + a.m[13];
    const float z = a.m[2] * p.x + a.m[6] * p.y + a.m[10] * p.z + a.m[14];
    const float w = a.m[3] * p.x + a.m[7] * p.y + a.m[11] * p.z + a.m[15];
    const float inv_w = w != 0.0f ? 1.0f / w : 1.0f;
    return {x * inv_w, y * inv_w, z * inv_w};
}

}  // namespace core
