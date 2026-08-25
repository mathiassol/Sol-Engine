#pragma once

#include <engine/shaders/shader_compiler.hpp>

#include <memory>

namespace engine::shaders::dxc {

std::unique_ptr<IShaderCompiler> create_compiler();
std::unique_ptr<IShaderCompiler> create_cached_compiler(std::string_view cache_directory);

} // namespace engine::shaders::dxc
