#include <engine/math/mat4.hpp>

#include <engine/math/constants.hpp>
#include <engine/math/vec3.hpp>

#include <cmath>

namespace engine::math {

Mat4 Mat4::identity() {
    Mat4 m{};
    m.cols[0] = {1.f, 0.f, 0.f, 0.f};
    m.cols[1] = {0.f, 1.f, 0.f, 0.f};
    m.cols[2] = {0.f, 0.f, 1.f, 0.f};
    m.cols[3] = {0.f, 0.f, 0.f, 1.f};
    return m;
}

Mat4 Mat4::translate(Vec3 t) {
    Mat4 m = identity();
    m.cols[3] = {t.x, t.y, t.z, 1.f};
    return m;
}

Mat4 Mat4::scale(Vec3 s) {
    Mat4 m = identity();
    m.cols[0].x = s.x;
    m.cols[1].y = s.y;
    m.cols[2].z = s.z;
    return m;
}

Mat4 Mat4::look_at(Vec3 eye, Vec3 target, Vec3 up) {
    Vec3 f = (target - eye).normalized();
    Vec3 s = f.cross(up).normalized();
    Vec3 u = s.cross(f);

    Mat4 m = identity();
    m.cols[0] = { s.x,  u.x, -f.x, 0.f};
    m.cols[1] = { s.y,  u.y, -f.y, 0.f};
    m.cols[2] = { s.z,  u.z, -f.z, 0.f};
    m.cols[3] = {-s.dot(eye), -u.dot(eye), f.dot(eye), 1.f};
    return m;
}

Mat4 Mat4::perspective(f32 fov_y_radians, f32 aspect, f32 near_z, f32 far_z) {
    f32 tan_half = std::tan(fov_y_radians * 0.5f);
    Mat4 m{};
    m.cols[0].x = 1.0f / (aspect * tan_half);
    m.cols[1].y = 1.0f / tan_half;
    m.cols[2].z = far_z / (near_z - far_z);
    m.cols[2].w = -1.0f;
    m.cols[3].z = (near_z * far_z) / (near_z - far_z);
    return m;
}

Mat4 Mat4::ortho(f32 left, f32 right, f32 bottom, f32 top, f32 near_z, f32 far_z) {
    Mat4 m = identity();
    m.cols[0].x = 2.f / (right - left);
    m.cols[1].y = 2.f / (top - bottom);
    m.cols[2].z = 1.f / (near_z - far_z);
    m.cols[3].x = -(right + left) / (right - left);
    m.cols[3].y = -(top + bottom) / (top - bottom);
    m.cols[3].z = near_z / (near_z - far_z);
    return m;
}

Mat4 Mat4::inverse_affine(Mat4 m) {
    // Inverse of [R | t; 0 0 0 1] for orthonormal R.
    Vec3 c0{m.cols[0].x, m.cols[0].y, m.cols[0].z};
    Vec3 c1{m.cols[1].x, m.cols[1].y, m.cols[1].z};
    Vec3 c2{m.cols[2].x, m.cols[2].y, m.cols[2].z};
    Vec3 t{m.cols[3].x, m.cols[3].y, m.cols[3].z};

    Mat4 inv = identity();
    inv.cols[0] = {c0.x, c1.x, c2.x, 0.f};
    inv.cols[1] = {c0.y, c1.y, c2.y, 0.f};
    inv.cols[2] = {c0.z, c1.z, c2.z, 0.f};
    inv.cols[3] = {
        -c0.dot(t),
        -c1.dot(t),
        -c2.dot(t),
        1.f,
    };
    return inv;
}

Mat4 operator*(Mat4 a, Mat4 b) {
    Mat4 r{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            r.cols[col][row] =
                a.cols[0][row] * b.cols[col].x +
                a.cols[1][row] * b.cols[col].y +
                a.cols[2][row] * b.cols[col].z +
                a.cols[3][row] * b.cols[col].w;
        }
    }
    return r;
}

} // namespace engine::math
