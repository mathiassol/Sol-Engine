#pragma once

#include <engine/core/types.hpp>
#include <engine/math/mat4.hpp>
#include <engine/math/vec3.hpp>
#include <engine/math/vec4.hpp>
#include <engine/math/vec2.hpp>
#include <engine/renderer/aa.hpp>
#include <engine/renderer/taa.hpp>
#include <engine/rhi/commands.hpp>
#include <engine/rhi/resources.hpp>

#include <span>

namespace engine::rhi {
class IBuffer;
class IDevice;
class IGraphicsPipeline;
}

namespace engine::renderer {

constexpr u32 kMaxPointLights = 4;

struct FrameConstants {
    math::Mat4 view_proj{};
    math::Mat4 model{};
    math::Mat4 sun_view_proj{};
    math::Vec4 sun_direction{};
    math::Vec4 sun_color{};
    math::Vec4 ambient{};
    math::Vec4 camera_pos{};
    math::Vec4 point_pos_radius[kMaxPointLights]{};
    math::Vec4 point_color_intensity[kMaxPointLights]{};
    math::Vec4 material_params{};
};

static_assert(sizeof(math::Mat4) == 64);
static_assert(sizeof(math::Vec4) == 16);
static_assert(sizeof(FrameConstants) == 400);

struct ShadowConstants {
    math::Mat4 view_proj{};
    math::Mat4 model{};
};

static_assert(sizeof(ShadowConstants) == 128);

struct Lighting {
    math::Vec3 sun_direction{0.f, 1.f, 0.f};
    math::Vec3 sun_color{1.f, 1.f, 1.f};
    math::Vec3 ambient{0.18f, 0.19f, 0.22f};
    math::Vec3 camera_pos{};
    math::Vec4 point_pos_radius[kMaxPointLights]{};
    math::Vec4 point_color_intensity[kMaxPointLights]{};
};

// GPU pointers are valid for this frame only. Per-draw constants come from
// IDevice::alloc_frame_memory (a fenced upload ring), not a persistent CBV.
struct DrawItem {
    rhi::IGraphicsPipeline* pipeline = nullptr;
    rhi::IBuffer* vertex_buffer = nullptr;
    rhi::IBuffer* index_buffer = nullptr;
    rhi::ITexture* texture = nullptr;
    rhi::ITexture* metallic_roughness = nullptr;
    rhi::ITexture* normal_map = nullptr;
    math::Mat4 model = math::Mat4::identity();
    math::Mat4 prev_model = math::Mat4::identity();
    f32 metallic = 0.f;
    f32 roughness = 0.5f;
    u32 index_count = 0;
    u32 vertex_stride = 0;
};

struct RenderSnapshot {
    math::Mat4 view{};
    math::Mat4 projection{};
    math::Mat4 prev_view_proj{};
    math::Mat4 sun_view_proj{};
    Lighting lighting{};
    std::span<const DrawItem> draws;
    rhi::IGraphicsPipeline* shadow_pipeline = nullptr;
    rhi::IGraphicsPipeline* sky_pipeline = nullptr;
    rhi::IGraphicsPipeline* bloom_downsample_pipeline = nullptr;
    rhi::IGraphicsPipeline* bloom_upsample_pipeline = nullptr;
    rhi::IGraphicsPipeline* tonemap_pipeline = nullptr;
    rhi::IGraphicsPipeline* fxaa_pipeline = nullptr;
    rhi::IGraphicsPipeline* smaa_edge_pipeline = nullptr;
    rhi::IGraphicsPipeline* smaa_weights_pipeline = nullptr;
    rhi::IGraphicsPipeline* smaa_blend_pipeline = nullptr;
    rhi::IGraphicsPipeline* motion_pipeline = nullptr;
    rhi::IGraphicsPipeline* taa_pipeline = nullptr;
    rhi::IGraphicsPipeline* tonemap_aces_pipeline = nullptr;
    rhi::ITexture* sky_cubemap = nullptr;
    rhi::ITexture* taa_history = nullptr;
    rhi::ITexture* ibl_irradiance = nullptr;
    rhi::ITexture* ibl_prefilter = nullptr;
    rhi::ITexture* ibl_brdf_lut = nullptr;
    u32 width = 0;
    u32 height = 0;
    aa::Mode aa_mode = aa::kDefault;
    math::Vec2 taa_jitter{};
    bool taa_reset = true;
    bool taa_odd = false;
    bool overlay_visible = false;
    bool debug_visible = false;
};

struct PassContext {
    rhi::IDevice& device;
    rhi::ICommandList& cmd;
    const RenderSnapshot& snapshot;
    rhi::ITexture* shader_reads[4]{};
    u32 shader_read_count = 0;
};

void record_opaque_draws(PassContext& ctx);
void record_shadow_draws(PassContext& ctx);
void record_motion_draws(PassContext& ctx);
void record_sky(PassContext& ctx);
void record_bloom_downsample(PassContext& ctx, bool first_mip);
void record_bloom_upsample(PassContext& ctx);
void record_tonemap(PassContext& ctx);
void record_fxaa(PassContext& ctx);
void record_smaa_edge(PassContext& ctx);
void record_smaa_weights(PassContext& ctx);
void record_smaa_blend(PassContext& ctx);
void record_taa(PassContext& ctx);
void record_tonemap_aces(PassContext& ctx);

} // namespace engine::renderer
