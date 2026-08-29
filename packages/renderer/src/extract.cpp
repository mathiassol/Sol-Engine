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
    if (!draws) {
        // Arena exhausted (it logged why). Emitting nothing costs one frame;
        // continuing would write through null.
        return stats;
    }
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

    // ── Batch the survivors ──────────────────────────────────────────────
    //
    // After the cull, not before: batch membership depends on what survived,
    // so batches cannot be cached across frames. This is an O(n) group-by over
    // an array already sitting in the arena.
    //
    // One batch list serves all three passes. Building per-pass would let
    // shadow, forward and motion disagree about how instances are grouped -
    // and the motion pass draws with DepthTest::Equal, so if its geometry does
    // not rasterize identically to forward it silently writes nothing.
    if (draw_count > 0) {
        DrawBatch* batches = arena.push_n<DrawBatch>(draw_count);
        InstanceData* instances = arena.push_n<InstanceData>(draw_count);
        u32* draw_batch = arena.push_n<u32>(draw_count);
        if (batches && instances && draw_batch) {
            auto same_key = [](const DrawBatch& b, const DrawItem& d) {
                return b.pipeline == d.pipeline
                    && b.vertex_buffer == d.vertex_buffer
                    && b.index_buffer == d.index_buffer
                    && b.texture == d.texture
                    && b.metallic_roughness == d.metallic_roughness
                    && b.normal_map == d.normal_map
                    && b.index_count == d.index_count
                    && b.vertex_stride == d.vertex_stride;
            };

            // Group by key, not by adjacency. Merging only neighbours sounds
            // cheaper and is worthless in practice: the sandbox alternates
            // albedo between neighbouring instances, so run-length batching
            // produced 33 batches for 33 draws - no batching at all.
            //
            // Not a sort either. The key is made of pointers, and *ordering*
            // pointers makes batch composition depend on allocator addresses,
            // so it would vary run to run and any gate asserting batch counts
            // would be flaky. Pointers are only ever compared for equality
            // here; batch order is first-appearance order, which is scene
            // order, which is stable.
            u32 batch_count = 0;
            for (u32 i = 0; i < draw_count; ++i) {
                const DrawItem& draw = draws[i];
                u32 found = batch_count;
                for (u32 b = 0; b < batch_count; ++b) {
                    if (same_key(batches[b], draw)) {
                        found = b;
                        break;
                    }
                }
                if (found == batch_count) {
                    DrawBatch& fresh = batches[batch_count];
                    fresh.pipeline = draw.pipeline;
                    fresh.vertex_buffer = draw.vertex_buffer;
                    fresh.index_buffer = draw.index_buffer;
                    fresh.texture = draw.texture;
                    fresh.metallic_roughness = draw.metallic_roughness;
                    fresh.normal_map = draw.normal_map;
                    fresh.index_count = draw.index_count;
                    fresh.vertex_stride = draw.vertex_stride;
                    ++batch_count;
                }
                draw_batch[i] = found;
                batches[found].instance_count += 1;
            }

            // Lay the batches out contiguously, then fill each one's slice.
            // The instance array is therefore a permutation of `draws` grouped
            // by key - `draws` itself keeps scene order for everything else.
            u32 running = 0;
            for (u32 b = 0; b < batch_count; ++b) {
                batches[b].first_instance = running;
                running += batches[b].instance_count;
            }
            u32* cursor = arena.push_n<u32>(batch_count);
            if (cursor) {
                for (u32 b = 0; b < batch_count; ++b) {
                    cursor[b] = batches[b].first_instance;
                }
                for (u32 i = 0; i < draw_count; ++i) {
                    const DrawItem& draw = draws[i];
                    const u32 dst = cursor[draw_batch[i]]++;
                    instances[dst].model = draw.model;
                    instances[dst].prev_model = draw.prev_model;
                    instances[dst].material_params = {draw.metallic, draw.roughness, 0.f, 0.f};
                }
                out.batches = {batches, batch_count};
                out.instances = {instances, draw_count};
                stats.batches = batch_count;
            }
        }
        // If the arena could not fit them it already logged; out.batches stays
        // empty and the recorder draws nothing rather than drawing garbage.
    }
    if (desc.history) {
        desc.history->prev_view = desc.view;
        desc.history->prev_projection = desc.projection;
        desc.history->has_camera = true;
    }
    return stats;
}

} // namespace engine::renderer
