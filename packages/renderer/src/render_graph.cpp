#include <engine/renderer/render_graph.hpp>
#include <engine/rhi/device.hpp>

namespace engine::renderer {

void RenderGraph::add_pass(RenderPassDesc desc) {
    passes_.push_back(std::move(desc));
}

void RenderGraph::execute(rhi::IDevice& device) {
    device.begin_frame();
    auto& cmd = device.command_list();
    cmd.begin();
    for (auto& pass : passes_) {
        if (pass.execute) {
            pass.execute(cmd);
        }
    }
    cmd.end();
    device.end_frame();
}

void RenderGraph::clear() {
    passes_.clear();
}

} // namespace engine::renderer
