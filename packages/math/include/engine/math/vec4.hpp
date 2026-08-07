#pragma once

#include <engine/core/types.hpp>

namespace engine::math {

struct Vec4 {
    f32 x = 0.f;
    f32 y = 0.f;
    f32 z = 0.f;
    f32 w = 0.f;

    Vec4() = default;
    Vec4(f32 x, f32 y, f32 z, f32 w) : x(x), y(y), z(z), w(w) {}

    f32 operator[](int i) const { return (&x)[i]; }
    f32& operator[](int i) { return (&x)[i]; }
};

} // namespace engine::math
