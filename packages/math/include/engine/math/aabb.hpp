#pragma once

#include <engine/math/mat4.hpp>
#include <engine/math/vec3.hpp>

#include <algorithm>

namespace engine::math {

struct Aabb {
    Vec3 min{1.e30f, 1.e30f, 1.e30f};
    Vec3 max{-1.e30f, -1.e30f, -1.e30f};

    static Aabb empty() { return {}; }

    bool valid() const {
        return min.x <= max.x && min.y <= max.y && min.z <= max.z;
    }

    void include(Vec3 p) {
        min.x = std::min(min.x, p.x);
        min.y = std::min(min.y, p.y);
        min.z = std::min(min.z, p.z);
        max.x = std::max(max.x, p.x);
        max.y = std::max(max.y, p.y);
        max.z = std::max(max.z, p.z);
    }

    Aabb transformed(Mat4 m) const {
        Aabb out = empty();
        const Vec3 corners[8] = {
            {min.x, min.y, min.z}, {max.x, min.y, min.z},
            {min.x, max.y, min.z}, {max.x, max.y, min.z},
            {min.x, min.y, max.z}, {max.x, min.y, max.z},
            {min.x, max.y, max.z}, {max.x, max.y, max.z},
        };
        for (Vec3 corner : corners) {
            out.include(m.transform_point(corner));
        }
        return out;
    }

    void include(const Aabb& other) {
        if (!other.valid()) {
            return;
        }
        include(other.min);
        include(other.max);
    }

    Vec3 center() const {
        return {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f};
    }

    Vec3 half_extents() const {
        return {(max.x - min.x) * 0.5f, (max.y - min.y) * 0.5f, (max.z - min.z) * 0.5f};
    }

    static Aabb from_center_half(Vec3 center, Vec3 half) {
        return {{center.x - half.x, center.y - half.y, center.z - half.z},
            {center.x + half.x, center.y + half.y, center.z + half.z}};
    }

    Aabb expanded(f32 margin) const {
        const Vec3 m{margin, margin, margin};
        return {min - m, max + m};
    }

    Aabb united(const Aabb& other) const {
        Aabb out = *this;
        out.include(other);
        return out;
    }

    bool overlaps(const Aabb& other) const {
        return min.x <= other.max.x && max.x >= other.min.x
            && min.y <= other.max.y && max.y >= other.min.y
            && min.z <= other.max.z && max.z >= other.min.z;
    }

    bool contains(const Aabb& other) const {
        return min.x <= other.min.x && min.y <= other.min.y && min.z <= other.min.z
            && max.x >= other.max.x && max.y >= other.max.y && max.z >= other.max.z;
    }

    Vec3 closest_point(Vec3 p) const {
        return {
            std::clamp(p.x, min.x, max.x),
            std::clamp(p.y, min.y, max.y),
            std::clamp(p.z, min.z, max.z),
        };
    }

    f32 surface_area() const {
        const f32 dx = max.x - min.x;
        const f32 dy = max.y - min.y;
        const f32 dz = max.z - min.z;
        return 2.f * (dx * dy + dy * dz + dz * dx);
    }
};

} // namespace engine::math
