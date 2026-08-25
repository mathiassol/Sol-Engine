#pragma once

#include <engine/renderer/render_graph.hpp>

#include <functional>

namespace engine::renderer {

constexpr u32 kShadowMapSize = 1024;

struct StandardFrameDesc {
    u32 shadow_map_size = kShadowMapSize;
    f32 clear_r = 0.08f;
    f32 clear_g = 0.08f;
    f32 clear_b = 0.12f;
    f32 clear_a = 1.f;
    std::function<void(PassContext&)> draw_debug_lines;
    std::function<void(PassContext&)> draw_overlay;
    bool log_ready = true;
};

// Registers shadow → forward → motion → sky → bloom → TAA → tonemap → AA → debug_lines → overlay.
// New engine passes go here, not in the application.
bool setup_standard_frame(RenderGraph& graph, StandardFrameDesc desc = {});

} // namespace engine::renderer
