#pragma once

#include <engine/core/types.hpp>

#include <cmath>

namespace engine::math {

struct Vec3 {
    f32 x = 0.f;
    f32 y = 0.f;
    f32 z = 0.f;

    Vec3() = default;
    constexpr Vec3(f32 x, f32 y, f32 z) : x(x), y(y), z(z) {}

    Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(f32 s) const { return {x * s, y * s, z * s}; }
    Vec3 operator-() const { return {-x, -y, -z}; }

    Vec3& operator+=(Vec3 o) {
        x += o.x;
        y += o.y;
        z += o.z;
        return *this;
    }
    Vec3& operator-=(Vec3 o) {
        x -= o.x;
        y -= o.y;
        z -= o.z;
        return *this;
    }

    f32 dot(Vec3 o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(Vec3 o) const {
        return {
            y * o.z - z * o.y,
            z * o.x - x * o.z,
            x * o.y - y * o.x,
        };
    }
    f32 length() const { return std::sqrt(dot(*this)); }
    Vec3 normalized() const {
        f32 len = length();
        return len > 0.f ? *this * (1.f / len) : Vec3{};
    }
};

} // namespace engine::math
