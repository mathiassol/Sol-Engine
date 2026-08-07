#pragma once

#include <engine/core/types.hpp>

#include <functional>
#include <string_view>
#include <vector>

namespace engine::rhi {
class ICommandList;
class IDevice;
} // namespace engine::rhi

namespace engine::renderer {

using PassExecuteFn = std::function<void(rhi::ICommandList&)>;

struct RenderPassDesc {
    std::string_view name;
    PassExecuteFn execute;
};

class RenderGraph {
public:
    void add_pass(RenderPassDesc desc);
    void execute(rhi::IDevice& device);
    void clear();

private:
    std::vector<RenderPassDesc> passes_;
};

} // namespace engine::renderer
