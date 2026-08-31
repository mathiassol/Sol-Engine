#pragma once

#include <engine/core/types.hpp>
#include <engine/math/vec3.hpp>
#include <engine/math/vec4.hpp>

namespace engine::renderer::bloom {

// Keep in sync with bloom_downsample.hlsl, bloom_upsample.hlsl, and tonemap.hlsl.
//
// Modern bloom is COD AW / Jimenez / Karis, not a single Gaussian on the
// whole frame. Threshold the bright parts, pyramid downsample, tent upsample
// that adds the matching down level (scatter), then add in HDR *before* ACES.
// Intensity is small because the add is linear HDR (sun ~4.8), not Unity's
// LDR overlay slider.
//
// Graphics fullscreen passes into RGBA16 ColorShaderResource transients.
// UAV / compute bloom waits on RHI texture UAVs.

inline constexpr u32 kMips = 5;
inline constexpr f32 kThreshold = 1.f;
inline constexpr f32 kSoftKnee = 0.5f;
inline constexpr f32 kIntensity = 0.06f;
inline constexpr f32 kClamp = 20.f;
inline constexpr bool kCompositeBeforeTonemap = true;

struct Constants {
    math::Vec4 texel_size{};
    math::Vec4 threshold{};
    math::Vec4 params{};
};

static_assert(sizeof(Constants) == 48);

inline f32 max3(f32 a, f32 b, f32 c) {
    const f32 ab = a > b ? a : b;
    return ab > c ? ab : c;
}

inline f32 clamp01_range(f32 v, f32 lo, f32 hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline f32 luma(math::Vec3 c) {
    return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
}

inline f32 karis_weight(math::Vec3 c) {
    return 1.f / (1.f + luma(c));
}

inline math::Vec3 karis_box(math::Vec3 a, math::Vec3 b, math::Vec3 c, math::Vec3 d) {
    const f32 wa = karis_weight(a);
    const f32 wb = karis_weight(b);
    const f32 wc = karis_weight(c);
    const f32 wd = karis_weight(d);
    const f32 w = wa + wb + wc + wd + 1e-5f;
    return (a * wa + b * wb + c * wc + d * wd) * (1.f / w);
}

inline math::Vec4 threshold_params(f32 thresh = kThreshold, f32 soft_knee = kSoftKnee) {
    const f32 knee = thresh * soft_knee + 1e-5f;
    return {thresh, thresh - knee, knee * 2.f, 0.25f / knee};
}

inline math::Vec3 apply_knee(math::Vec3 color, math::Vec4 thresh = threshold_params()) {
    const f32 br = max3(color.x, color.y, color.z);
    f32 rq = clamp01_range(br - thresh.y, 0.f, thresh.z);
    rq = rq * rq * thresh.w;
    const f32 s = (rq > br - thresh.x ? rq : br - thresh.x) / (br > 1e-5f ? br : 1e-5f);
    math::Vec3 out = color * s;
    const f32 m = max3(out.x, out.y, out.z);
    if (m > kClamp) {
        out = out * (kClamp / m);
    }
    return out;
}

// `exposure` is applied on the first mip only, because that is the one that
// reads scene_color; every later mip reads an already-exposed bloom level. The
// gating lives here rather than in the shader so a gate can assert it - the
// shader multiplies by params.y unconditionally.
//
// Thresholding after exposure is the point, not a side effect: kThreshold = 1
// then means "would clip on the sensor" rather than "brighter than 1.0 in
// absolute scene radiance regardless of camera", which is what it meant before
// exposure existed.
inline Constants make_downsample_constants(u32 src_width, u32 src_height, bool first_mip,
    f32 exposure = 1.f) {
    Constants out{};
    const f32 w = src_width > 0 ? static_cast<f32>(src_width) : 1.f;
    const f32 h = src_height > 0 ? static_cast<f32>(src_height) : 1.f;
    out.texel_size = {1.f / w, 1.f / h, 0.f, 0.f};
    out.threshold = threshold_params();
    out.params = {first_mip ? 1.f : 0.f, first_mip ? exposure : 1.f, 0.f, 0.f};
    return out;
}

inline Constants make_upsample_constants(u32 src_width, u32 src_height) {
    return make_downsample_constants(src_width, src_height, false);
}

} // namespace engine::renderer::bloom
