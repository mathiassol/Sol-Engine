#pragma once

#include <engine/core/types.hpp>
#include <engine/math/mat4.hpp>
#include <engine/math/vec2.hpp>
#include <engine/math/vec3.hpp>
#include <engine/math/vec4.hpp>
#include <engine/renderer/ibl.hpp>

#include <cmath>

namespace engine::renderer::sky {

// Visible backdrop. Keep in sync with packages/sandbox/content/shaders/sky.hlsl.
//
// This is not IBL. Filament/UE generate a sharp skybox *and* a filtered
// reflection cube from the same source. Sampling GGX mips as the sky looks
// blurry and diverges from lighting. The live path samples a sharp source
// cubemap (baked from ibl::sky_radiance, swap-ready for HDRI) into HDR
// scene_color *before* ACES.
//
// Infinity: the vertex shader emits the *far* plane's depth, which is 0 under
// reversed-Z and 1 under standard Z - so the value is carried in `Constants`
// rather than written as a literal in the shader. It was a literal 1.0, which
// under this engine's reversed-Z is the near plane, and the sky consequently
// passed depth_closer_or_equal against every opaque fragment and overwrote it
// (S6). Depth test is closer-or-equal, depth write off.
// Sun disk is skybox-only (Filament showSun). IBL irradiance/prefilter stay
// disk-free so the directional sun (~4.8) is not double-counted. The disk
// is the HDR highlight bloom is meant to catch.

enum class Mode : u8 { Procedural = 0, Cubemap = 1 };

inline constexpr Mode kMode = Mode::Cubemap;
inline constexpr f32 kIntensity = ibl::kIntensity;
inline constexpr bool kSunDisk = true;
inline constexpr f32 kSunCorePower = 256.f;
inline constexpr f32 kSunGlowPower = 24.f;
inline constexpr f32 kSunCore = 6.f;
inline constexpr f32 kSunGlow = 0.35f;
inline constexpr u32 kCubemapSize = ibl::kSourceSize;

struct Constants {
    math::Mat4 inv_view{};
    math::Vec4 ndc_scale{}; // xy = 1/proj scale, zw = TAA jitter in NDC (0 if off)
    math::Vec4 sun_direction{};
    math::Vec4 sun_color{};
    // .x is the clip-space z the vertex shader emits. Its own named field, not
    // a spare w on one of the vectors above: this is the value that was wrong,
    // and a value smuggled into someone else's padding is a value nobody reads
    // when they go looking.
    math::Vec4 far_depth{};
};

static_assert(sizeof(Constants) == 128);

// `far_depth` has no default, deliberately. A default is how a caller silently
// gets the other convention's value - which is the exact shape of the bug this
// field exists to close. Pass rhi::far_depth(device.depth_convention()).
inline Constants make_constants(const math::Mat4& view, const math::Mat4& projection,
    f32 far_depth, math::Vec3 sun_direction = {0.f, 1.f, 0.f},
    math::Vec3 sun_color = {1.f, 1.f, 1.f}, math::Vec2 jitter_ndc = {}) {
    Constants out{};
    out.inv_view = math::Mat4::inverse_affine(view);
    out.ndc_scale = {1.f / projection.cols[0].x, 1.f / projection.cols[1].y, jitter_ndc.x,
        jitter_ndc.y};
    const math::Vec3 dir = sun_direction.normalized();
    out.sun_direction = {dir.x, dir.y, dir.z, 0.f};
    out.sun_color = {sun_color.x, sun_color.y, sun_color.z, 0.f};
    out.far_depth = {far_depth, 0.f, 0.f, 0.f};
    return out;
}

inline math::Vec3 direction_from_ndc(math::Vec2 ndc, const Constants& constants) {
    ndc.x -= constants.ndc_scale.z;
    ndc.y -= constants.ndc_scale.w;
    const math::Vec3 view_dir{ndc.x * constants.ndc_scale.x, ndc.y * constants.ndc_scale.y, -1.f};
    const math::Mat4& m = constants.inv_view;
    return math::Vec3{
        m.cols[0].x * view_dir.x + m.cols[1].x * view_dir.y + m.cols[2].x * view_dir.z,
        m.cols[0].y * view_dir.x + m.cols[1].y * view_dir.y + m.cols[2].y * view_dir.z,
        m.cols[0].z * view_dir.x + m.cols[1].z * view_dir.y + m.cols[2].z * view_dir.z,
    }.normalized();
}

inline math::Vec3 radiance(math::Vec3 direction) {
    return ibl::sky_radiance(direction);
}

inline math::Vec3 apply_sun_disk(math::Vec3 direction, math::Vec3 sky, math::Vec3 sun_direction,
    math::Vec3 sun_color) {
    if (!kSunDisk) {
        return sky;
    }
    const f32 mu = std::max(0.f, direction.normalized().dot(sun_direction.normalized()));
    const f32 core = std::pow(mu, kSunCorePower);
    const f32 glow = std::pow(mu, kSunGlowPower);
    return sky + sun_color * (core * kSunCore + glow * kSunGlow);
}

} // namespace engine::renderer::sky
