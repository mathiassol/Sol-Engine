#pragma once

#include <engine/core/types.hpp>
#include <engine/math/mat4.hpp>
#include <engine/math/vec2.hpp>
#include <engine/math/vec3.hpp>
#include <engine/math/vec4.hpp>
#include <engine/rhi/resources.hpp>

#include <cmath>

namespace engine::renderer::taa {

// Keep in sync with packages/sandbox/content/shaders/taa.hlsl.
//
// Karis / UE4 temporal supersampling at native resolution. Jitter forward and
// sky rays. Motion UV deltas stay unjittered; motion raster uses the same
// clip jitter so coverage matches color. History is a persistent ping-pong
// RT, not a dying graph transient. Not stacked on SMAA.

inline constexpr rhi::Format kFormat = rhi::Format::RGBA16_FLOAT;
inline constexpr u32 kSampleCount = 8;
inline constexpr f32 kHistoryWeight = 0.95f;
inline constexpr f32 kBloomIntensity = 0.06f;
inline constexpr bool kBeforeTonemap = true;

struct Constants {
    math::Vec4 texel_size{};
    math::Vec4 params{};
    math::Vec4 jitter{};
};

static_assert(sizeof(Constants) == 48);

inline f32 radical_inverse(u32 base, u32 index) {
    f32 result = 0.f;
    f32 f = 1.f;
    while (index > 0) {
        f /= static_cast<f32>(base);
        result += f * static_cast<f32>(index % base);
        index /= base;
    }
    return result;
}

inline math::Vec2 jitter_ndc(u32 sample, u32 width, u32 height) {
    const u32 index = sample % kSampleCount + 1u;
    const f32 w = width > 0 ? static_cast<f32>(width) : 1.f;
    const f32 h = height > 0 ? static_cast<f32>(height) : 1.f;
    const f32 hx = radical_inverse(2, index) - 0.5f;
    const f32 hy = radical_inverse(3, index) - 0.5f;
    return {hx * 2.f / w, hy * -2.f / h};
}

inline math::Mat4 apply_jitter(math::Mat4 projection, math::Vec2 ndc) {
    for (int c = 0; c < 4; ++c) {
        projection.cols[c].x += ndc.x * projection.cols[c].w;
        projection.cols[c].y += ndc.y * projection.cols[c].w;
    }
    return projection;
}

// D3D: NDC y up, UV y down. Sample current at `uv - jitter_to_uv(ndc)`.
inline math::Vec2 jitter_to_uv(math::Vec2 ndc) {
    return {ndc.x * 0.5f, ndc.y * -0.5f};
}

inline math::Vec3 rgb_to_ycocg(math::Vec3 c) {
    return {
        0.25f * c.x + 0.5f * c.y + 0.25f * c.z,
        0.5f * c.x - 0.5f * c.z,
        -0.25f * c.x + 0.5f * c.y - 0.25f * c.z,
    };
}

inline math::Vec3 ycocg_to_rgb(math::Vec3 c) {
    return {c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z};
}

inline math::Vec3 clip_aabb(math::Vec3 aabb_min, math::Vec3 aabb_max, math::Vec3 p) {
    const math::Vec3 center = (aabb_max + aabb_min) * 0.5f;
    math::Vec3 extents = (aabb_max - aabb_min) * 0.5f;
    extents.x = extents.x < 1e-5f ? 1e-5f : extents.x;
    extents.y = extents.y < 1e-5f ? 1e-5f : extents.y;
    extents.z = extents.z < 1e-5f ? 1e-5f : extents.z;
    const math::Vec3 v = p - center;
    const math::Vec3 r = {
        std::abs(v.x / extents.x),
        std::abs(v.y / extents.y),
        std::abs(v.z / extents.z),
    };
    const f32 m = r.x > r.y ? (r.x > r.z ? r.x : r.z) : (r.y > r.z ? r.y : r.z);
    if (m > 1.f) {
        return center + v * (1.f / m);
    }
    return p;
}

inline math::Vec3 resolve_sample(math::Vec3 current, math::Vec3 history, math::Vec3 aabb_min,
    math::Vec3 aabb_max, f32 history_weight, bool reset) {
    if (reset) {
        return current;
    }
    const math::Vec3 clipped = ycocg_to_rgb(
        clip_aabb(aabb_min, aabb_max, rgb_to_ycocg(history)));
    const f32 a = history_weight < 0.f ? 0.f : (history_weight > 1.f ? 1.f : history_weight);
    return current * (1.f - a) + clipped * a;
}

inline bool nearly_zero(math::Vec2 v, f32 eps = 1e-8f) {
    return v.x * v.x + v.y * v.y <= eps * eps;
}

inline Constants make_constants(u32 width, u32 height, math::Vec2 jitter_ndc_offset, bool reset) {
    Constants out{};
    const f32 w = width > 0 ? static_cast<f32>(width) : 1.f;
    const f32 h = height > 0 ? static_cast<f32>(height) : 1.f;
    const math::Vec2 uv = jitter_to_uv(jitter_ndc_offset);
    out.texel_size = {1.f / w, 1.f / h, w, h};
    out.params = {reset ? 1.f : 0.f, kHistoryWeight, kBloomIntensity, 0.f};
    out.jitter = {uv.x, uv.y, jitter_ndc_offset.x, jitter_ndc_offset.y};
    return out;
}

} // namespace engine::renderer::taa
