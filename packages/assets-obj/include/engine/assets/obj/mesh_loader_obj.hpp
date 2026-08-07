#pragma once

#include <engine/assets/mesh.hpp>

#include <memory>

namespace engine::assets::obj {

std::unique_ptr<IMeshLoader> create_mesh_loader();

} // namespace engine::assets::obj
