#pragma once

// glTF -> scene::World, behind the r.scene knob.
//
// TEMPORARY. `document` (step 3 of the editor architecture spec) replaces this
// with the real per-entity text format. Do not grow it: if this file starts
// wanting structure, that is the signal to build `document`, not to refactor
// here. It stays in the sandbox for the same reason - a package we already know
// we will delete is exactly the scaffolding docs/packageRules.md forbids.

#include <engine/assets/asset_loader.hpp>
#include <engine/assets/gpu/mesh_store.hpp>
#include <engine/assets/gpu/texture_store.hpp>
#include <engine/rhi/device.hpp>
#include <engine/scene/world.hpp>

namespace sandbox {

// Imports whatever `r.scene` names, replacing `world`'s instances and materials
// with the scene's. Its lighting is left alone.
//
// Returns false having touched nothing when the knob is empty - which is the
// default, and therefore what every gate run and the husky demo exercise.
bool import_scene_from_cvar(engine::rhi::IDevice& device, engine::assets::IAssetLoader& loader,
    engine::assets::gpu::GpuMeshStore& meshes, engine::assets::gpu::GpuTextureStore& textures,
    engine::scene::World& world);

} // namespace sandbox
