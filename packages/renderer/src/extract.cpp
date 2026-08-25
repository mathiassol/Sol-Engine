#include <engine/renderer/extract.hpp>

#include <engine/math/frustum.hpp>
#include <engine/math/vec3.hpp>

#include <cmath>

namespace engine::renderer {

math::Mat4 make_sun_view_proj(math::Vec3 sun_direction, math::Aabb bounds) {
    if (!bounds.valid()) {
        bounds.min = {-4.f, 0.f, -4.f};
        bounds.max = {4.f, 2.f, 4.f};
    }
    const math::Vec3 center = bounds.center();
    const math::Vec3 extent = bounds.max - bounds.min;
    f32 radius = extent.length() * 0.5f;
    if (radius < 1.f) {
        radius = 1.f;
    }
    radius *= 1.15f;
    math::Vec3 to_sun = sun_direction.normalized();
    math::Vec3 up{0.f, 1.f, 0.f};
    if (std::abs(to_sun.dot(up)) > 0.95f) {
        up = {1.f, 0.f, 0.f};
    }
    const math::Vec3 eye = center + to_sun * (radius + 2.f);
    const math::Mat4 view = math::Mat4::look_at(eye, center, up);
    const math::Mat4 proj = math::Mat4::ortho(
        -radius, radius, -radius, radius, 0.5f, radius * 2.f + 4.f);
    return proj * view;
}

namespace {

bool instance_in_frustum(const math::Frustum& frustum, const math::Aabb& local_bounds,
    const math::Mat4& model) {
    if (!local_bounds.valid()) {
        return true;
    }
    return frustum.intersects(local_bounds.transformed(model));
}

} // namespace

ExtractStats extract_visible(const ExtractDesc& desc, Arena& arena, RenderSnapshot& out,
    DebugAabbSink* debug) {
    ExtractStats stats{};
    out.view = desc.view;
    out.projection = desc.projection;
    out.lighting = desc.lighting;
    out.shadow_pipeline = desc.shadow_pipeline;
    out.sky_pipeline = desc.sky_pipeline;
    out.bloom_downsample_pipeline = desc.bloom_downsample_pipeline;
    out.bloom_upsample_pipeline = desc.bloom_upsample_pipeline;
    out.tonemap_pipeline = desc.tonemap_pipeline;
    out.fxaa_pipeline = desc.fxaa_pipeline;
    out.smaa_edge_pipeline = desc.smaa_edge_pipeline;
    out.smaa_weights_pipeline = desc.smaa_weights_pipeline;
    out.smaa_blend_pipeline = desc.smaa_blend_pipeline;
    out.motion_pipeline = desc.motion_pipeline;
    out.taa_pipeline = desc.taa_pipeline;
    out.tonemap_aces_pipeline = desc.tonemap_aces_pipeline;
    out.sky_cubemap = desc.sky_cubemap;
    out.taa_history = desc.taa_history;
    out.ibl_irradiance = desc.ibl_irradiance;
    out.ibl_prefilter = desc.ibl_prefilter;
    out.ibl_brdf_lut = desc.ibl_brdf_lut;
    out.width = desc.width;
    out.height = desc.height;
    out.aa_mode = desc.aa_mode;
    out.taa_reset = desc.taa_reset;
    out.taa_odd = (desc.taa_sample & 1u) != 0;
    const bool smaa = desc.smaa_edge_pipeline && desc.smaa_weights_pipeline
        && desc.smaa_blend_pipeline;
    const bool taa_on = aa::effective_mode(desc.aa_mode, desc.fxaa_pipeline != nullptr, smaa,
        desc.taa_pipeline && desc.tonemap_aces_pipeline) == aa::Mode::Taa;
    out.taa_jitter = taa_on ? taa::jitter_ndc(desc.taa_sample, desc.width, desc.height)
                            : math::Vec2{};
    if (!taa_on) {
        out.taa_history = nullptr;
        out.taa_reset = true;
    }
    out.overlay_visible = desc.overlay_visible;
    out.debug_visible = desc.debug_visible;
    out.draws = {};
    out.sun_view_proj = make_sun_view_proj(desc.sun_direction, math::Aabb::empty());
    const math::Mat4 view_proj = desc.projection * desc.view;
    out.prev_view_proj = view_proj;
    if (desc.history && desc.history->has_camera) {
        out.prev_view_proj = desc.history->prev_projection * desc.history->prev_view;
    }

    if (debug && debug->clear) {
        debug->clear();
    }

    if (desc.instances.empty()) {
        return stats;
    }

    const math::Frustum frustum = math::Frustum::from_view_proj(desc.projection * desc.view);
    DrawItem* draws = arena.push_n<DrawItem>(desc.instances.size());
    u32 draw_count = 0;
    math::Aabb visible_bounds = math::Aabb::empty();

    for (const ExtractInstance& instance : desc.instances) {
        stats.considered += 1;
        const math::Aabb world_bounds = instance.local_bounds.transformed(instance.model);
        if (debug && debug->add_aabb && instance.local_bounds.valid()) {
            debug->add_aabb(world_bounds, instance.debug_color);
        }

        math::Mat4 prev_model = instance.model;
        if (desc.history && instance.id < motion::kHistorySlots
            && desc.history->has_model[instance.id]) {
            prev_model = desc.history->prev_model[instance.id];
        }
        if (desc.history && instance.id < motion::kHistorySlots) {
            desc.history->prev_model[instance.id] = instance.model;
            desc.history->has_model[instance.id] = true;
        }

        if (!instance_in_frustum(frustum, instance.local_bounds, instance.model)) {
            continue;
        }
        stats.visible += 1;
        if (instance.local_bounds.valid()) {
            visible_bounds.include(world_bounds);
        }
        if (!instance.pipeline || !instance.vertex_buffer || !instance.index_buffer
            || !instance.texture) {
            continue;
        }
        draws[draw_count].pipeline = instance.pipeline;
        draws[draw_count].vertex_buffer = instance.vertex_buffer;
        draws[draw_count].index_buffer = instance.index_buffer;
        draws[draw_count].texture = instance.texture;
        draws[draw_count].metallic_roughness = instance.metallic_roughness;
        draws[draw_count].normal_map = instance.normal_map;
        draws[draw_count].model = instance.model;
        draws[draw_count].prev_model = prev_model;
        draws[draw_count].metallic = instance.metallic;
        draws[draw_count].roughness = instance.roughness;
        draws[draw_count].index_count = instance.index_count;
        draws[draw_count].vertex_stride = instance.vertex_stride;
        draw_count += 1;
        stats.drawn += 1;
    }

    out.sun_view_proj = make_sun_view_proj(desc.sun_direction, visible_bounds);
    out.draws = {draws, draw_count};
    if (desc.history) {
        desc.history->prev_view = desc.view;
        desc.history->prev_projection = desc.projection;
        desc.history->has_camera = true;
    }
    return stats;
}

} // namespace engine::renderer
