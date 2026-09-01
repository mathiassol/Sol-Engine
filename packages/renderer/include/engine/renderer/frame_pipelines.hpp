#pragma once

#include <engine/core/types.hpp>
#include <engine/rhi/resources.hpp>

namespace engine::renderer {

// Every pipeline a frame binds, in one place, carried by value from the app to
// the render graph. Before this existed the same 13 pointers were hand-copied
// through four structs under two different spellings, and a forgotten copy line
// disabled a pass with no diagnostic - 7 of the 16 should_execute predicates
// read null as "feature off".
//
// `forward` is here even though the renderer consumes it per-batch rather than
// per-frame (world_extract.cpp assigns DrawItem::pipeline). Uniformity is the
// point: the completeness gate covers it and it reads like its twelve siblings.
struct FramePipelines {
    rhi::IGraphicsPipeline* forward = nullptr;
    rhi::IGraphicsPipeline* shadow = nullptr;
    rhi::IGraphicsPipeline* sky = nullptr;
    rhi::IGraphicsPipeline* bloom_downsample = nullptr;
    rhi::IGraphicsPipeline* bloom_upsample = nullptr;
    rhi::IGraphicsPipeline* tonemap = nullptr;
    rhi::IGraphicsPipeline* tonemap_aces = nullptr;
    rhi::IGraphicsPipeline* fxaa = nullptr;
    rhi::IGraphicsPipeline* smaa_edge = nullptr;
    rhi::IGraphicsPipeline* smaa_weights = nullptr;
    rhi::IGraphicsPipeline* smaa_blend = nullptr;
    rhi::IGraphicsPipeline* motion = nullptr;
    rhi::IGraphicsPipeline* taa = nullptr;
};

// Pairs each field with a name, so the set can be iterated - which is what the
// completeness gate needs, and what a named-fields-only design cannot give
// without a second hand-maintained list.
struct FramePipelineEntry {
    rhi::IGraphicsPipeline* FramePipelines::*field;
    const char* name;
};

inline constexpr FramePipelineEntry kFramePipelines[] = {
    {&FramePipelines::forward, "forward"},
    {&FramePipelines::shadow, "shadow"},
    {&FramePipelines::sky, "sky"},
    {&FramePipelines::bloom_downsample, "bloom_downsample"},
    {&FramePipelines::bloom_upsample, "bloom_upsample"},
    {&FramePipelines::tonemap, "tonemap"},
    {&FramePipelines::tonemap_aces, "tonemap_aces"},
    {&FramePipelines::fxaa, "fxaa"},
    {&FramePipelines::smaa_edge, "smaa_edge"},
    {&FramePipelines::smaa_weights, "smaa_weights"},
    {&FramePipelines::smaa_blend, "smaa_blend"},
    {&FramePipelines::motion, "motion"},
    {&FramePipelines::taa, "taa"},
};

inline constexpr usize kFramePipelineCount
    = sizeof(kFramePipelines) / sizeof(kFramePipelines[0]);

// A field with no table entry cannot be filled by the creation loop, checked by
// the gate, or named in a log - it would fail in exactly the silent way this
// type exists to remove. A table entry with no field does not compile. This
// makes the remaining case a build error too. Same idiom as
// static_assert(sizeof(InstanceData) == 144) in render_snapshot.hpp, and the
// enum-plus-names assert in physics_cpu.cpp.
static_assert(sizeof(FramePipelines) == kFramePipelineCount * sizeof(void*),
    "every FramePipelines field needs exactly one kFramePipelines entry");

} // namespace engine::renderer
