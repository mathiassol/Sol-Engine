#pragma once

#include <engine/math/aabb.hpp>
#include <engine/math/mat4.hpp>
#include <engine/math/vec3.hpp>
#include <engine/math/vec4.hpp>

#include <cmath>

namespace engine::math {

struct Plane {
    Vec3 normal{};
    f32 d = 0.f;

    f32 distance(Vec3 p) const { return normal.dot(p) + d; }
};

struct Frustum {
    Plane planes[6]{};

    static Frustum from_view_proj(Mat4 vp) {
        Frustum f{};
        const auto row = [&](int r) {
            return Vec4{vp.cols[0][r], vp.cols[1][r], vp.cols[2][r], vp.cols[3][r]};
        };
        const Vec4 r0 = row(0);
        const Vec4 r1 = row(1);
        const Vec4 r2 = row(2);
        const Vec4 r3 = row(3);

        const auto set_plane = [](Plane& p, f32 a, f32 b, f32 c, f32 d) {
            const f32 len = std::sqrt(a * a + b * b + c * c);
            if (len > 1.e-8f) {
                const f32 inv = 1.f / len;
                p.normal = {a * inv, b * inv, c * inv};
                p.d = d * inv;
            } else {
                p.normal = {};
                p.d = d;
            }
        };

        // D3D clip: -w <= x,y <= w and 0 <= z <= w.
        set_plane(f.planes[0], r3.x + r0.x, r3.y + r0.y, r3.z + r0.z, r3.w + r0.w); // left
        set_plane(f.planes[1], r3.x - r0.x, r3.y - r0.y, r3.z - r0.z, r3.w - r0.w); // right
        set_plane(f.planes[2], r3.x + r1.x, r3.y + r1.y, r3.z + r1.z, r3.w + r1.w); // bottom
        set_plane(f.planes[3], r3.x - r1.x, r3.y - r1.y, r3.z - r1.z, r3.w - r1.w); // top
        set_plane(f.planes[4], r2.x, r2.y, r2.z, r2.w);                               // near
        set_plane(f.planes[5], r3.x - r2.x, r3.y - r2.y, r3.z - r2.z, r3.w - r2.w); // far
        return f;
    }

    bool intersects(const Aabb& box) const {
        if (!box.valid()) {
            return true;
        }
        for (const Plane& plane : planes) {
            const Vec3 p{
                plane.normal.x >= 0.f ? box.max.x : box.min.x,
                plane.normal.y >= 0.f ? box.max.y : box.min.y,
                plane.normal.z >= 0.f ? box.max.z : box.min.z,
            };
            if (plane.distance(p) < 0.f) {
                return false;
            }
        }
        return true;
    }
};

} // namespace engine::math
