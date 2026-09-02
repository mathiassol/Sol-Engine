#pragma once

#include <engine/debug/frame_stats.hpp>
#include <engine/rhi/commands.hpp>
#include <engine/rhi/device.hpp>
#include <engine/shaders/shader_compiler.hpp>

#include <memory>
#include <string_view>

namespace engine::debug {

class StatsOverlay {
public:
    bool init(rhi::IDevice& device, shaders::IShaderCompiler& compiler,
        std::string_view shader_path, shaders::ShaderTarget target);
    void set_visible(bool visible) { visible_ = visible; }
    bool visible() const { return visible_; }
    void update(const FrameStats& stats);
    void draw(rhi::ICommandList& cmd, u32 width, u32 height);

private:
    void rebuild_mesh(std::string_view text);

    rhi::IDevice* device_ = nullptr;
    std::unique_ptr<rhi::IGraphicsPipeline> pipeline_;
    std::unique_ptr<rhi::IBuffer> vertex_buffer_;
    u32 vertex_count_ = 0;
    std::string cached_text_;
    bool visible_ = false;
};

} // namespace engine::debug
