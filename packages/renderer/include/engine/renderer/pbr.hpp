#pragma once

#include <engine/core/types.hpp>
#include <engine/math/constants.hpp>
#include <engine/math/vec3.hpp>

#include <cmath>

namespace engine::renderer::pbr {

// Filament / glTF metallic-roughness, analytic lights only.
// Keep in sync with packages/sandbox/content/shaders/forward.hlsl.
//
// Specular: Cook-Torrance with GGX D, Smith-GGX height-correlated V, Schlick F.
// Diffuse: Lambert / pi, energy-conserved with (1-F) and (1-metallic).
// Author roughness is perceptual; alpha = roughness^2 (Disney/UE/Filament).
// IBL (split-sum) lives in ibl.hpp + forward.hlsl; this file is punctual only.

inline constexpr f32 kMinPerceptualRoughness = 0.045f;
inline constexpr f32 kDielectricF0 = 0.04f;

inline f32 saturate(f32 x) {
    return x < 0.f ? 0.f : (x > 1.f ? 1.f : x);
}

inline math::Vec3 mul(math::Vec3 a, math::Vec3 b) {
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}

inline math::Vec3 lerp(math::Vec3 a, math::Vec3 b, f32 t) {
    return a * (1.f - t) + b * t;
}

inline f32 perceptual_to_alpha(f32 perceptual_roughness) {
    const f32 r = perceptual_roughness < kMinPerceptualRoughness
        ? kMinPerceptualRoughness
        : (perceptual_roughness > 1.f ? 1.f : perceptual_roughness);
    return r * r;
}

inline math::Vec3 f0_from_metal(math::Vec3 albedo, f32 metallic) {
    const f32 m = saturate(metallic);
    return lerp({kDielectricF0, kDielectricF0, kDielectricF0}, albedo, m);
}

inline math::Vec3 diffuse_color(math::Vec3 albedo, f32 metallic) {
    return albedo * (1.f - saturate(metallic));
}

// `alpha` is already perceptual^2.
inline f32 d_ggx(f32 noh, f32 alpha) {
    const f32 a2 = alpha * alpha;
    const f32 d = (noh * a2 - noh) * noh + 1.f;
    return a2 / (math::kPi * d * d);
}

inline f32 v_smith_ggx_correlated(f32 nov, f32 nol, f32 alpha) {
    const f32 a2 = alpha * alpha;
    const f32 ggxl = nov * std::sqrt((-nol * a2 + nol) * nol + a2);
    const f32 ggxv = nol * std::sqrt((-nov * a2 + nov) * nov + a2);
    return 0.5f / (ggxv + ggxl);
}

inline math::Vec3 f_schlick(f32 loh, math::Vec3 f0) {
    const f32 f = std::pow(1.f - saturate(loh), 5.f);
    return f0 + (math::Vec3{1.f, 1.f, 1.f} - f0) * f;
}

inline constexpr f32 lambert() {
    return 1.f / math::kPi;
}

// One punctual light. `radiance` is already color * intensity * attenuation.
inline math::Vec3 evaluate_punctual(math::Vec3 n, math::Vec3 v, math::Vec3 l,
    math::Vec3 albedo, f32 metallic, f32 perceptual_roughness, math::Vec3 radiance) {
    const f32 nol = saturate(n.dot(l));
    if (nol <= 0.f) {
        return {};
    }
    const f32 alpha = perceptual_to_alpha(perceptual_roughness);
    const math::Vec3 h = (v + l).normalized();
    const f32 nov = saturate(n.dot(v)) + 1e-5f;
    const f32 noh = saturate(n.dot(h));
    const f32 loh = saturate(l.dot(h));

    const math::Vec3 f0 = f0_from_metal(albedo, metallic);
    const math::Vec3 f = f_schlick(loh, f0);
    const f32 d = d_ggx(noh, alpha);
    const f32 vis = v_smith_ggx_correlated(nov, nol, alpha);
    const math::Vec3 fr = f * (d * vis);

    const math::Vec3 fd = mul(diffuse_color(albedo, metallic), math::Vec3{1.f, 1.f, 1.f} - f)
        * lambert();
    return mul(fd + fr, radiance) * nol;
}

} // namespace engine::renderer::pbr
