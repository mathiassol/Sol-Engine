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
    static Mat4 ortho(f32 left, f32 right, f32 bottom, f32 top, f32 near_z, f32 far_z);

    static Mat4 inverse_affine(Mat4 m);

    Vec3 transform_point(Vec3 p) const {
        return {
            cols[0].x * p.x + cols[1].x * p.y + cols[2].x * p.z + cols[3].x,
            cols[0].y * p.x + cols[1].y * p.y + cols[2].y * p.z + cols[3].y,
            cols[0].z * p.x + cols[1].z * p.y + cols[2].z * p.z + cols[3].z,
        };
    }
};

Mat4 operator*(Mat4 a, Mat4 b);

} // namespace engine::math
