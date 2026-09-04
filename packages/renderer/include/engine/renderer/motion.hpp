#pragma once

#include <engine/core/types.hpp>
#include <engine/math/mat4.hpp>
#include <engine/math/vec2.hpp>
#include <engine/math/vec3.hpp>
#include <engine/math/vec4.hpp>
#include <engine/renderer/render_snapshot.hpp>
#include <engine/rhi/resources.hpp>

#include <cmath>

namespace engine::renderer::motion {

// Keep in sync with packages/sandbox/content/shaders/motion.hlsl.
//
// Per-pixel UV offset from the previous frame to this one. TAA / motion blur
// sample history at `curr_uv - motion`. Raster uses the same clip jitter as
// color so depth-equal coverage hits; stored UV deltas stay unjittered.
// Not stacked on SMAA.
//
// Format is RGBA16 because the RHI has no RG16 yet. BA unused.

inline constexpr rhi::Format kFormat = rhi::Format::RGBA16_FLOAT;
// Must be >= the scene's instance capacity. Extract indexes this array by the
// scene instance id, and an id past the end silently gets prev_model == model -
// which reads as "this object did not move", so TAA reprojects it wrongly and
// motion blur vanishes. No crash, no warning. scene-render static_asserts
// the coupling; run_instance_capacity_gate proves it at runtime.
inline constexpr u32 kHistorySlots = 512;
inline constexpr f32 kUvScaleX = 0.5f;
inline constexpr f32 kUvScaleY = -0.5f;

struct Constants {
    math::Mat4 view_proj{};
    math::Mat4 prev_view_proj{};
    math::Vec4 jitter{};
    InstanceBase instance_base{};
};

// Was 272 with model and prev_model embedded; both now come from the
// per-instance structured buffer, which is also what let this pass be batched.
static_assert(sizeof(Constants) == 160);

struct MotionHistory {
    math::Mat4 prev_view{};
    math::Mat4 prev_projection{};
    math::Mat4 prev_model[kHistorySlots]{};
    bool has_model[kHistorySlots]{};
    bool has_camera = false;
};

inline math::Vec4 clip_from_local(const math::Mat4& view_proj, const math::Mat4& model,
    math::Vec3 local) {
    const math::Vec3 world = model.transform_point(local);
    return {
        view_proj.cols[0].x * world.x + view_proj.cols[1].x * world.y
            + view_proj.cols[2].x * world.z + view_proj.cols[3].x,
        view_proj.cols[0].y * world.x + view_proj.cols[1].y * world.y
            + view_proj.cols[2].y * world.z + view_proj.cols[3].y,
        view_proj.cols[0].z * world.x + view_proj.cols[1].z * world.y
            + view_proj.cols[2].z * world.z + view_proj.cols[3].z,
        view_proj.cols[0].w * world.x + view_proj.cols[1].w * world.y
            + view_proj.cols[2].w * world.z + view_proj.cols[3].w,
    };
}

inline math::Vec2 ndc_to_uv(math::Vec2 ndc) {
    return {ndc.x * kUvScaleX + 0.5f, ndc.y * kUvScaleY + 0.5f};
}

inline math::Vec2 clip_to_uv(math::Vec4 clip) {
    if (std::abs(clip.w) < 1e-8f) {
        return {};
    }
    const f32 inv_w = 1.f / clip.w;
    return ndc_to_uv({clip.x * inv_w, clip.y * inv_w});
}

inline math::Vec2 screen_uv_motion(const math::Mat4& view_proj, const math::Mat4& model,
    const math::Mat4& prev_view_proj, const math::Mat4& prev_model, math::Vec3 local) {
    const math::Vec2 curr = clip_to_uv(clip_from_local(view_proj, model, local));
    const math::Vec2 prev = clip_to_uv(clip_from_local(prev_view_proj, prev_model, local));
    return curr - prev;
}

inline bool nearly_zero(math::Vec2 v, f32 eps = 1e-5f) {
    return v.x * v.x + v.y * v.y <= eps * eps;
}

} // namespace engine::renderer::motion
