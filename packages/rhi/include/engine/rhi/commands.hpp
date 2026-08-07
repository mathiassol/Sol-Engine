#pragma once

#include <engine/core/types.hpp>

namespace engine::rhi {

class IBuffer;

class IGraphicsPipeline {
public:
    virtual ~IGraphicsPipeline() = default;
};

struct Color4 {
    f32 r = 0.f;
    f32 g = 0.f;
    f32 b = 0.f;
    f32 a = 1.f;
};

struct RenderPassInfo {
    Color4 clear_color{};
    bool clear_color_target = true;
    bool clear_depth = true;
};

class ICommandList {
public:
    virtual ~ICommandList() = default;

    virtual void begin() = 0;
    virtual void end() = 0;

    virtual void begin_render_pass(const RenderPassInfo& info) = 0;
    virtual void end_render_pass() = 0;

    virtual void set_viewport(u32 width, u32 height) = 0;
    virtual void set_pipeline(IGraphicsPipeline& pipeline) = 0;
    virtual void set_vertex_buffer(u32 slot, IBuffer& buffer, u32 stride_bytes,
        usize offset_bytes = 0) = 0;
    virtual void set_index_buffer(IBuffer& buffer, usize offset_bytes = 0) = 0;
    virtual void set_constant_buffer(u32 slot, IBuffer& buffer, usize offset_bytes = 0) = 0;
    virtual void draw(u32 vertex_count, u32 start_vertex = 0) = 0;
    virtual void draw_indexed(u32 index_count, u32 start_index = 0, i32 base_vertex = 0) = 0;
};

} // namespace engine::rhi
