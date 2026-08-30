#pragma once

#include <engine/core/types.hpp>

namespace engine::math {

// The sRGB transfer function (IEC 61966-2-1), piecewise — not pow(x, 2.2).
//
// The two differ most in the darks, where sRGB is linear below 0.04045
// (decoding) / 0.0031308 (encoding). A pow approximation of linear 0.001 gives
// 0.0195 where the standard gives 12.92 * 0.001 = 0.01292 — over 50% too
// bright, in exactly the range shadow detail lives.
//
// This has to match `srgb_encode` / `srgb_decode` in
// packages/sandbox/content/shaders/common.hlsli. The curve necessarily exists
// twice, in C++ and in HLSL, the same way InstanceData's layout does; the
// colour-space gate compares the two through a compute readback rather than
// trusting them to stay in step.
//
// Hardware sRGB texture formats apply this on sample, before filtering. These
// functions exist for the paths the hardware does not cover: CPU mip
// generation, and the gate.
f32 srgb_to_linear(f32 encoded);
f32 linear_to_srgb(f32 linear);

// Same curve over the 0..255 byte range, which is what image data arrives as.
f32 srgb_byte_to_linear(u8 encoded);
u8 linear_to_srgb_byte(f32 linear);

} // namespace engine::math
