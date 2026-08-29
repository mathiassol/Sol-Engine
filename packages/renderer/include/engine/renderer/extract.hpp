#pragma once

#include <engine/core/arena.hpp>
#include <engine/math/aabb.hpp>
#include <engine/math/mat4.hpp>
#include <engine/math/vec3.hpp>
#include <engine/renderer/motion.hpp>
#include <engine/renderer/render_snapshot.hpp>

#include <functional>
#include <span>

namespace engine::renderer {

struct ExtractInstance {
    rhi::IGraphicsPipeline* pipeline = nullptr;
    rhi::IBuffer* vertex_buffer = nullptr;
    rhi::IBuffer* index_buffer = nullptr;
    rhi::ITexture* texture = nullptr;
    rhi::ITexture* metallic_roughness = nullptr;
    rhi::ITexture* normal_map = nullptr;
    math::Mat4 model = math::Mat4::identity();
    math::Aabb local_bounds{};
    math::Vec3 debug_color{0.2f, 1.f, 0.35f};
    u32 id = ~0u;
    f32 metallic = 0.f;
    f32 roughness = 0.5f;
    u32 index_count = 0;
    u32 vertex_stride = 0;
};

struct DebugAabbSink {
    std::function<void()> clear;
    std::function<void(const math::Aabb&, math::Vec3)> add_aabb;
};

struct ExtractDesc {
    math::Mat4 view{};
    math::Mat4 projection{};
    Lighting lighting{};
    math::Vec3 sun_direction{0.f, 1.f, 0.f};
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
    motion::MotionHistory* history = nullptr;
    rhi::ITexture* sky_cubemap = nullptr;
    rhi::ITexture* taa_history = nullptr;
    rhi::ITexture* ibl_irradiance = nullptr;
    rhi::ITexture* ibl_prefilter = nullptr;
    rhi::ITexture* ibl_brdf_lut = nullptr;
    u32 width = 0;
    u32 height = 0;
    aa::Mode aa_mode = aa::kDefault;
    u32 taa_sample = 0;
    bool taa_reset = true;
    bool overlay_visible = false;
    bool debug_visible = false;
    std::span<const ExtractInstance> instances;
};

struct ExtractStats {
    u32 considered = 0;
    u32 visible = 0;
    u32 drawn = 0;
    // Draw calls actually issued per geometry pass. `drawn / batches` is the
    // average instances collapsed into one call.
    u32 batches = 0;
};

math::Mat4 make_sun_view_proj(math::Vec3 sun_direction, math::Aabb bounds);

ExtractStats extract_visible(const ExtractDesc& desc, Arena& arena, RenderSnapshot& out,
    DebugAabbSink* debug = nullptr);

} // namespace engine::renderer
