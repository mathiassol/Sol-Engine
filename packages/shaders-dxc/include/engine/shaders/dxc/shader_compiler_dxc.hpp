#pragma once

#include <engine/shaders/shader_compiler.hpp>

#include <memory>

namespace engine::shaders::dxc {

std::unique_ptr<IShaderCompiler> create_compiler();

} // namespace engine::shaders::dxc
