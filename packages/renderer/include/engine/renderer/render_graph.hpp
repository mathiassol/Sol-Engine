#pragma once

#include <engine/core/types.hpp>
#include <engine/renderer/render_snapshot.hpp>
#include <engine/rhi/commands.hpp>
#include <engine/rhi/resources.hpp>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace engine::rhi {
class IDevice;
class ITexture;
}

namespace engine::renderer {

enum class Access : u8 {
    ColorWrite,
    DepthWrite,
    CopySrc,
    CopyDst,
    ShaderRead,
};

// Compute participates in ordering exactly as Graphics does - the reads and
// writes arrays below are kind-agnostic, so a compute pass is scheduled after
// whatever produced the resources it reads, and a missing producer is reported
// against it by name. What it cannot yet do is *write* a graph resource: that
// needs UAV textures on the RHI contract (ENGINE_MAP RHI #9). Until then a
// compute pass reads graph textures and writes buffers of its own, which is why
// bloom is still fullscreen triangles rather than dispatches.
enum class PassKind : u8 { Graphics, Copy, Compute };

struct ResourceHandle {
    u32 id = 0;
    bool valid() const { return id != 0; }
    bool operator==(ResourceHandle other) const { return id == other.id; }
};

struct ResourceRef {
    ResourceHandle handle{};
    Access access = Access::ColorWrite;
};

struct TransientDesc {
    std::string_view name;
    rhi::Format format = rhi::Format::RGBA8_UNORM;
    rhi::TextureUsage usage = rhi::TextureUsage::RenderTarget;
    // 0 means current swapchain extent (then divided by extent_div).
    u32 width = 0;
    u32 height = 0;
    u32 extent_div = 1;
};

struct RenderPassDesc {
    static constexpr u32 kMaxRefs = 4;

    std::string name;
    PassKind kind = PassKind::Graphics;
    ResourceRef writes[kMaxRefs]{};
    u32 write_count = 0;
    ResourceRef reads[kMaxRefs]{};
    u32 read_count = 0;
    ResourceHandle copy_src{};
    ResourceHandle copy_dst{};
    rhi::Color4 clear_color{};
    bool clear_color_target = false;
    bool clear_depth = false;
    std::function<bool(const RenderSnapshot&)> should_execute;
    std::function<void(PassContext&)> execute;
};

class RenderGraph {
public:
    ResourceHandle swapchain_color() const;
    ResourceHandle swapchain_depth() const;
    ResourceHandle create_transient(const TransientDesc& desc);
    ResourceHandle import_persistent(std::string_view name, rhi::Format format);
    void bind_persistent(ResourceHandle handle, rhi::ITexture* texture);
    ResourceHandle find_resource(std::string_view name) const;

    void add_pass(RenderPassDesc desc);
    bool compile();
    // Recreates transients before recording. Always presents, even if compile fails.
    void execute(rhi::IDevice& device, const RenderSnapshot& snapshot);
    void clear();
    bool compiled() const { return compiled_; }
    u32 pass_count() const;
    std::string_view pass_name(u32 index) const;

private:
    struct ResourceRecord {
        std::string name;
        bool imported = false;
        bool is_depth = false;
        rhi::Format format = rhi::Format::Unknown;
        rhi::TextureUsage usage = rhi::TextureUsage::RenderTarget;
        rhi::ResourceState state = rhi::ResourceState::Common;
        std::unique_ptr<rhi::ITexture> texture;
        rhi::ITexture* external = nullptr;
        u32 width = 0;
        u32 height = 0;
        u32 extent_div = 1;
    };

    rhi::ITexture* resolve(rhi::IDevice& device, ResourceHandle handle);
    void ensure_imported();
    void ensure_transients(rhi::IDevice& device);
    void transition_to(rhi::ICommandList& cmd, rhi::IDevice& device,
        ResourceHandle handle, rhi::ResourceState desired);
    rhi::ResourceState state_for(Access access) const;
    bool is_imported(ResourceHandle handle) const;

    std::vector<ResourceRecord> resources_;
    std::vector<RenderPassDesc> passes_;
    bool compiled_ = false;
    bool dirty_ = true;
    u32 allocated_width_ = 0;
    u32 allocated_height_ = 0;
    bool swapchain_in_common_ = true;
    // Passes already reported as skipped. Some predicates are false every frame
    // by design (AA defaults to Off), so an unlatched message would log at
    // 60 Hz - the mistake alloc_frame_memory and warn_physics_capacity both
    // avoid with a latch.
    std::vector<std::string> skip_reported_;
};

} // namespace engine::renderer
