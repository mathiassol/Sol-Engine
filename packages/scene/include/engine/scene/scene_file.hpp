#pragma once

#include <engine/assets/asset_loader.hpp>
#include <engine/scene/world.hpp>

#include <string>
#include <string_view>

namespace engine::scene {

// Text scene file. Camera is runtime (fly cam) and is not stored.
// Unnamed instances are omitted on write and cannot round-trip.
// Parent links are names, not indices. Locals are as stored on Instance.
bool write_world(const World& world, std::string& out);
bool read_world(std::string_view text, World& out);

// Read a `.solscene` through a mounted virtual path — the path form a game
// actually has. Until this existed, read_world took text and nothing in the
// engine opened a scene file, so the format was a library with no way in.
//
// An instance names its mesh by `assets::MeshHandle`, whose id is
// `fnv1a64(<the key the mesh was stored under>)` — stable across runs, so a
// hand-authored file can reference a mesh. It is stable, not readable: the id
// is a 20-digit number in the file. Naming meshes by path would be a format
// change and is deliberately not done here.
bool load_world(assets::IAssetLoader& loader, std::string_view virtual_path, World& out);

} // namespace engine::scene
