#pragma once

#include <engine/core/types.hpp>
#include <engine/math/vec2.hpp>
#include <engine/math/vec3.hpp>

#include <vector>

namespace engine::renderer::ibl {

// Karis / UE / glTF split-sum IBL. Keep in sync with forward.hlsl.
//
// Units: this engine is unitless RGB (sun ~4.8, ACES). Do NOT copy Filament's
// default 30000 lux — that is photometric and is why IBL looked insane in
// engines that mixed lux with RGB lights. Intensity is 1. The IBL sky has no
// sun disk (the directional sun is separate; the visible disk is skybox-only).
// Dielectric F0 stays 0.04.
//
// Diffuse: cosine-weighted irradiance cube, shader multiplies by (1-metallic)*albedo.
// Specular: GGX-prefiltered cube (lod = perceptual roughness * max lod) * BRDF LUT.

inline constexpr f32 kIntensity = 1.f;
inline constexpr u32 kRadianceSize = 32;
inline constexpr u32 kIrradianceSize = 16;
inline constexpr u32 kPrefilterSize = 32;
inline constexpr u32 kPrefilterMips = 5; // 32,16,8,4,2
inline constexpr u32 kLutSize = 128;
inline constexpr f32 kMaxLod = static_cast<f32>(kPrefilterMips - 1);
// Sharp source cube for the visible sky (and a future HDRI swap). Not GGX mips.
inline constexpr u32 kSourceSize = 128;

struct Baked {
    std::vector<u16> source_rgba16;     // 128^2 * 6 * 4, mip 0 only
    std::vector<u16> irradiance_rgba16; // 16^2 * 6 * 4
    std::vector<u16> prefilter_rgba16;  // face-major, mip chain per face
    std::vector<u16> lut_rgba16;        // 128^2 * 4, RG = scale,bias
};

math::Vec3 sky_radiance(math::Vec3 direction);
math::Vec2 integrate_brdf(f32 nov, f32 perceptual_roughness, u32 sample_count);
f32 lod_from_roughness(f32 perceptual_roughness);
Baked bake();

} // namespace engine::renderer::ibl
