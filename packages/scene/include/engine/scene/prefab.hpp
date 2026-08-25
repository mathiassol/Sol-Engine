#pragma once

#include <engine/math/mat4.hpp>
#include <engine/scene/world.hpp>

#include <string_view>

namespace engine::scene {

// Copy a named root and its named descendants into a fragment World.
// Lights and camera are left at defaults. Same text format as write_world.
bool extract_prefab(const World& world, std::string_view root_name, World& out);

// Spawn a fragment into dest. Unparented locals become world_transform * local.
// Names are prefix + original (must fit kMaxNameChars). Materials are appended.
// Returns the dest index of the first spawned instance, or kInvalidInstance.
u32 instantiate_prefab(World& dest, const World& prefab, const math::Mat4& world_transform,
    std::string_view prefix);

// read_world then instantiate_prefab.
u32 instantiate_prefab(World& dest, std::string_view text, const math::Mat4& world_transform,
    std::string_view prefix);

} // namespace engine::scene
