#pragma once

#include <engine/core/types.hpp>
#include <engine/math/mat4.hpp>
#include <engine/math/vec3.hpp>
#include <engine/math/vec4.hpp>
#include <engine/math/vec2.hpp>
#include <engine/renderer/aa.hpp>
#include <engine/renderer/taa.hpp>
#include <engine/rhi/commands.hpp>
#include <engine/rhi/device.hpp>
#include <engine/rhi/resources.hpp>

#include <span>

namespace engine::rhi {
class IBuffer;
class IDevice;
class IGraphicsPipeline;
}

namespace engine::renderer {

constexpr u32 kMaxPointLights = 4;

// Index of the batch's first instance inside the frame's InstanceData array.
//
// Every pass constant buffer carries one. The alternative - offsetting the
// bound buffer per batch, or using StartInstanceLocation - is not portable:
// D3D excludes StartInstanceLocation from SV_InstanceID, Vulkan includes it in
// gl_InstanceIndex, and Metal splits it out again. A base in the constant
// buffer reads the same on all three.
struct InstanceBase {
    u32 value = 0;
    u32 pad0 = 0;
    u32 pad1 = 0;
    u32 pad2 = 0;
};
static_assert(sizeof(InstanceBase) == 16);

// One record per drawn instance, read from a structured buffer by the vertex
// shader. Shadow reads `model`, forward reads `model` + `material_params`,
// motion reads both matrices - one layout, three consumers, so they cannot
// disagree about what an instance is.
//
// Tightly packed on purpose: a StructuredBuffer element packs like a C struct,
// NOT like a cbuffer (where a float3 would eat a full 16-byte register). The
// static_assert below is what keeps the HLSL mirror honest.
struct InstanceData {
    math::Mat4 model = math::Mat4::identity();
    math::Mat4 prev_model = math::Mat4::identity();
    math::Vec4 material_params{};
};

static_assert(sizeof(math::Mat4) == 64);
static_assert(sizeof(math::Vec4) == 16);
static_assert(sizeof(InstanceData) == 144);

// A run of instances sharing everything the pipeline binds per draw. Anything
// bound per draw is necessarily part of the key - which is why the three
// textures are here: until bindless, two objects with different albedo cannot
// share a batch however identical their geometry.
struct DrawBatch {
    rhi::IGraphicsPipeline* pipeline = nullptr;
    rhi::IBuffer* vertex_buffer = nullptr;
    rhi::IBuffer* index_buffer = nullptr;
    rhi::ITexture* texture = nullptr;
    rhi::ITexture* metallic_roughness = nullptr;
    rhi::ITexture* normal_map = nullptr;
    u32 index_count = 0;
    u32 vertex_stride = 0;
    u32 first_instance = 0;
    u32 instance_count = 0;
};

struct FrameConstants {
    math::Mat4 view_proj{};
    math::Mat4 sun_view_proj{};
    math::Vec4 sun_direction{};
    math::Vec4 sun_color{};
    math::Vec4 ambient{};
    math::Vec4 camera_pos{};
    math::Vec4 point_pos_radius[kMaxPointLights]{};
    math::Vec4 point_color_intensity[kMaxPointLights]{};
    InstanceBase instance_base{};
};

// Was 400 with a per-draw model matrix and material_params embedded; both now
// live per instance.
static_assert(sizeof(FrameConstants) == 336);

struct ShadowConstants {
    math::Mat4 view_proj{};
    InstanceBase instance_base{};
};

static_assert(sizeof(ShadowConstants) == 80);

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
    // Batched view of the same draws, built by extract_visible after culling.
    // `instances` is indexed by DrawBatch::first_instance + SV_InstanceID.
    std::span<const DrawBatch> batches;
    std::span<const InstanceData> instances;
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
    // Linear multiplier applied to scene_color at each of its first-read sites
    // (bloom's first downsample, tonemap, TAA) and never to bloom's output.
    f32 exposure = 1.f;
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
    // `snapshot.instances` uploaded into the frame ring, once for the whole
    // frame. Shadow, forward and motion all read the same bytes, so uploading
    // per pass would triple the ring cost of every instance and put the ceiling
    // back where instancing was supposed to move it from. Null buffer means the
    // upload failed (or there was nothing to upload) and geometry passes skip.
    rhi::FrameAllocation instances{};
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
