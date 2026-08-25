#pragma once

#include <engine/physics/physics.hpp>

#include <memory>

namespace engine::physics::cpu {

std::unique_ptr<IPhysics> create_physics();

} // namespace engine::physics::cpu
