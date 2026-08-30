#include <engine/renderer/render_graph.hpp>

#include <engine/core/assert.hpp>
#include <engine/core/log.hpp>
#include <engine/renderer/aa.hpp>
#include <engine/renderer/bloom.hpp>
#include <engine/renderer/motion.hpp>
#include <engine/renderer/sky.hpp>
#include <engine/renderer/taa.hpp>
#include <engine/rhi/device.hpp>

#include <cstdio>
#include <unordered_set>

namespace engine::renderer {

namespace {

constexpr u32 kSwapchainColorId = 1;
constexpr u32 kSwapchainDepthId = 2;
constexpr u32 kFirstTransientId = 3;

const char* access_name(Access access) {
    switch (access) {
    case Access::ColorWrite: return "ColorWrite";
    case Access::DepthWrite: return "DepthWrite";
    case Access::CopySrc:    return "CopySrc";
    case Access::CopyDst:    return "CopyDst";
    case Access::ShaderRead: return "ShaderRead";
    }
    return "Unknown";
}

const char* format_name(rhi::Format format) {
    switch (format) {
    case rhi::Format::Unknown:     return "Unknown";
    case rhi::Format::RGBA8_UNORM: return "RGBA8_UNORM";
    case rhi::Format::RGBA16_FLOAT: return "RGBA16_FLOAT";
    case rhi::Format::D32_FLOAT:   return "D32_FLOAT";
    }
    return "Unknown";
}

bool pass_writes(const RenderPassDesc& pass, u32 resource_id) {
    if (pass.kind == PassKind::Copy) {
        return pass.copy_dst.id == resource_id;
    }
    for (u32 i = 0; i < pass.write_count; ++i) {
        if (pass.writes[i].handle.id == resource_id) {
            return true;
        }
    }
    return false;
}

bool pass_reads(const RenderPassDesc& pass, u32 resource_id) {
    if (pass.kind == PassKind::Copy) {
        return pass.copy_src.id == resource_id;
    }
    for (u32 i = 0; i < pass.read_count; ++i) {
        if (pass.reads[i].handle.id == resource_id) {
            return true;
        }
    }
    return false;
}

bool has_pass_cycle(const std::vector<RenderPassDesc>& passes, std::string& cycle_pass) {
    const u32 n = static_cast<u32>(passes.size());
    if (n < 2) {
        return false;
    }

    std::vector<std::vector<u32>> adj(n);
    std::vector<u32> indeg(n, 0);
    for (u32 i = 0; i < n; ++i) {
        for (u32 j = 0; j < n; ++j) {
            if (i == j) {
                continue;
            }
            bool edge = false;
            if (passes[i].kind == PassKind::Copy && pass_reads(passes[j], passes[i].copy_dst.id)) {
                edge = true;
            }
            for (u32 w = 0; !edge && w < passes[i].write_count; ++w) {
                edge = pass_reads(passes[j], passes[i].writes[w].handle.id);
            }
            if (edge) {
                adj[i].push_back(j);
                ++indeg[j];
            }
        }
    }

    std::vector<u32> queue;
    queue.reserve(n);
    for (u32 i = 0; i < n; ++i) {
        if (indeg[i] == 0) {
            queue.push_back(i);
        }
    }
    u32 seen = 0;
    for (usize qi = 0; qi < queue.size(); ++qi) {
        const u32 u = queue[qi];
        ++seen;
        for (u32 v : adj[u]) {
            if (--indeg[v] == 0) {
                queue.push_back(v);
            }
        }
    }
    if (seen == n) {
        return false;
    }
    for (u32 i = 0; i < n; ++i) {
        if (indeg[i] != 0) {
            cycle_pass = passes[i].name;
            break;
        }
    }
    return true;
}

// Upload the frame's per-instance array and hand back its slice. Called once
// per frame from `execute`, not once per pass: shadow, forward and motion read
// identical bytes, so a per-pass upload would cost 3x144 bytes of ring per
// drawn instance instead of 144 and undo most of what batching bought.
rhi::FrameAllocation upload_instances(rhi::IDevice& device,
    std::span<const InstanceData> instances) {
    if (instances.empty()) {
        return {};
    }
    const usize bytes = instances.size() * sizeof(InstanceData);
    const rhi::FrameAllocation slice = device.alloc_frame_memory(bytes);
    if (!slice.buffer) {
        return {};  // ring exhausted; it logged
    }
    device.write_buffer(*slice.buffer, slice.offset, instances.data(), bytes);
    return slice;
}

// One draw-recording skeleton for the three geometry passes. They differ only
// in the constants type, which pipeline they bind, and what extra resources go
// with each batch.
//
// Per *batch*, not per draw: the pass constants and the instance array carry
// what used to be re-uploaded for every object, and one draw_indexed covers
// the whole run. `pipeline` null means "use the pipeline on the batch" (the
// forward pass); otherwise every batch shares one pipeline (shadow, motion).
template <typename Constants, typename FillFn, typename BindFn>
void record_draws(PassContext& ctx, rhi::IGraphicsPipeline* pipeline, Constants& constants,
    FillFn&& fill, BindFn&& bind) {
    const rhi::FrameAllocation instance_slice = ctx.instances;
    if (!instance_slice.buffer) {
        return;  // nothing to draw, or the frame's upload failed
    }

    for (const DrawBatch& batch : ctx.snapshot.batches) {
        rhi::IGraphicsPipeline* pso = pipeline ? pipeline : batch.pipeline;
        if (!pso || !batch.vertex_buffer || !batch.index_buffer || batch.instance_count == 0) {
            continue;
        }

        constants.instance_base.value = batch.first_instance;
        fill(constants, batch);

        const rhi::FrameAllocation slice = ctx.device.alloc_frame_memory(sizeof(Constants));
        if (!slice.buffer) {
            // Ring full; it logs once per frame. Dropping a batch now costs a
            // group of objects rather than one, so it is louder than it was -
            // but still a recoverable frame rather than a dead process.
            continue;
        }

        ctx.device.write_buffer(*slice.buffer, slice.offset, &constants, sizeof(Constants));
        ctx.cmd.set_pipeline(*pso);
        ctx.cmd.set_constant_buffer(0, *slice.buffer, slice.offset);
        ctx.cmd.set_structured_buffer(0, *instance_slice.buffer, instance_slice.offset);
        bind(ctx, batch);
        ctx.cmd.set_vertex_buffer(0, *batch.vertex_buffer, batch.vertex_stride);
        ctx.cmd.set_index_buffer(*batch.index_buffer);
        ctx.cmd.draw_indexed(batch.index_count, 0, 0, batch.instance_count);
    }
}

} // namespace

void record_opaque_draws(PassContext& ctx) {
    FrameConstants constants{};
    math::Mat4 projection = ctx.snapshot.projection;
    if (!taa::nearly_zero(ctx.snapshot.taa_jitter)) {
        projection = taa::apply_jitter(projection, ctx.snapshot.taa_jitter);
    }
    constants.view_proj = projection * ctx.snapshot.view;
    constants.sun_view_proj = ctx.snapshot.sun_view_proj;
    const Lighting& lighting = ctx.snapshot.lighting;
    constants.sun_direction = {lighting.sun_direction.x, lighting.sun_direction.y,
        lighting.sun_direction.z, 0.f};
    constants.sun_color = {lighting.sun_color.x, lighting.sun_color.y, lighting.sun_color.z, 0.f};
    constants.ambient = {lighting.ambient.x, lighting.ambient.y, lighting.ambient.z, 0.f};
    constants.camera_pos = {lighting.camera_pos.x, lighting.camera_pos.y, lighting.camera_pos.z, 1.f};
    for (u32 i = 0; i < kMaxPointLights; ++i) {
        constants.point_pos_radius[i] = lighting.point_pos_radius[i];
        constants.point_color_intensity[i] = lighting.point_color_intensity[i];
    }
    rhi::ITexture* shadow = ctx.shader_read_count > 0 ? ctx.shader_reads[0] : nullptr;

    record_draws(ctx, nullptr, constants,
        [](FrameConstants&, const DrawBatch&) {},
        [shadow](PassContext& c, const DrawBatch& draw) {
            if (draw.texture) {
                c.cmd.set_shader_resource(0, *draw.texture);
            }
            if (shadow) {
                c.cmd.set_shader_resource(1, *shadow);
            }
            if (c.snapshot.ibl_irradiance) {
                c.cmd.set_shader_resource(2, *c.snapshot.ibl_irradiance);
            }
            if (c.snapshot.ibl_prefilter) {
                c.cmd.set_shader_resource(3, *c.snapshot.ibl_prefilter);
            }
            if (c.snapshot.ibl_brdf_lut) {
                c.cmd.set_shader_resource(4, *c.snapshot.ibl_brdf_lut);
            }
            if (draw.metallic_roughness) {
                c.cmd.set_shader_resource(5, *draw.metallic_roughness);
            }
            if (draw.normal_map) {
                c.cmd.set_shader_resource(6, *draw.normal_map);
            }
        });
}

void record_shadow_draws(PassContext& ctx) {
    if (!ctx.snapshot.shadow_pipeline) {
        return;
    }

    ShadowConstants constants{};
    constants.view_proj = ctx.snapshot.sun_view_proj;

    record_draws(ctx, ctx.snapshot.shadow_pipeline, constants,
        [](ShadowConstants&, const DrawBatch&) {},
        [](PassContext&, const DrawBatch&) {});
}

void record_motion_draws(PassContext& ctx) {
    if (!ctx.snapshot.motion_pipeline) {
        return;
    }

    motion::Constants constants{};
    constants.view_proj = ctx.snapshot.projection * ctx.snapshot.view;
    constants.prev_view_proj = ctx.snapshot.prev_view_proj;
    constants.jitter = {ctx.snapshot.taa_jitter.x, ctx.snapshot.taa_jitter.y, 0.f, 0.f};

    record_draws(ctx, ctx.snapshot.motion_pipeline, constants,
        [](motion::Constants&, const DrawBatch&) {},
        [](PassContext&, const DrawBatch&) {});
}

void record_sky(PassContext& ctx) {
    if (!ctx.snapshot.sky_pipeline || !ctx.snapshot.sky_cubemap) {
        return;
    }

    const sky::Constants constants = sky::make_constants(ctx.snapshot.view, ctx.snapshot.projection,
        ctx.snapshot.lighting.sun_direction, ctx.snapshot.lighting.sun_color,
        ctx.snapshot.taa_jitter);
    const rhi::FrameAllocation slice = ctx.device.alloc_frame_memory(sizeof(constants));
    if (!slice.buffer) {
        return;  // constant ring exhausted this frame; skip the pass
    }
    ctx.device.write_buffer(*slice.buffer, slice.offset, &constants, sizeof(constants));
    ctx.cmd.set_pipeline(*ctx.snapshot.sky_pipeline);
    ctx.cmd.set_constant_buffer(0, *slice.buffer, slice.offset);
    ctx.cmd.set_shader_resource(0, *ctx.snapshot.sky_cubemap);
    ctx.cmd.draw(3);
}

void record_bloom_downsample(PassContext& ctx, bool first_mip) {
    if (!ctx.snapshot.bloom_downsample_pipeline || ctx.shader_read_count == 0
        || !ctx.shader_reads[0]) {
        return;
    }
    const bloom::Constants constants = bloom::make_downsample_constants(
        ctx.shader_reads[0]->width(), ctx.shader_reads[0]->height(), first_mip);
    const rhi::FrameAllocation slice = ctx.device.alloc_frame_memory(sizeof(constants));
    if (!slice.buffer) {
        return;  // constant ring exhausted this frame; skip the pass
    }
    ctx.device.write_buffer(*slice.buffer, slice.offset, &constants, sizeof(constants));
    ctx.cmd.set_pipeline(*ctx.snapshot.bloom_downsample_pipeline);
    ctx.cmd.set_constant_buffer(0, *slice.buffer, slice.offset);
    ctx.cmd.set_shader_resource(0, *ctx.shader_reads[0]);
    ctx.cmd.draw(3);
}

void record_bloom_upsample(PassContext& ctx) {
    if (!ctx.snapshot.bloom_upsample_pipeline || ctx.shader_read_count < 2
        || !ctx.shader_reads[0] || !ctx.shader_reads[1]) {
        return;
    }
    const bloom::Constants constants = bloom::make_upsample_constants(
        ctx.shader_reads[0]->width(), ctx.shader_reads[0]->height());
    const rhi::FrameAllocation slice = ctx.device.alloc_frame_memory(sizeof(constants));
    if (!slice.buffer) {
        return;  // constant ring exhausted this frame; skip the pass
    }
    ctx.device.write_buffer(*slice.buffer, slice.offset, &constants, sizeof(constants));
    ctx.cmd.set_pipeline(*ctx.snapshot.bloom_upsample_pipeline);
    ctx.cmd.set_constant_buffer(0, *slice.buffer, slice.offset);
    ctx.cmd.set_shader_resource(0, *ctx.shader_reads[0]);
    ctx.cmd.set_shader_resource(1, *ctx.shader_reads[1]);
    ctx.cmd.draw(3);
}

void record_tonemap(PassContext& ctx) {
    if (!ctx.snapshot.tonemap_pipeline || ctx.shader_read_count == 0 || !ctx.shader_reads[0]) {
        return;
    }
    ctx.cmd.set_pipeline(*ctx.snapshot.tonemap_pipeline);
    ctx.cmd.set_shader_resource(0, *ctx.shader_reads[0]);
    if (ctx.shader_read_count > 1 && ctx.shader_reads[1]) {
        ctx.cmd.set_shader_resource(1, *ctx.shader_reads[1]);
    }
    ctx.cmd.draw(3);
}

namespace {

// False means the constant ring is exhausted and nothing was bound. Returning
// void here was a bug: the comment claimed it skipped the pass, but only the
// caller can do that, and all four drew anyway with root CBV slot 0 unset.
[[nodiscard]] bool bind_aa_constants(PassContext& ctx, const rhi::ITexture& src) {
    const aa::Constants constants = aa::make_constants(src.width(), src.height());
    const rhi::FrameAllocation slice = ctx.device.alloc_frame_memory(sizeof(constants));
    if (!slice.buffer) {
        return false;  // caller must skip the pass
    }
    ctx.device.write_buffer(*slice.buffer, slice.offset, &constants, sizeof(constants));
    ctx.cmd.set_constant_buffer(0, *slice.buffer, slice.offset);
    return true;
}

} // namespace

void record_fxaa(PassContext& ctx) {
    if (!ctx.snapshot.fxaa_pipeline || ctx.shader_read_count == 0 || !ctx.shader_reads[0]) {
        return;
    }
    ctx.cmd.set_pipeline(*ctx.snapshot.fxaa_pipeline);
    if (!bind_aa_constants(ctx, *ctx.shader_reads[0])) {
        return;
    }
    ctx.cmd.set_shader_resource(0, *ctx.shader_reads[0]);
    ctx.cmd.draw(3);
}

void record_smaa_edge(PassContext& ctx) {
    if (!ctx.snapshot.smaa_edge_pipeline || ctx.shader_read_count == 0 || !ctx.shader_reads[0]) {
        return;
    }
    ctx.cmd.set_pipeline(*ctx.snapshot.smaa_edge_pipeline);
    if (!bind_aa_constants(ctx, *ctx.shader_reads[0])) {
        return;
    }
    ctx.cmd.set_shader_resource(0, *ctx.shader_reads[0]);
    ctx.cmd.draw(3);
}

void record_smaa_weights(PassContext& ctx) {
    if (!ctx.snapshot.smaa_weights_pipeline || ctx.shader_read_count == 0 || !ctx.shader_reads[0]) {
        return;
    }
    ctx.cmd.set_pipeline(*ctx.snapshot.smaa_weights_pipeline);
    if (!bind_aa_constants(ctx, *ctx.shader_reads[0])) {
        return;
    }
    ctx.cmd.set_shader_resource(0, *ctx.shader_reads[0]);
    ctx.cmd.draw(3);
}

void record_smaa_blend(PassContext& ctx) {
    if (!ctx.snapshot.smaa_blend_pipeline || ctx.shader_read_count < 2
        || !ctx.shader_reads[0] || !ctx.shader_reads[1]) {
        return;
    }
    ctx.cmd.set_pipeline(*ctx.snapshot.smaa_blend_pipeline);
    if (!bind_aa_constants(ctx, *ctx.shader_reads[0])) {
        return;
    }
    ctx.cmd.set_shader_resource(0, *ctx.shader_reads[0]);
    ctx.cmd.set_shader_resource(1, *ctx.shader_reads[1]);
    ctx.cmd.draw(3);
}

void record_taa(PassContext& ctx) {
    if (!ctx.snapshot.taa_pipeline || ctx.shader_read_count < 3 || !ctx.shader_reads[0]
        || !ctx.shader_reads[1] || !ctx.shader_reads[2]) {
        return;
    }
    rhi::ITexture* history = ctx.snapshot.taa_history;
    const bool reset = ctx.snapshot.taa_reset || history == nullptr;
    if (!history) {
        history = ctx.shader_reads[0];
    }
    const taa::Constants constants = taa::make_constants(ctx.shader_reads[0]->width(),
        ctx.shader_reads[0]->height(), ctx.snapshot.taa_jitter, reset);
    const rhi::FrameAllocation slice = ctx.device.alloc_frame_memory(sizeof(constants));
    if (!slice.buffer) {
        return;  // constant ring exhausted this frame; skip the pass
    }
    ctx.device.write_buffer(*slice.buffer, slice.offset, &constants, sizeof(constants));
    ctx.cmd.set_pipeline(*ctx.snapshot.taa_pipeline);
    ctx.cmd.set_constant_buffer(0, *slice.buffer, slice.offset);
    ctx.cmd.set_shader_resource(0, *ctx.shader_reads[0]);
    ctx.cmd.set_shader_resource(1, *ctx.shader_reads[1]);
    ctx.cmd.set_shader_resource(2, *ctx.shader_reads[2]);
    ctx.cmd.set_shader_resource(3, *history);
    ctx.cmd.draw(3);
}

void record_tonemap_aces(PassContext& ctx) {
    if (!ctx.snapshot.tonemap_aces_pipeline || ctx.shader_read_count == 0
        || !ctx.shader_reads[0]) {
        return;
    }
    ctx.cmd.set_pipeline(*ctx.snapshot.tonemap_aces_pipeline);
    ctx.cmd.set_shader_resource(0, *ctx.shader_reads[0]);
    ctx.cmd.draw(3);
}

ResourceHandle RenderGraph::swapchain_color() const {
    return ResourceHandle{kSwapchainColorId};
}

ResourceHandle RenderGraph::swapchain_depth() const {
    return ResourceHandle{kSwapchainDepthId};
}

bool RenderGraph::is_imported(ResourceHandle handle) const {
    if (!handle.valid() || handle.id >= resources_.size()) {
        return false;
    }
    return resources_[handle.id].imported;
}

rhi::ResourceState RenderGraph::state_for(Access access) const {
    switch (access) {
    case Access::ColorWrite: return rhi::ResourceState::RenderTarget;
    case Access::DepthWrite: return rhi::ResourceState::DepthWrite;
    case Access::CopySrc:    return rhi::ResourceState::CopySrc;
    case Access::CopyDst:    return rhi::ResourceState::CopyDst;
    case Access::ShaderRead: return rhi::ResourceState::ShaderRead;
    }
    return rhi::ResourceState::Common;
}

void RenderGraph::ensure_imported() {
    if (!resources_.empty()) {
        return;
    }
    resources_.resize(kFirstTransientId);
    resources_[kSwapchainColorId].name = "swapchain_color";
    resources_[kSwapchainColorId].imported = true;
    resources_[kSwapchainColorId].format = rhi::Format::RGBA8_UNORM;
    resources_[kSwapchainColorId].state = rhi::ResourceState::Present;
    resources_[kSwapchainDepthId].name = "swapchain_depth";
    resources_[kSwapchainDepthId].imported = true;
    resources_[kSwapchainDepthId].is_depth = true;
    resources_[kSwapchainDepthId].format = rhi::Format::D32_FLOAT;
    resources_[kSwapchainDepthId].state = rhi::ResourceState::DepthWrite;
}

ResourceHandle RenderGraph::create_transient(const TransientDesc& desc) {
    ensure_imported();

    ResourceRecord record{};
    record.name = std::string(desc.name);
    record.format = desc.format;
    record.usage = desc.usage;
    record.width = desc.width;
    record.height = desc.height;
    record.extent_div = desc.extent_div == 0 ? 1u : desc.extent_div;
    record.is_depth = desc.usage == rhi::TextureUsage::DepthStencil
        || desc.usage == rhi::TextureUsage::DepthShaderResource
        || desc.format == rhi::Format::D32_FLOAT;
    record.state = record.is_depth ? rhi::ResourceState::DepthWrite : rhi::ResourceState::Common;
    resources_.push_back(std::move(record));
    dirty_ = true;
    compiled_ = false;
    return ResourceHandle{static_cast<u32>(resources_.size() - 1)};
}

ResourceHandle RenderGraph::import_persistent(std::string_view name, rhi::Format format) {
    ensure_imported();
    ResourceRecord record{};
    record.name = std::string(name);
    record.imported = true;
    record.format = format;
    record.usage = rhi::TextureUsage::ColorShaderResource;
    record.state = rhi::ResourceState::Common;
    resources_.push_back(std::move(record));
    dirty_ = true;
    compiled_ = false;
    return ResourceHandle{static_cast<u32>(resources_.size() - 1)};
}

void RenderGraph::bind_persistent(ResourceHandle handle, rhi::ITexture* texture) {
    if (!handle.valid() || handle.id >= resources_.size()) {
        return;
    }
    resources_[handle.id].external = texture;
    resources_[handle.id].state = rhi::ResourceState::Common;
}

ResourceHandle RenderGraph::find_resource(std::string_view name) const {
    for (u32 i = 1; i < resources_.size(); ++i) {
        if (resources_[i].name == name) {
            return ResourceHandle{i};
        }
    }
    return {};
}

void RenderGraph::add_pass(RenderPassDesc desc) {
    ensure_imported();
    passes_.push_back(std::move(desc));
    dirty_ = true;
    compiled_ = false;
}

u32 RenderGraph::pass_count() const {
    return static_cast<u32>(passes_.size());
}

std::string_view RenderGraph::pass_name(u32 index) const {
    if (index >= passes_.size()) {
        return {};
    }
    return passes_[index].name;
}

void RenderGraph::clear() {
    passes_.clear();
    resources_.clear();
    compiled_ = false;
    dirty_ = true;
    allocated_width_ = 0;
    allocated_height_ = 0;
    swapchain_in_common_ = true;
}

bool RenderGraph::compile() {
    if (resources_.empty()) {
        log(LogLevel::Error, LogChannel::Render, "Graph compile failed: no resources");
        compiled_ = false;
        return false;
    }

    std::unordered_set<u32> produced;
    for (u32 i = 1; i < resources_.size(); ++i) {
        if (resources_[i].imported) {
            produced.insert(i);
        }
    }

    bool ok = true;
    for (const RenderPassDesc& pass : passes_) {
        auto consume = [&](ResourceHandle handle, Access access, const char* kind) {
            if (!handle.valid() || handle.id >= resources_.size()) {
                log(LogLevel::Error, LogChannel::Render,
                    std::string("Graph: pass '") + pass.name + "' " + kind + " is invalid");
                ok = false;
                return;
            }
            if (produced.find(handle.id) == produced.end()) {
                char message[256];
                std::snprintf(message, sizeof(message),
                    "Graph: missing producer for '%s' (%s) read by pass '%s'",
                    resources_[handle.id].name.c_str(), access_name(access), pass.name.c_str());
                log(LogLevel::Error, LogChannel::Render, message);
                ok = false;
            }
        };

        if (pass.kind == PassKind::Copy) {
            consume(pass.copy_src, Access::CopySrc, "copy_src");
            if (!pass.copy_dst.valid() || pass.copy_dst.id >= resources_.size()) {
                log(LogLevel::Error, LogChannel::Render,
                    std::string("Graph: pass '") + pass.name + "' copy_dst is invalid");
                ok = false;
                continue;
            }
            if (pass.copy_src == pass.copy_dst) {
                log(LogLevel::Error, LogChannel::Render,
                    std::string("Graph: pass '") + pass.name + "' copy_src == copy_dst");
                ok = false;
            }
            if (pass.copy_src.valid() && pass.copy_src.id < resources_.size()) {
                const ResourceRecord& src = resources_[pass.copy_src.id];
                const ResourceRecord& dst = resources_[pass.copy_dst.id];
                if (src.format != dst.format) {
                    char message[256];
                    std::snprintf(message, sizeof(message),
                        "Graph: copy format mismatch in pass '%s' (%s -> %s)",
                        pass.name.c_str(), format_name(src.format), format_name(dst.format));
                    log(LogLevel::Error, LogChannel::Render, message);
                    ok = false;
                }
            }
            produced.insert(pass.copy_dst.id);
            continue;
        }

        if (pass.write_count == 0) {
            log(LogLevel::Error, LogChannel::Render,
                std::string("Graph: graphics pass '") + pass.name + "' has no writes");
            ok = false;
        }

        for (u32 i = 0; i < pass.read_count; ++i) {
            consume(pass.reads[i].handle, pass.reads[i].access, "read");
        }
        for (u32 i = 0; i < pass.write_count; ++i) {
            const ResourceHandle handle = pass.writes[i].handle;
            if (!handle.valid() || handle.id >= resources_.size()) {
                log(LogLevel::Error, LogChannel::Render,
                    std::string("Graph: pass '") + pass.name + "' write is invalid");
                ok = false;
                continue;
            }
            produced.insert(handle.id);
        }
    }

    if (ok) {
        std::string cycle_pass;
        if (has_pass_cycle(passes_, cycle_pass)) {
            log(LogLevel::Error, LogChannel::Render,
                std::string("Graph: pass cycle involving '") + cycle_pass + "'");
            ok = false;
        }
    }

    compiled_ = ok;
    dirty_ = !ok;
    if (ok) {
        char message[128];
        std::snprintf(message, sizeof(message),
            "Graph compiled: %zu passes, %zu resources",
            passes_.size(), resources_.size() > 1 ? resources_.size() - 1 : 0);
        log(LogLevel::Info, LogChannel::Render, message);
    }
    return ok;
}

rhi::ITexture* RenderGraph::resolve(rhi::IDevice& device, ResourceHandle handle) {
    if (!handle.valid() || handle.id >= resources_.size()) {
        return nullptr;
    }
    if (handle.id == kSwapchainColorId) {
        return &device.swapchain_color();
    }
    if (handle.id == kSwapchainDepthId) {
        return &device.swapchain_depth();
    }
    if (resources_[handle.id].external) {
        return resources_[handle.id].external;
    }
    return resources_[handle.id].texture.get();
}

void RenderGraph::ensure_transients(rhi::IDevice& device) {
    const u32 width = device.width();
    const u32 height = device.height();
    const bool resize = width != allocated_width_ || height != allocated_height_;
    if (resize) {
        bool drop = false;
        for (usize i = 0; i < resources_.size(); ++i) {
            if (is_imported(ResourceHandle{static_cast<u32>(i)}) || !resources_[i].texture) {
                continue;
            }
            if (resources_[i].width == 0 || resources_[i].height == 0) {
                drop = true;
                break;
            }
        }
        if (drop) {
            device.wait_idle();
        }
        for (usize i = 0; i < resources_.size(); ++i) {
            if (is_imported(ResourceHandle{static_cast<u32>(i)})) {
                continue;
            }
            if (resources_[i].width != 0 && resources_[i].height != 0) {
                continue;
            }
            resources_[i].texture.reset();
            resources_[i].state = resources_[i].is_depth
                ? rhi::ResourceState::DepthWrite
                : rhi::ResourceState::Common;
        }
        allocated_width_ = width;
        allocated_height_ = height;
        swapchain_in_common_ = true;
    }

    for (usize i = 0; i < resources_.size(); ++i) {
        ResourceRecord& record = resources_[i];
        if (is_imported(ResourceHandle{static_cast<u32>(i)}) || record.imported
            || record.name.empty() || record.texture) {
            continue;
        }
        rhi::TextureDesc desc{};
        const u32 div = record.extent_div == 0 ? 1u : record.extent_div;
        desc.width = record.width != 0 ? record.width : width / div;
        desc.height = record.height != 0 ? record.height : height / div;
        if (desc.width == 0) {
            desc.width = 1;
        }
        if (desc.height == 0) {
            desc.height = 1;
        }
        desc.format = record.format;
        desc.usage = record.usage;
        record.texture = device.create_texture(desc);
        if (record.texture) {
            device.set_debug_name(*record.texture, record.name);
        } else {
            log(LogLevel::Error, LogChannel::Render,
                std::string("Graph: failed to allocate transient '") + record.name + "'");
        }
    }
}

void RenderGraph::transition_to(rhi::ICommandList& cmd, rhi::IDevice& device,
    ResourceHandle handle, rhi::ResourceState desired) {
    if (!handle.valid() || handle.id >= resources_.size()) {
        return;
    }
    ResourceRecord& record = resources_[handle.id];
    if (record.state == desired) {
        return;
    }
    rhi::ITexture* texture = resolve(device, handle);
    ENGINE_ASSERT(texture != nullptr);
    cmd.transition(*texture, record.state, desired);
    record.state = desired;
}

void RenderGraph::execute(rhi::IDevice& device, const RenderSnapshot& snapshot) {
    bool can_record = true;
    if (dirty_) {
        can_record = compile();
    }

    if (can_record) {
        ensure_transients(device);
    }

    device.begin_frame();
    if (resources_.size() > kSwapchainDepthId) {
        resources_[kSwapchainColorId].state = swapchain_in_common_
            ? rhi::ResourceState::Common
            : rhi::ResourceState::Present;
        resources_[kSwapchainDepthId].state = rhi::ResourceState::DepthWrite;
        swapchain_in_common_ = false;
    }

    auto& cmd = device.command_list();
    cmd.begin();

    if (!can_record) {
        cmd.end();
        device.end_frame();
        return;
    }

    // Before any pass records: the ring allocator hands out memory that stays
    // valid for the whole frame, so one upload here serves shadow, forward and
    // motion. Doing it inside `record_draws` would upload the same array three
    // times.
    const rhi::FrameAllocation instance_slice = upload_instances(device, snapshot.instances);

    for (RenderPassDesc& pass : passes_) {
        if (pass.should_execute && !pass.should_execute(snapshot)) {
            continue;
        }
        const rhi::GpuDebugEvent pass_event(cmd,
            pass.name.empty() ? std::string_view("pass") : std::string_view(pass.name));
        if (pass.kind == PassKind::Copy) {
            transition_to(cmd, device, pass.copy_src, rhi::ResourceState::CopySrc);
            transition_to(cmd, device, pass.copy_dst, rhi::ResourceState::CopyDst);
            rhi::ITexture* src = resolve(device, pass.copy_src);
            rhi::ITexture* dst = resolve(device, pass.copy_dst);
            if (src && dst) {
                cmd.copy_texture(*src, *dst);
            }
            continue;
        }

        rhi::ITexture* color = nullptr;
        rhi::ITexture* depth = nullptr;
        for (u32 i = 0; i < pass.write_count; ++i) {
            const ResourceRef& ref = pass.writes[i];
            transition_to(cmd, device, ref.handle, state_for(ref.access));
            if (ref.access == Access::ColorWrite) {
                color = resolve(device, ref.handle);
            } else if (ref.access == Access::DepthWrite) {
                depth = resolve(device, ref.handle);
            }
        }
        for (u32 i = 0; i < pass.read_count; ++i) {
            transition_to(cmd, device, pass.reads[i].handle, state_for(pass.reads[i].access));
        }

        rhi::RenderPassInfo info{};
        info.color = color;
        info.depth = depth;
        info.clear_color = pass.clear_color;
        info.clear_color_target = pass.clear_color_target;
        info.clear_depth = pass.clear_depth;
        cmd.begin_render_pass(info);
        if (pass.execute) {
            PassContext ctx{device, cmd, snapshot, instance_slice};
            for (u32 i = 0; i < pass.read_count && ctx.shader_read_count < 4; ++i) {
                if (pass.reads[i].access == Access::ShaderRead) {
                    ctx.shader_reads[ctx.shader_read_count] = resolve(device, pass.reads[i].handle);
                    ctx.shader_read_count += 1;
                }
            }
            pass.execute(ctx);
        }
        cmd.end_render_pass();
    }

    transition_to(cmd, device, swapchain_color(), rhi::ResourceState::Present);
    cmd.end();
    device.end_frame();
}

} // namespace engine::renderer
