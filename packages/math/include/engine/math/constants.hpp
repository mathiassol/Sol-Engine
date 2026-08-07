#pragma once

#include <engine/core/types.hpp>

namespace engine::math {

inline constexpr f32 kPi        = 3.14159265358979323846f;
inline constexpr f32 kDegToRad  = kPi / 180.0f;
inline constexpr f32 kRadToDeg  = 180.0f / kPi;

inline f32 radians(f32 degrees) { return degrees * kDegToRad; }
inline f32 degrees(f32 radians) { return radians * kRadToDeg; }

} // namespace engine::math
