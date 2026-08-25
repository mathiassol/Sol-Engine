#pragma once

#include <engine/core/types.hpp>
#include <engine/math/vec3.hpp>
#include <engine/math/vec4.hpp>

namespace engine::renderer::aa {

// Keep in sync with packages/sandbox/content/shaders/fxaa.hlsl, smaa_*.hlsl, taa.hlsl.
//
// One AA method at a time. Default is Off (no jitter). FXAA 3.11 is the cheap
// preset. SMAA 1x is the sharp spatial option. Karis TAA stays on F5 as an
// optional leftover, not the quality path. CMAA2 wants texture UAVs. MSAA is a
// hardware layer, not this slot. Do not stack TAA on SMAA/FXAA.
//
// Spatial AA runs on LDR after ACES, before F3/F4. TAA runs in HDR after bloom
// and before ACES (Karis / UE4). Bloom stays in HDR either way.

enum class Mode : u8 { Off = 0, Fxaa = 1, Smaa = 2, Taa = 3 };

inline constexpr Mode kDefault = Mode::Off;
inline constexpr Mode kLast = Mode::Taa;
inline constexpr u32 kModeCount = 4;
inline constexpr f32 kSmaaThreshold = 0.1f;
inline constexpr int kSmaaMaxSearch = 8;
inline constexpr f32 kSmaaLocalContrast = 2.f;
inline constexpr bool kAfterTonemap = true;
inline constexpr bool kTaaBeforeTonemap = true;
inline constexpr bool kStackSpatial = false;

struct Constants {
    math::Vec4 texel_size{};
};

static_assert(sizeof(Constants) == 16);

inline const char* mode_name(Mode mode) {
    switch (mode) {
    case Mode::Off:  return "OFF";
    case Mode::Fxaa: return "FXAA";
    case Mode::Smaa: return "SMAA";
    case Mode::Taa:  return "TAA";
    }
    return "OFF";
}

inline Mode next_mode(Mode mode) {
    const u32 i = (static_cast<u32>(mode) + 1u) % kModeCount;
    return static_cast<Mode>(i);
}

inline Mode effective_mode(Mode requested, bool fxaa_ready, bool smaa_ready, bool taa_ready) {
    if (requested == Mode::Taa && taa_ready) {
        return Mode::Taa;
    }
    if (requested == Mode::Taa && smaa_ready) {
        return Mode::Smaa;
    }
    if (requested == Mode::Fxaa && fxaa_ready) {
        return Mode::Fxaa;
    }
    if (requested == Mode::Smaa && smaa_ready) {
        return Mode::Smaa;
    }
    return Mode::Off;
}

inline f32 luma(math::Vec3 c) {
    return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
}

inline Constants make_constants(u32 width, u32 height) {
    Constants out{};
    const f32 w = width > 0 ? static_cast<f32>(width) : 1.f;
    const f32 h = height > 0 ? static_cast<f32>(height) : 1.f;
    out.texel_size = {1.f / w, 1.f / h, w, h};
    return out;
}

} // namespace engine::renderer::aa
