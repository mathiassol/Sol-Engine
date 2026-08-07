#pragma once

#include <engine/core/types.hpp>

#include <cmath>

namespace engine::math {

struct Vec2 {
    f32 x = 0.f;
    f32 y = 0.f;

    Vec2() = default;
    Vec2(f32 x, f32 y) : x(x), y(y) {}

    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(f32 s) const { return {x * s, y * s}; }

    f32 length() const { return std::sqrt(x * x + y * y); }
};

} // namespace engine::math
