#pragma once

#include <engine/core/types.hpp>

#include <vector>

namespace engine::math {

// Number of mips in a full chain down to 1x1.
u32 mip_chain_length(u32 width, u32 height);

// Box-filter an RGBA8 image into a packed mip chain: mip 0 first, then each
// successive half-size level, tightly packed with no per-level alignment.
//
// `srgb` decides *what is being averaged*. Averaging encoded bytes is not
// averaging light: for a texel split half black, half white, the byte mean is
// 127 while the correct answer — average in linear, re-encode — is 188. That is
// a full stop of error in the mid-grey of every mip level, and it compounds
// down the chain.
//
// Alpha is averaged directly either way. sRGB formats transform RGB only, so
// gamma-correcting alpha would corrupt it.
//
// This lives in `math` rather than in the RHI backend for two reasons: it is
// pure pixel arithmetic with no graphics dependency, and a gate cannot assert
// a static function inside a backend translation unit.
std::vector<u8> build_rgba8_mip_chain(const void* top, u32 width, u32 height, u32 mip_count,
    bool srgb);

} // namespace engine::math
