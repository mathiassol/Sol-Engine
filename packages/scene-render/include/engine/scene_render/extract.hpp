#pragma once

#include <engine/assets/gpu/mesh_store.hpp>
#include <engine/core/arena.hpp>
#include <engine/debug/debug_lines.hpp>
#include <engine/math/vec3.hpp>
#include <engine/renderer/extract.hpp>
#include <engine/renderer/frame_pipelines.hpp>
#include <engine/renderer/render_snapshot.hpp>
#include <engine/rhi/commands.hpp>
#include <engine/rhi/resources.hpp>
#include <engine/scene/world.hpp>

namespace engine::scene_render {

constexpr engine::u32 kHuskyVariantCount = 4;
constexpr engine::u32 kFloorAlbedoIndex = 4;

struct WorldExtractAssets {
    engine::renderer::FramePipelines pipelines;
    engine::rhi::ITexture* taa_history = nullptr;
    engine::u32 taa_sample = 0;
    bool taa_reset = true;
    engine::renderer::aa::Mode aa_mode = engine::renderer::aa::kDefault;
    engine::f32 exposure = 1.f;
    engine::rhi::ITexture* sky_cubemap = nullptr;
    const engine::assets::gpu::GpuMeshStore* meshes = nullptr;
    engine::rhi::ITexture* husky_albedos[kHuskyVariantCount]{};
    engine::rhi::ITexture* floor_albedo = nullptr;
    engine::rhi::ITexture* default_mr = nullptr;
    engine::rhi::ITexture* default_normal = nullptr;
    engine::rhi::ITexture* ibl_irradiance = nullptr;
    engine::rhi::ITexture* ibl_prefilter = nullptr;
    engine::rhi::ITexture* ibl_brdf_lut = nullptr;
};

void extract_lighting(const engine::scene::World& world, engine::math::Vec3 camera_pos,
    engine::renderer::Lighting& out);

engine::math::Aabb scene_world_bounds(const engine::scene::World& world,
    const engine::assets::gpu::GpuMeshStore& meshes);

engine::renderer::ExtractStats extract_world(const engine::scene::World& world,
    engine::math::Vec3 camera_pos, const WorldExtractAssets& assets, bool overlay_visible,
    engine::debug::DebugLines* debug_lines, engine::Arena& arena,
    engine::renderer::RenderSnapshot& snapshot,
    engine::renderer::motion::MotionHistory* history = nullptr);

} // namespace engine::scene_render
