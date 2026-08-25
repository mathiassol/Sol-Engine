#include <engine/debug/debug_lines.hpp>

#include <engine/core/log.hpp>
#include <engine/rhi/resources.hpp>

#include <span>

namespace engine::debug {

namespace {

struct LineConstants {
    math::Mat4 view_proj{};
};

rhi::GraphicsPipelineDesc lines_pipeline_desc(std::span<const u8> vs, std::span<const u8> ps) {
    rhi::GraphicsPipelineDesc desc{};
    desc.vertex_shader = vs;
    desc.pixel_shader = ps;
    desc.attributes[0] = {rhi::VertexSemantic::Position, 0, rhi::VertexFormat::Float3, 0};
    desc.attributes[1] = {rhi::VertexSemantic::Color, 0, rhi::VertexFormat::Float3, 12};
    desc.attribute_count = 2;
    desc.constant_buffer_count = 1;
    desc.depth = rhi::DepthTest::Less;
    desc.cull = rhi::CullMode::None;
    desc.blend = rhi::BlendMode::Opaque;
    desc.topology = rhi::PrimitiveTopology::LineList;
    desc.color_format = rhi::Format::RGBA8_UNORM;
    desc.depth_format = rhi::Format::D32_FLOAT;
    desc.debug_name = "debug_lines";
    return desc;
}

} // namespace

bool DebugLines::init(rhi::IDevice& device, shaders::IShaderCompiler& compiler,
    std::string_view shader_path) {
    device_ = &device;

    shaders::ShaderCompileDesc vs_desc{};
    vs_desc.file_path = shader_path;
    vs_desc.entry_point = "vs_main";
    vs_desc.target_profile = "vs_6_0";

    shaders::ShaderCompileDesc ps_desc = vs_desc;
    ps_desc.entry_point = "ps_main";
    ps_desc.target_profile = "ps_6_0";

    shaders::ShaderBytecode vs_bytecode;
    shaders::ShaderBytecode ps_bytecode;
    std::string error;
    if (!compiler.compile(vs_desc, vs_bytecode, error)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "Debug lines vertex shader compile failed");
        return false;
    }
    if (!compiler.compile(ps_desc, ps_bytecode, error)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "Debug lines pixel shader compile failed");
        return false;
    }

    pipeline_ = device.create_graphics_pipeline(
        lines_pipeline_desc(vs_bytecode.data, ps_bytecode.data));
    if (!pipeline_) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "Debug lines pipeline creation failed");
        return false;
    }

    return true;
}

void DebugLines::clear() {
    vertices_.clear();
}

void DebugLines::add_line(math::Vec3 a, math::Vec3 b, math::Vec3 color) {
    vertices_.push_back({a.x, a.y, a.z, color.x, color.y, color.z});
    vertices_.push_back({b.x, b.y, b.z, color.x, color.y, color.z});
}

void DebugLines::add_aabb(const math::Aabb& box, math::Vec3 color) {
    if (!box.valid()) {
        return;
    }

    const math::Vec3 p[8] = {
        {box.min.x, box.min.y, box.min.z},
        {box.max.x, box.min.y, box.min.z},
        {box.min.x, box.max.y, box.min.z},
        {box.max.x, box.max.y, box.min.z},
        {box.min.x, box.min.y, box.max.z},
        {box.max.x, box.min.y, box.max.z},
        {box.min.x, box.max.y, box.max.z},
        {box.max.x, box.max.y, box.max.z},
    };

    add_line(p[0], p[1], color);
    add_line(p[2], p[3], color);
    add_line(p[4], p[5], color);
    add_line(p[6], p[7], color);
    add_line(p[0], p[2], color);
    add_line(p[1], p[3], color);
    add_line(p[4], p[6], color);
    add_line(p[5], p[7], color);
    add_line(p[0], p[4], color);
    add_line(p[1], p[5], color);
    add_line(p[2], p[6], color);
    add_line(p[3], p[7], color);
}

void DebugLines::draw(rhi::ICommandList& cmd, const math::Mat4& view, const math::Mat4& projection) {
    if (!visible_ || !pipeline_ || !device_ || vertices_.empty()) {
        return;
    }

    const usize bytes = vertices_.size() * sizeof(Vertex);
    const rhi::FrameAllocation verts = device_->alloc_frame_memory(bytes);
    if (!verts.buffer) {
        return;
    }
    device_->write_buffer(*verts.buffer, verts.offset, vertices_.data(), bytes);

    LineConstants constants{};
    constants.view_proj = projection * view;
    const rhi::FrameAllocation slice = device_->alloc_frame_memory(sizeof(constants));
    if (!slice.buffer) {
        return;
    }
    device_->write_buffer(*slice.buffer, slice.offset, &constants, sizeof(constants));

    cmd.set_pipeline(*pipeline_);
    cmd.set_constant_buffer(0, *slice.buffer, slice.offset);
    cmd.set_vertex_buffer(0, *verts.buffer, sizeof(Vertex), verts.offset);
    cmd.draw(static_cast<u32>(vertices_.size()));
}

} // namespace engine::debug
