#pragma once

#include <engine/core/types.hpp>
#include <engine/math/constants.hpp>
#include <engine/math/vec2.hpp>

#include <cmath>

namespace engine::renderer::pcf {

// Keep in sync with packages/sandbox/content/shaders/forward.hlsl.
//
// High-quality directional PCF on a comparison shadow map (D3D12 SampleCmp):
// each tap is already hardware 2x2 bilinear PCF. We add a 16-tap Vogel disk
// rotated per pixel with Jimenez interleaved-gradient noise so the kernel does
// not band. Fixed texel radius — PCSS / DPCF blocker search is a later map row.
// Do not treat reasarch/GRAPICS-RESEARCH.md as the spec.

inline constexpr int kTapCount = 16;
inline constexpr f32 kRadiusTexels = 3.f;
inline constexpr f32 kConstantBias = 0.0015f;
inline constexpr f32 kGoldenAngle = 2.399963229728653f; // 2 pi / phi^2

inline f32 interleaved_gradient_noise(math::Vec2 pixel) {
    const f32 n = 52.9829189f
        * std::fmod(pixel.x * 0.06711056f + pixel.y * 0.00583715f, 1.f);
    f32 frac = n - std::floor(n);
    if (frac < 0.f) {
        frac += 1.f;
    }
    return frac;
}

inline math::Vec2 vogel_disk(int i, int n, f32 phi) {
    const f32 r = std::sqrt((static_cast<f32>(i) + 0.5f) / static_cast<f32>(n));
    const f32 theta = static_cast<f32>(i) * kGoldenAngle + phi;
    return {std::cos(theta) * r, std::sin(theta) * r};
}

// CPU stand-in for a hard shadow step at u=0.5. Used by --gates, not the GPU.
inline f32 filter_step_edge(math::Vec2 uv, math::Vec2 pixel, u32 map_size) {
    const f32 phi = interleaved_gradient_noise(pixel) * (2.f * math::kPi);
    const f32 texel = 1.f / static_cast<f32>(map_size);
    f32 sum = 0.f;
    for (int i = 0; i < kTapCount; ++i) {
        const math::Vec2 offset = vogel_disk(i, kTapCount, phi) * (kRadiusTexels * texel);
        sum += (uv.x + offset.x) < 0.5f ? 0.f : 1.f;
    }
    return sum / static_cast<f32>(kTapCount);
}

} // namespace engine::renderer::pcf
