#include <engine/math/mat4.hpp>

#include <engine/math/constants.hpp>
#include <engine/math/vec3.hpp>

#include <algorithm>
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
    // Three unguarded denominators. A zero fov, a zero aspect (a minimised
    // window), or near == far produced inf/NaN that flowed straight into a
    // constant buffer and out to the GPU. Clamp to a degenerate-but-finite
    // frustum instead: the frame looks wrong, which is visible and local,
    // rather than poisoning culling and shadow fitting silently.
    constexpr f32 kMinFov = 1.e-3f;
    constexpr f32 kMinAspect = 1.e-3f;
    constexpr f32 kMinDepthRange = 1.e-4f;

    const f32 fov = std::clamp(fov_y_radians, kMinFov, kPi - kMinFov);
    const f32 safe_aspect = (aspect > kMinAspect) ? aspect : kMinAspect;
    f32 tan_half = std::tan(fov * 0.5f);
    if (!(tan_half > kMinFov)) {
        tan_half = kMinFov;
    }
    f32 depth_range = near_z - far_z;
    if (std::abs(depth_range) < kMinDepthRange) {
        depth_range = -kMinDepthRange;
    }

    Mat4 m{};
    m.cols[0].x = 1.0f / (safe_aspect * tan_half);
    m.cols[1].y = 1.0f / tan_half;
    m.cols[2].z = far_z / depth_range;
    m.cols[2].w = -1.0f;
    m.cols[3].z = (near_z * far_z) / depth_range;
    return m;
}

Mat4 Mat4::ortho(f32 left, f32 right, f32 bottom, f32 top, f32 near_z, f32 far_z) {
    // Same reasoning as perspective(): a degenerate extent must not become a
    // NaN sun view-projection. The shadow pass fits this to visible bounds
    // every frame, so an empty scene reaches here with a zero extent.
    constexpr f32 kMinExtent = 1.e-4f;
    auto safe_range = [](f32 range) {
        if (!(std::abs(range) > kMinExtent)) {
            return range < 0.f ? -kMinExtent : kMinExtent;
        }
        return range;
    };
    const f32 width = safe_range(right - left);
    const f32 height = safe_range(top - bottom);
    const f32 depth = safe_range(near_z - far_z);

    Mat4 m = identity();
    m.cols[0].x = 2.f / width;
    m.cols[1].y = 2.f / height;
    m.cols[2].z = 1.f / depth;
    m.cols[3].x = -(right + left) / width;
    m.cols[3].y = -(top + bottom) / height;
    m.cols[3].z = near_z / depth;
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
