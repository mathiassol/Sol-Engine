#include <engine/math/srgb.hpp>

#include <algorithm>
#include <cmath>

namespace engine::math {
namespace {

constexpr f32 kDecodeThreshold = 0.04045f;
constexpr f32 kEncodeThreshold = 0.0031308f;
constexpr f32 kSlope = 12.92f;
constexpr f32 kOffset = 0.055f;
constexpr f32 kScale = 1.055f;

} // namespace

f32 srgb_to_linear(f32 encoded) {
    // Clamped rather than extrapolated: values outside 0..1 have no defined
    // meaning in an 8-bit encoded texture, and pow() of a negative is NaN.
    const f32 c = std::clamp(encoded, 0.f, 1.f);
    if (c <= kDecodeThreshold) {
        return c / kSlope;
    }
    return std::pow((c + kOffset) / kScale, 2.4f);
}

f32 linear_to_srgb(f32 linear) {
    const f32 c = std::clamp(linear, 0.f, 1.f);
    if (c <= kEncodeThreshold) {
        return c * kSlope;
    }
    return kScale * std::pow(c, 1.f / 2.4f) - kOffset;
}

f32 srgb_byte_to_linear(u8 encoded) {
    return srgb_to_linear(static_cast<f32>(encoded) / 255.f);
}

u8 linear_to_srgb_byte(f32 linear) {
    const f32 encoded = linear_to_srgb(linear);
    // Round, don't truncate. Truncating loses half a code value on every
    // mip level, which compounds down a 12-level chain.
    const f32 scaled = encoded * 255.f + 0.5f;
    return static_cast<u8>(std::clamp(scaled, 0.f, 255.f));
}

} // namespace engine::math
