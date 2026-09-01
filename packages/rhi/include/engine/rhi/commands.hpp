#pragma once

#include <engine/core/types.hpp>
#include <engine/rhi/resources.hpp>

#include <string_view>

namespace engine::rhi {

class IBuffer;
class ITexture;

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
    ITexture* color = nullptr;
    ITexture* depth = nullptr;
    // Where a multisampled `color` is resolved when the pass ends. Null means no
    // resolve. Declaring the intent rather than the mechanism lets each backend
    // use what it wants - a resolve at end-of-pass here, an attachment on the
    // render pass elsewhere - and keeps the graph declaring what it wants rather
    // than how.
    ITexture* resolve = nullptr;
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
    virtual void transition(ITexture& texture, ResourceState from, ResourceState to) = 0;
    virtual void transition(IBuffer& buffer, ResourceState from, ResourceState to) = 0;
    virtual void copy_texture(ITexture& src, ITexture& dst) = 0;
    virtual void copy_buffer(IBuffer& src, IBuffer& dst, usize size) = 0;

    virtual void set_viewport(u32 width, u32 height) = 0;
    virtual void set_pipeline(IGraphicsPipeline& pipeline) = 0;
    virtual void set_compute_pipeline(IComputePipeline& pipeline) = 0;
    virtual void set_vertex_buffer(u32 slot, IBuffer& buffer, u32 stride_bytes,
        usize offset_bytes = 0) = 0;
    virtual void set_index_buffer(IBuffer& buffer, usize offset_bytes = 0) = 0;
    virtual void set_constant_buffer(u32 slot, IBuffer& buffer, usize offset_bytes = 0) = 0;
    virtual void set_shader_resource(u32 slot, ITexture& texture) = 0;
    virtual void set_unordered_access(u32 slot, IBuffer& buffer) = 0;
    // A storage texture a compute pass writes. Declared on the graph as
    // Access::StorageWrite so the barrier and the ordering agree.
    virtual void set_unordered_access(u32 slot, ITexture& texture) = 0;
    // Bind a buffer as a shader-readable array, visible to every stage.
    //
    // Visible to every shader stage, where a sampled texture is not. That
    // asymmetry is real and portable - it comes from how each backend binds a
    // storage buffer versus a texture table - and shaders depend on it, so a
    // backend must preserve it. resources.hpp's binding contract has the
    // per-backend detail.
    virtual void set_structured_buffer(u32 slot, IBuffer& buffer, usize offset_bytes = 0) = 0;
    // Samplers are bound as static samplers on GraphicsPipelineDesc, not per
    // draw. There is deliberately no set_sampler: a dynamic sampler table has
    // no consumer, and a dead virtual is a trap for a second backend author,
    // who must implement it, discover it is never called, and reproduce the
    // stub. Add it back with the pass that actually needs it.
    virtual void draw(u32 vertex_count, u32 start_vertex = 0) = 0;
    // instance_count > 1 draws the geometry that many times; the shader reads
    // per-instance data with SV_InstanceID.
    //
    // There is deliberately no start_instance. D3D excludes StartInstanceLocation
    // from SV_InstanceID, Vulkan *includes* firstInstance in gl_InstanceIndex,
    // and Metal splits it into a separate [[base_instance]] input - so the same
    // shader would read different data on each backend. Pass a batch base
    // offset in the pass constant buffer instead; that behaves identically
    // everywhere.
    virtual void draw_indexed(u32 index_count, u32 start_index = 0, i32 base_vertex = 0,
        u32 instance_count = 1) = 0;
    virtual void dispatch(u32 group_count_x, u32 group_count_y = 1, u32 group_count_z = 1) = 0;

    // PIX-style GPU timeline. begin/end nest; set_marker is a tick with no duration.
    virtual void begin_event(std::string_view name) = 0;
    virtual void end_event() = 0;
    virtual void set_marker(std::string_view name) = 0;
    virtual u32 debug_event_depth() const = 0;
    virtual std::string_view debug_event_name() const = 0;
    virtual std::string_view last_debug_marker() const = 0;
};

// RAII begin_event / end_event. Safe to move; not copyable.
class GpuDebugEvent {
public:
    GpuDebugEvent(ICommandList& cmd, std::string_view name) : cmd_(&cmd) {
        cmd_->begin_event(name);
    }
    ~GpuDebugEvent() {
        if (cmd_) {
            cmd_->end_event();
        }
    }
    GpuDebugEvent(const GpuDebugEvent&) = delete;
    GpuDebugEvent& operator=(const GpuDebugEvent&) = delete;
    GpuDebugEvent(GpuDebugEvent&& other) noexcept : cmd_(other.cmd_) {
        other.cmd_ = nullptr;
    }
    GpuDebugEvent& operator=(GpuDebugEvent&&) = delete;

private:
    ICommandList* cmd_ = nullptr;
};

} // namespace engine::rhi
