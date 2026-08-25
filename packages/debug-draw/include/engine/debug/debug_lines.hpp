#pragma once

#include <engine/math/aabb.hpp>
#include <engine/math/mat4.hpp>
#include <engine/math/vec3.hpp>
#include <engine/rhi/commands.hpp>
#include <engine/rhi/device.hpp>
#include <engine/shaders/shader_compiler.hpp>

#include <memory>
#include <string_view>
#include <vector>

namespace engine::debug {

class DebugLines {
public:
    bool init(rhi::IDevice& device, shaders::IShaderCompiler& compiler,
        std::string_view shader_path);
    void set_visible(bool visible) { visible_ = visible; }
    bool visible() const { return visible_; }

    void clear();
    void add_line(math::Vec3 a, math::Vec3 b, math::Vec3 color);
    void add_aabb(const math::Aabb& box, math::Vec3 color);
    void draw(rhi::ICommandList& cmd, const math::Mat4& view, const math::Mat4& projection);

private:
    struct Vertex {
        f32 px = 0.f, py = 0.f, pz = 0.f;
        f32 r = 1.f, g = 1.f, b = 1.f;
    };

    rhi::IDevice* device_ = nullptr;
    std::unique_ptr<rhi::IGraphicsPipeline> pipeline_;
    std::vector<Vertex> vertices_;
    bool visible_ = false;
};

} // namespace engine::debug
