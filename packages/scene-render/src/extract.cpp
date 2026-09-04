#include <engine/scene_render/extract.hpp>

#include <engine/assets/mesh.hpp>
#include <engine/core/log.hpp>
#include <engine/math/aabb.hpp>
#include <engine/math/vec3.hpp>
#include <engine/renderer/motion.hpp>

#include <cstdio>

namespace engine::scene_render {

namespace {

// One latch per map kind, the same shape as scene::warn_full: a material that
// names a texture the store cannot resolve names it again every frame, and an
// unlatched message at 60 Hz is noise to scroll past rather than a diagnostic.
void warn_unresolved_map(bool& latch, const char* what, engine::assets::TextureHandle handle) {
    if (latch) {
        return;
    }
    latch = true;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Material %s map handle id=%llu gen=%u is not in the texture store - drawing the built-in "
        "default instead. Either it was never uploaded, or it was unloaded while referenced.",
        what, static_cast<unsigned long long>(handle.id), handle.generation);
    engine::log(engine::LogLevel::Warn, engine::LogChannel::Assets, message);
}

bool g_albedo_unresolved_warned = false;
bool g_normal_unresolved_warned = false;
bool g_mr_unresolved_warned = false;

} // namespace

void extract_lighting(const engine::scene::World& world, engine::math::Vec3 camera_pos,
    engine::renderer::Lighting& out) {
    const engine::math::Vec3 sun_dir = world.sun.direction.normalized();
    out.sun_direction = sun_dir;
    out.sun_color = world.sun.color;
    out.ambient = world.ambient;
    out.camera_pos = camera_pos;
    // scene and renderer each declare their own kMaxPointLights. This loop is
    // the one place both are visible, and it indexes renderer-sized arrays
    // with the scene's bound - so if they ever diverge it is a buffer overrun
    // with nothing to catch it. Couple them here.
    static_assert(engine::scene::kMaxPointLights == engine::renderer::kMaxPointLights,
        "scene::kMaxPointLights and renderer::kMaxPointLights must agree - "
        "the extract below writes scene lights into renderer-sized arrays");

    // Same class of bug as the light counts above: extract indexes
    // MotionHistory by scene instance id, so a scene that can hold more
    // instances than there are history slots silently loses motion vectors
    // for the overflow.
    static_assert(engine::scene::kMaxInstances <= engine::renderer::motion::kHistorySlots,
        "motion::kHistorySlots must cover scene::kMaxInstances - otherwise instances past "
        "the slot count get prev_model == model and TAA reprojects them wrongly");

    for (engine::u32 i = 0; i < engine::scene::kMaxPointLights; ++i) {
        const auto& light = world.points[i];
        out.point_pos_radius[i]
            = {light.position.x, light.position.y, light.position.z, light.radius};
        out.point_color_intensity[i]
            = {light.color.x, light.color.y, light.color.z, light.intensity};
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
    // Arena, not the stack. This used to be
    // `ExtractInstance storage[kMaxInstances]{}` - 168 bytes each, brace-
    // initialized, on the stack of a function called every frame. At a 512
    // instance cap that is 86 KiB memset per frame, and it grows linearly with
    // the cap until it overflows the 1 MiB default stack with no log. The
    // arena already fails soft.
    engine::renderer::ExtractInstance* storage =
        arena.push_n<engine::renderer::ExtractInstance>(world.instance_count);
    if (!storage && world.instance_count > 0) {
        return {};  // arena exhausted; it logged why
    }
    engine::u32 count = 0;
    const engine::math::Vec3 kBoxColors[] = {
        {0.2f, 1.f, 0.35f},
        {1.f, 0.9f, 0.2f},
        {1.f, 0.45f, 0.15f},
        {0.95f, 0.3f, 0.9f},
        {0.25f, 0.85f, 1.f},
    };

    // Each map comes from the store the material's handle names, and falls back
    // to a built-in when the material names none. This used to be a two-branch
    // lookup into an array of demo husky textures indexed by `material.albedo`
    // - which is why a scene the engine did not ship with could not have
    // textures at all.
    //
    // Resolved per material, not per instance. A store lookup is a
    // hash-and-probe and the answer varies only per material, of which there
    // are at most kMaxMaterials; three finds inside the instance loop below is
    // 3 x instance_count per extract - over a thousand at the current cap,
    // where an array index used to stand, and it grows with every cap raise
    // while this loop does not.
    auto resolve = [&assets](engine::assets::TextureHandle handle,
                       engine::rhi::ITexture* fallback, bool& latch, const char* what) {
        engine::rhi::ITexture* found = assets.textures ? assets.textures->get(handle) : nullptr;
        if (found) {
            return found;
        }
        // `valid() && !found` is exactly "named a map that is not there".
        // GpuTextureStore::get answers null to three different questions - the
        // material names nothing (the intended case), the handle was unloaded,
        // or it belongs to some other store - and the fallback flattens all
        // three, so this is the only place the difference can still be seen.
        // It matters more than it looks: before materials carried their own
        // handles, an unresolvable albedo left item.texture null and
        // renderer/extract.cpp culled the draw, so a broken texture made the
        // object vanish. White is the better default, but it removed the only
        // signal there was, and this warning is what replaces it.
        if (handle.valid()) {
            warn_unresolved_map(latch, what, handle);
        }
        return fallback;
    };

    struct ResolvedMaps {
        engine::rhi::ITexture* albedo = nullptr;
        engine::rhi::ITexture* normal = nullptr;
        engine::rhi::ITexture* mr = nullptr;
    };
    const engine::u32 material_count = world.material_count < engine::scene::kMaxMaterials
                                           ? world.material_count
                                           : engine::scene::kMaxMaterials;
    ResolvedMaps maps[engine::scene::kMaxMaterials]{};
    for (engine::u32 m = 0; m < material_count; ++m) {
        const auto& material = world.materials[m];
        maps[m].albedo = resolve(material.albedo, assets.default_albedo,
            g_albedo_unresolved_warned, "albedo");
        maps[m].normal = resolve(material.normal, assets.default_normal,
            g_normal_unresolved_warned, "normal");
        maps[m].mr = resolve(material.metallic_roughness, assets.default_mr,
            g_mr_unresolved_warned, "metallic-roughness");
    }

    for (engine::u32 i = 0; i < world.instance_count; ++i) {
        const auto& instance = world.instances[i];
        if (instance.material >= material_count) {
            continue;
        }
        const auto& material = world.materials[instance.material];
        const auto& resolved = maps[instance.material];
        const auto* mesh = assets.meshes ? assets.meshes->get(instance.mesh) : nullptr;
        if (!mesh) {
            continue;
        }

        auto& item = storage[count];
        // The one line that routes a material to a pipeline. Falls back to
        // the opaque one when the transparent pipeline is absent, so a build
        // that failed to create it draws a visibly wrong material instead of
        // making the object disappear. run_pipeline_set_gate makes that
        // unreachable; the fallback costs a `?:` and removes the class.
        const bool translucent = material.opacity < 1.f
            && assets.pipelines.forward_transparent != nullptr;
        item.pipeline = translucent ? assets.pipelines.forward_transparent
                                    : assets.pipelines.forward;
        item.vertex_buffer = mesh->vertex_buffer.get();
        item.index_buffer = mesh->index_buffer.get();
        item.texture = resolved.albedo;
        item.metallic_roughness = resolved.mr;
        item.normal_map = resolved.normal;
        item.model = engine::scene::instance_world_model(world, i);
        item.local_bounds = mesh->bounds;
        item.debug_color = kBoxColors[i % 5];
        item.id = i;
        item.metallic = material.metallic;
        item.roughness = material.roughness;
        item.opacity = material.opacity;
        item.index_count = mesh->index_count;
        item.vertex_stride = sizeof(engine::assets::VertexPN);
        count += 1;
    }

    engine::renderer::ExtractDesc desc{};
    desc.view = world.camera.view;
    desc.projection = world.camera.projection;
    extract_lighting(world, camera_pos, desc.lighting);
    desc.sun_direction = world.sun.direction;
    desc.pipelines = assets.pipelines;
    desc.history = history;
    desc.sky_cubemap = assets.sky_cubemap;
    desc.taa_history = assets.taa_history;
    desc.ibl_irradiance = assets.ibl_irradiance;
    desc.ibl_prefilter = assets.ibl_prefilter;
    desc.ibl_brdf_lut = assets.ibl_brdf_lut;
    desc.width = snapshot.width;
    desc.height = snapshot.height;
    desc.aa_mode = assets.aa_mode;
    desc.exposure = assets.exposure;
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

} // namespace engine::scene_render
