#pragma once

#include <engine/shaders/shader_hot_reload.hpp>

#include <memory>

namespace engine::shaders::dxc {

std::unique_ptr<IShaderHotReloader> create_hot_reloader();

} // namespace engine::shaders::dxc
