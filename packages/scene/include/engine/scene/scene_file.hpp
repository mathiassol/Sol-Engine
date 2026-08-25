#pragma once

#include <engine/scene/world.hpp>

#include <string>
#include <string_view>

namespace engine::scene {

// Text scene file. Camera is runtime (fly cam) and is not stored.
// Unnamed instances are omitted on write and cannot round-trip.
// Parent links are names, not indices. Locals are as stored on Instance.
bool write_world(const World& world, std::string& out);
bool read_world(std::string_view text, World& out);

} // namespace engine::scene
