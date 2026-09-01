#include <engine/renderer/standard_frame.hpp>

#include <cstdio>

#include <engine/core/log.hpp>
#include <engine/renderer/aa.hpp>
#include <engine/renderer/bloom.hpp>
#include <engine/renderer/frame_pipelines.hpp>
#include <engine/renderer/motion.hpp>
#include <engine/renderer/render_snapshot.hpp>
#include <engine/renderer/taa.hpp>

namespace engine::renderer {

bool setup_standard_frame(RenderGraph& graph, StandardFrameDesc desc) {
    graph.clear();

    const auto backbuffer = graph.swapchain_color();
    const auto depth = graph.swapchain_depth();
    const auto shadow = graph.create_transient({
        "shadow_depth",
        rhi::Format::D32_FLOAT,
        rhi::TextureUsage::DepthShaderResource,
        desc.shadow_map_size,
        desc.shadow_map_size,
    });
    const auto scene_color = graph.create_transient({
        "scene_color",
        rhi::Format::RGBA16_FLOAT,
        rhi::TextureUsage::ColorShaderResource,
    });
    const auto motion_vectors = graph.create_transient({
        "motion_vectors",
        motion::kFormat,
        rhi::TextureUsage::ColorShaderResource,
    });
    const auto ldr_color = graph.create_transient({
        "ldr_color",
        rhi::Format::RGBA8_UNORM,
        rhi::TextureUsage::ColorShaderResource,
    });
    const auto smaa_edges = graph.create_transient({
        "smaa_edges",
        rhi::Format::RGBA8_UNORM,
        rhi::TextureUsage::ColorShaderResource,
    });
    const auto smaa_blend = graph.create_transient({
        "smaa_blend",
        rhi::Format::RGBA8_UNORM,
        rhi::TextureUsage::ColorShaderResource,
    });
    const auto taa_a = graph.import_persistent("taa_history_a", rhi::Format::RGBA16_FLOAT);
    const auto taa_b = graph.import_persistent("taa_history_b", rhi::Format::RGBA16_FLOAT);

    ResourceHandle bloom_down[bloom::kMips]{};
    ResourceHandle bloom_up[bloom::kMips - 1]{};
    static const char* kDownNames[] = {
        "bloom_down0", "bloom_down1", "bloom_down2", "bloom_down3", "bloom_down4",
    };
    static const char* kUpNames[] = {
        "bloom_up0", "bloom_up1", "bloom_up2", "bloom_up3",
    };
    static_assert(sizeof(kDownNames) / sizeof(kDownNames[0]) == bloom::kMips);
    static_assert(sizeof(kUpNames) / sizeof(kUpNames[0]) == bloom::kMips - 1);
    for (u32 i = 0; i < bloom::kMips; ++i) {
        bloom_down[i] = graph.create_transient({
            kDownNames[i],
            rhi::Format::RGBA16_FLOAT,
            rhi::TextureUsage::ColorShaderResource,
            0,
            0,
            1u << (i + 1),
        });
    }
    for (u32 i = 0; i < bloom::kMips - 1; ++i) {
        bloom_up[i] = graph.create_transient({
            kUpNames[i],
            rhi::Format::RGBA16_FLOAT,
            rhi::TextureUsage::ColorShaderResource,
            0,
            0,
            1u << (i + 1),
        });
    }

    RenderPassDesc shadow_pass{};
    shadow_pass.name = "shadow";
    shadow_pass.writes[0] = {shadow, Access::DepthWrite};
    shadow_pass.write_count = 1;
    shadow_pass.clear_depth = true;
    shadow_pass.execute = record_shadow_draws;
    graph.add_pass(std::move(shadow_pass));

    RenderPassDesc forward{};
    forward.name = "forward";
    forward.writes[0] = {scene_color, Access::ColorWrite};
    forward.writes[1] = {depth, Access::DepthWrite};
    forward.write_count = 2;
    forward.reads[0] = {shadow, Access::ShaderRead};
    forward.read_count = 1;
    forward.clear_color = {desc.clear_r, desc.clear_g, desc.clear_b, desc.clear_a};
    forward.clear_color_target = true;
    forward.clear_depth = true;
    forward.execute = record_opaque_draws;
    graph.add_pass(std::move(forward));

    RenderPassDesc velocity{};
    velocity.name = "motion_vectors";
    velocity.writes[0] = {motion_vectors, Access::ColorWrite};
    velocity.writes[1] = {depth, Access::DepthWrite};
    velocity.write_count = 2;
    velocity.clear_color = {0.f, 0.f, 0.f, 1.f};
    velocity.clear_color_target = true;
    velocity.clear_depth = false;
    velocity.should_execute = [](const RenderSnapshot& snapshot) {
        return snapshot.pipelines.motion != nullptr;
    };
    velocity.execute = record_motion_draws;
    graph.add_pass(std::move(velocity));

    RenderPassDesc sky{};
    sky.name = "sky";
    sky.writes[0] = {scene_color, Access::ColorWrite};
    sky.writes[1] = {depth, Access::DepthWrite};
    sky.write_count = 2;
    sky.clear_color_target = false;
    sky.clear_depth = false;
    sky.should_execute = [](const RenderSnapshot& snapshot) {
        return snapshot.pipelines.sky != nullptr && snapshot.sky_cubemap != nullptr;
    };
    sky.execute = record_sky;
    graph.add_pass(std::move(sky));

    for (u32 i = 0; i < bloom::kMips; ++i) {
        const bool first = i == 0;
        RenderPassDesc down{};
        down.name = kDownNames[i];
        down.writes[0] = {bloom_down[i], Access::ColorWrite};
        down.write_count = 1;
        down.reads[0] = {first ? scene_color : bloom_down[i - 1], Access::ShaderRead};
        down.read_count = 1;
        down.clear_color_target = false;
        down.should_execute = [](const RenderSnapshot& snapshot) {
            return snapshot.pipelines.bloom_downsample != nullptr;
        };
        down.execute = [first](PassContext& ctx) {
            record_bloom_downsample(ctx, first);
        };
        graph.add_pass(std::move(down));
    }

    for (int i = static_cast<int>(bloom::kMips) - 2; i >= 0; --i) {
        RenderPassDesc up{};
        up.name = kUpNames[i];
        up.writes[0] = {bloom_up[i], Access::ColorWrite};
        up.write_count = 1;
        const ResourceHandle low = (i + 1 == static_cast<int>(bloom::kMips) - 1)
            ? bloom_down[i + 1]
            : bloom_up[i + 1];
        up.reads[0] = {low, Access::ShaderRead};
        up.reads[1] = {bloom_down[i], Access::ShaderRead};
        up.read_count = 2;
        up.clear_color_target = false;
        up.should_execute = [](const RenderSnapshot& snapshot) {
            return snapshot.pipelines.bloom_upsample != nullptr;
        };
        up.execute = record_bloom_upsample;
        graph.add_pass(std::move(up));
    }

    auto snapshot_aa = [](const RenderSnapshot& snapshot) {
        const bool smaa = snapshot.pipelines.smaa_edge && snapshot.pipelines.smaa_weights
            && snapshot.pipelines.smaa_blend;
        const bool taa = snapshot.pipelines.taa && snapshot.pipelines.tonemap_aces;
        return aa::effective_mode(snapshot.aa_mode, snapshot.pipelines.fxaa != nullptr, smaa, taa);
    };
    auto taa_on = [snapshot_aa](const RenderSnapshot& snapshot) {
        return snapshot_aa(snapshot) == aa::Mode::Taa;
    };

    RenderPassDesc taa_even{};
    taa_even.name = "taa_even";
    taa_even.writes[0] = {taa_a, Access::ColorWrite};
    taa_even.write_count = 1;
    taa_even.reads[0] = {scene_color, Access::ShaderRead};
    taa_even.reads[1] = {bloom_up[0], Access::ShaderRead};
    taa_even.reads[2] = {motion_vectors, Access::ShaderRead};
    taa_even.read_count = 3;
    taa_even.clear_color_target = false;
    taa_even.should_execute = [taa_on](const RenderSnapshot& snapshot) {
        return taa_on(snapshot) && !snapshot.taa_odd;
    };
    taa_even.execute = record_taa;
    graph.add_pass(std::move(taa_even));

    RenderPassDesc taa_odd{};
    taa_odd.name = "taa_odd";
    taa_odd.writes[0] = {taa_b, Access::ColorWrite};
    taa_odd.write_count = 1;
    taa_odd.reads[0] = {scene_color, Access::ShaderRead};
    taa_odd.reads[1] = {bloom_up[0], Access::ShaderRead};
    taa_odd.reads[2] = {motion_vectors, Access::ShaderRead};
    taa_odd.read_count = 3;
    taa_odd.clear_color_target = false;
    taa_odd.should_execute = [taa_on](const RenderSnapshot& snapshot) {
        return taa_on(snapshot) && snapshot.taa_odd;
    };
    taa_odd.execute = record_taa;
    graph.add_pass(std::move(taa_odd));

    RenderPassDesc tonemap_taa_even{};
    tonemap_taa_even.name = "tonemap_taa_even";
    tonemap_taa_even.writes[0] = {ldr_color, Access::ColorWrite};
    tonemap_taa_even.write_count = 1;
    tonemap_taa_even.reads[0] = {taa_a, Access::ShaderRead};
    tonemap_taa_even.read_count = 1;
    tonemap_taa_even.clear_color_target = false;
    tonemap_taa_even.should_execute = [taa_on](const RenderSnapshot& snapshot) {
        return taa_on(snapshot) && !snapshot.taa_odd;
    };
    tonemap_taa_even.execute = record_tonemap_aces;
    graph.add_pass(std::move(tonemap_taa_even));

    RenderPassDesc tonemap_taa_odd{};
    tonemap_taa_odd.name = "tonemap_taa_odd";
    tonemap_taa_odd.writes[0] = {ldr_color, Access::ColorWrite};
    tonemap_taa_odd.write_count = 1;
    tonemap_taa_odd.reads[0] = {taa_b, Access::ShaderRead};
    tonemap_taa_odd.read_count = 1;
    tonemap_taa_odd.clear_color_target = false;
    tonemap_taa_odd.should_execute = [taa_on](const RenderSnapshot& snapshot) {
        return taa_on(snapshot) && snapshot.taa_odd;
    };
    tonemap_taa_odd.execute = record_tonemap_aces;
    graph.add_pass(std::move(tonemap_taa_odd));

    RenderPassDesc tonemap{};
    tonemap.name = "tonemap";
    tonemap.writes[0] = {ldr_color, Access::ColorWrite};
    tonemap.write_count = 1;
    tonemap.reads[0] = {scene_color, Access::ShaderRead};
    tonemap.reads[1] = {bloom_up[0], Access::ShaderRead};
    tonemap.read_count = 2;
    tonemap.clear_color_target = false;
    tonemap.should_execute = [taa_on](const RenderSnapshot& snapshot) {
        return !taa_on(snapshot);
    };
    tonemap.execute = record_tonemap;
    graph.add_pass(std::move(tonemap));

    RenderPassDesc aa_copy{};
    aa_copy.name = "aa_copy";
    aa_copy.kind = PassKind::Copy;
    aa_copy.copy_src = ldr_color;
    aa_copy.copy_dst = backbuffer;
    aa_copy.should_execute = [snapshot_aa](const RenderSnapshot& snapshot) {
        const aa::Mode mode = snapshot_aa(snapshot);
        return mode == aa::Mode::Off || mode == aa::Mode::Taa;
    };
    graph.add_pass(std::move(aa_copy));

    RenderPassDesc fxaa{};
    fxaa.name = "fxaa";
    fxaa.writes[0] = {backbuffer, Access::ColorWrite};
    fxaa.write_count = 1;
    fxaa.reads[0] = {ldr_color, Access::ShaderRead};
    fxaa.read_count = 1;
    fxaa.clear_color_target = false;
    fxaa.should_execute = [snapshot_aa](const RenderSnapshot& snapshot) {
        return snapshot_aa(snapshot) == aa::Mode::Fxaa;
    };
    fxaa.execute = record_fxaa;
    graph.add_pass(std::move(fxaa));

    RenderPassDesc smaa_edge{};
    smaa_edge.name = "smaa_edge";
    smaa_edge.writes[0] = {smaa_edges, Access::ColorWrite};
    smaa_edge.write_count = 1;
    smaa_edge.reads[0] = {ldr_color, Access::ShaderRead};
    smaa_edge.read_count = 1;
    smaa_edge.clear_color = {0.f, 0.f, 0.f, 1.f};
    smaa_edge.clear_color_target = true;
    smaa_edge.should_execute = [snapshot_aa](const RenderSnapshot& snapshot) {
        return snapshot_aa(snapshot) == aa::Mode::Smaa;
    };
    smaa_edge.execute = record_smaa_edge;
    graph.add_pass(std::move(smaa_edge));

    RenderPassDesc smaa_weights{};
    smaa_weights.name = "smaa_weights";
    smaa_weights.writes[0] = {smaa_blend, Access::ColorWrite};
    smaa_weights.write_count = 1;
    smaa_weights.reads[0] = {smaa_edges, Access::ShaderRead};
    smaa_weights.read_count = 1;
    smaa_weights.clear_color = {0.f, 0.f, 0.f, 1.f};
    smaa_weights.clear_color_target = true;
    smaa_weights.should_execute = [snapshot_aa](const RenderSnapshot& snapshot) {
        return snapshot_aa(snapshot) == aa::Mode::Smaa;
    };
    smaa_weights.execute = record_smaa_weights;
    graph.add_pass(std::move(smaa_weights));

    RenderPassDesc smaa_blend_pass{};
    smaa_blend_pass.name = "smaa_blend";
    smaa_blend_pass.writes[0] = {backbuffer, Access::ColorWrite};
    smaa_blend_pass.write_count = 1;
    smaa_blend_pass.reads[0] = {ldr_color, Access::ShaderRead};
    smaa_blend_pass.reads[1] = {smaa_blend, Access::ShaderRead};
    smaa_blend_pass.read_count = 2;
    smaa_blend_pass.clear_color_target = false;
    smaa_blend_pass.should_execute = [snapshot_aa](const RenderSnapshot& snapshot) {
        return snapshot_aa(snapshot) == aa::Mode::Smaa;
    };
    smaa_blend_pass.execute = record_smaa_blend;
    graph.add_pass(std::move(smaa_blend_pass));

    RenderPassDesc debug_lines{};
    debug_lines.name = "debug_lines";
    debug_lines.writes[0] = {backbuffer, Access::ColorWrite};
    debug_lines.writes[1] = {depth, Access::DepthWrite};
    debug_lines.write_count = 2;
    debug_lines.clear_color_target = false;
    debug_lines.clear_depth = false;
    debug_lines.should_execute = [](const RenderSnapshot& snapshot) {
        return snapshot.debug_visible;
    };
    debug_lines.execute = [draw = std::move(desc.draw_debug_lines)](PassContext& ctx) {
        if (draw) {
            draw(ctx);
        }
    };
    graph.add_pass(std::move(debug_lines));

    RenderPassDesc overlay{};
    overlay.name = "stats_overlay";
    overlay.writes[0] = {backbuffer, Access::ColorWrite};
    overlay.write_count = 1;
    overlay.should_execute = [](const RenderSnapshot& snapshot) {
        return snapshot.overlay_visible;
    };
    overlay.execute = [draw = std::move(desc.draw_overlay)](PassContext& ctx) {
        if (draw) {
            draw(ctx);
        }
    };
    graph.add_pass(std::move(overlay));

    const bool compiled = graph.compile();
    if (desc.log_ready) {
        // shadow_map_size is the one field of StandardFrameDesc a knob can move
        // (the sandbox's r.quality / r.shadow_size), and until it appeared here
        // there was no way to observe which size the real graph was built with -
        // the shadow gate runs against its own probe graph and always reported
        // the default.
        char message[192];
        std::snprintf(message, sizeof(message),
            "Render graph ready (shadow %ux%u, forward, motion, sky, bloom, TAA, tonemap, "
            "AA, debug lines, overlay)",
            desc.shadow_map_size, desc.shadow_map_size);
        log(compiled ? LogLevel::Info : LogLevel::Error, LogChannel::Render,
            compiled ? message : "Render graph compile failed");
    }
    return compiled;
}

} // namespace engine::renderer
