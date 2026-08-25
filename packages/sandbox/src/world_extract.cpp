#include "world_extract.hpp"

#include <engine/assets/mesh.hpp>
#include <engine/math/aabb.hpp>
#include <engine/math/vec3.hpp>

namespace sandbox {

void extract_lighting(const engine::scene::World& world, engine::math::Vec3 camera_pos,
    engine::renderer::Lighting& out) {
    const engine::math::Vec3 sun_dir = world.sun.direction.normalized();
    out.sun_direction = sun_dir;
    out.sun_color = world.sun.color;
    out.ambient = world.ambient;
    out.camera_pos = camera_pos;
    for (engine::u32 i = 0; i < engine::scene::kMaxPointLights; ++i) {
        const auto& light = world.points[i];
        out.point_pos_radius[i] = {light.position.x, light.position.y, light.position.z, light.radius};
        out.point_color_intensity[i] = {light.color.x, light.color.y, light.color.z, light.intensity};
    }
}

engine::math::Aabb scene_world_bounds(const engine::scene::World& world,
    const engine::assets::gpu::GpuMeshStore& meshes) {
    engine::math::Aabb bounds = engine::math::Aabb::empty();
    for (engine::u32 i = 0; i < world.instance_count; ++i) {
        const auto* mesh = meshes.get(world.instances[i].mesh);
        if (!mesh || !mesh->bounds.valid()) {
            continue;
        }
        bounds.include(mesh->bounds.transformed(engine::scene::instance_world_model(world, i)));
    }
    return bounds;
}

engine::renderer::ExtractStats extract_world(const engine::scene::World& world,
    engine::math::Vec3 camera_pos, const WorldExtractAssets& assets, bool overlay_visible,
    engine::debug::DebugLines* debug_lines, engine::Arena& arena,
    engine::renderer::RenderSnapshot& snapshot,
    engine::renderer::motion::MotionHistory* history) {
    engine::renderer::ExtractInstance storage[engine::scene::kMaxInstances]{};
    engine::u32 count = 0;
    const engine::math::Vec3 kBoxColors[] = {
        {0.2f, 1.f, 0.35f},
        {1.f, 0.9f, 0.2f},
        {1.f, 0.45f, 0.15f},
        {0.95f, 0.3f, 0.9f},
        {0.25f, 0.85f, 1.f},
    };

    for (engine::u32 i = 0; i < world.instance_count && count < engine::scene::kMaxInstances; ++i) {
        const auto& instance = world.instances[i];
        if (instance.material >= world.material_count) {
            continue;
        }
        const auto& material = world.materials[instance.material];
        const auto* mesh = assets.meshes ? assets.meshes->get(instance.mesh) : nullptr;
        if (!mesh) {
            continue;
        }

        engine::rhi::ITexture* texture = nullptr;
        if (material.albedo < kHuskyVariantCount) {
            texture = assets.husky_albedos[material.albedo];
        } else if (material.albedo == kFloorAlbedoIndex) {
            texture = assets.floor_albedo;
        }

        auto& item = storage[count];
        item.pipeline = assets.forward;
        item.vertex_buffer = mesh->vertex_buffer.get();
        item.index_buffer = mesh->index_buffer.get();
        item.texture = texture;
        item.metallic_roughness = assets.default_mr;
        item.normal_map = assets.default_normal;
        item.model = engine::scene::instance_world_model(world, i);
        item.local_bounds = mesh->bounds;
        item.debug_color = kBoxColors[i % 5];
        item.id = i;
        item.metallic = material.metallic;
        item.roughness = material.roughness;
        item.index_count = mesh->index_count;
        item.vertex_stride = sizeof(engine::assets::VertexPN);
        count += 1;
    }

    engine::renderer::ExtractDesc desc{};
    desc.view = world.camera.view;
    desc.projection = world.camera.projection;
    extract_lighting(world, camera_pos, desc.lighting);
    desc.sun_direction = world.sun.direction;
    desc.shadow_pipeline = assets.shadow;
    desc.sky_pipeline = assets.sky;
    desc.bloom_downsample_pipeline = assets.bloom_downsample;
    desc.bloom_upsample_pipeline = assets.bloom_upsample;
    desc.tonemap_pipeline = assets.tonemap;
    desc.fxaa_pipeline = assets.fxaa;
    desc.smaa_edge_pipeline = assets.smaa_edge;
    desc.smaa_weights_pipeline = assets.smaa_weights;
    desc.smaa_blend_pipeline = assets.smaa_blend;
    desc.motion_pipeline = assets.motion;
    desc.taa_pipeline = assets.taa;
    desc.tonemap_aces_pipeline = assets.tonemap_aces;
    desc.history = history;
    desc.sky_cubemap = assets.sky_cubemap;
    desc.taa_history = assets.taa_history;
    desc.ibl_irradiance = assets.ibl_irradiance;
    desc.ibl_prefilter = assets.ibl_prefilter;
    desc.ibl_brdf_lut = assets.ibl_brdf_lut;
    desc.width = snapshot.width;
    desc.height = snapshot.height;
    desc.aa_mode = assets.aa_mode;
    desc.taa_sample = assets.taa_sample;
    desc.taa_reset = assets.taa_reset;
    desc.overlay_visible = overlay_visible;
    desc.debug_visible = debug_lines != nullptr && debug_lines->visible();
    desc.instances = {storage, count};

    engine::renderer::DebugAabbSink sink{};
    engine::renderer::DebugAabbSink* sink_ptr = nullptr;
    if (desc.debug_visible && debug_lines) {
        sink.clear = [debug_lines]() { debug_lines->clear(); };
        sink.add_aabb = [debug_lines](const engine::math::Aabb& box, engine::math::Vec3 color) {
            debug_lines->add_aabb(box, color);
        };
        sink_ptr = &sink;
    }

    return engine::renderer::extract_visible(desc, arena, snapshot, sink_ptr);
}

} // namespace sandbox
