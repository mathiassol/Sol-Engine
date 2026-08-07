#pragma once

#include <engine/math/vec3.hpp>
#include <engine/math/vec4.hpp>

namespace engine::math {

// Column-major, right-handed, Y-up. See docs/FOUNDATION.md.
struct Mat4 {
    Vec4 cols[4]{};

    static Mat4 identity();
    static Mat4 translate(Vec3 t);
    static Mat4 scale(Vec3 s);
    static Mat4 look_at(Vec3 eye, Vec3 target, Vec3 up);
    static Mat4 perspective(f32 fov_y_radians, f32 aspect, f32 near_z, f32 far_z);

    static Mat4 inverse_affine(Mat4 m);
};

Mat4 operator*(Mat4 a, Mat4 b);

} // namespace engine::math
