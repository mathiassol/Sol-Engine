#include "scene_import.hpp"

#include "sandbox_common.hpp"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#ifdef ENGINE_HAS_PNG
#include <engine/assets/png/image_loader_png.hpp>
#endif

namespace sandbox {

namespace {

// Declared beside the code that reads it, per the cvar convention in
// core/cvar.hpp. Empty by default, so nothing in the demo or the gate sequence
// changes unless a run asks for a scene by name.
engine::Cvar cv_scene{"r.scene", "",
    "glTF scene under /content to import instead of the demo scene"};

// What the import managed to do. The last two fields are the ones worth
// reading: a scene that half-loaded because its textures were missing
// otherwise looks exactly like a scene that rendered badly.
struct ImportStats {
    engine::u32 materials = 0;
    engine::u32 instances = 0;
    engine::u32 textures = 0;
    engine::u32 skipped_nodes = 0;
    engine::u32 missing_textures = 0;
};

// The leading /content is implied but accepted, so both of these name the same
// file:
//     --set r.scene=/scenes/alley/ph_hidden_alley.gltf
//     --set r.scene=/content/scenes/alley/ph_hidden_alley.gltf
std::string content_virtual_path(std::string_view value) {
    if (value.empty() || value.starts_with("/content/") || value == "/content") {
        return std::string(value);
    }
    std::string out = "/content";
    if (value.front() != '/' && value.front() != '\\') {
        out += '/';
    }
    out += value;
    return out;
}

// `load_png_file` is format-agnostic despite its name: assets-png-wic hands
// CreateDecoderFromStream a null vendor GUID, so WIC sniffs the container and
// chooses the decoder. That is why 41 of this scene's 286 image URIs can end
// in .jpg with no JPEG decoder anywhere in the tree.
bool decode_image_file(const std::string& path, engine::assets::ImageData& out) {
#ifdef ENGINE_HAS_PNG
    return engine::assets::png::load_png_file(path, out);
#else
    (void)path;
    (void)out;
    return false;
#endif
}

bool import_gltf_scene(const engine::assets::gltf::GltfSceneResult& src,
    engine::rhi::IDevice& device, engine::assets::IAssetLoader& loader,
    engine::assets::gpu::GpuMeshStore& meshes, engine::assets::gpu::GpuTextureStore& textures,
    engine::scene::World& out, ImportStats& stats) {
    const engine::usize textures_before = textures.size();

    // One helper for all three maps: resolve the URI, decode, and store keyed
    // on the RESOLVED path. Keying on the URI as written would re-upload a
    // texture that two materials name by different relative paths, and the
    // scene would render identically - the failure is a VRAM figure, not a
    // picture.
    //
    // `space` is a parameter and not a default, because the three maps do not
    // agree: albedo is authored colour, normal and metal-rough are data. It is
    // also part of the store's key, so one file used both ways is two textures,
    // which is the correct answer rather than a wasted one - three of this
    // scene's 286 images are referenced both ways.
    auto load_texture = [&](std::string_view uri, engine::assets::gpu::ColorSpace space)
        -> engine::assets::TextureHandle {
        if (uri.empty()) {
            return {};  // the bridge substitutes a built-in default
        }
        std::string resolved;
        if (!resolve_content(loader, uri, resolved)) {
            ++stats.missing_textures;
            return {};
        }
        engine::assets::ImageData image{};
        if (!decode_image_file(resolved, image)) {
            ++stats.missing_textures;
            return {};
        }
        return textures.store(device, resolved, image, space);
    };

    // add_material / add_instance, not a hand-rolled cap check: they already do
    // the bound, scene::warn_full's per-kind warn-once latch - the thing that
    // stops a full scene logging at 60 Hz - and the invalid-handle return.
    for (const auto& material : src.materials) {
        engine::scene::Material dst{};
        dst.albedo = load_texture(material.albedo_uri, engine::assets::gpu::ColorSpace::Srgb);
        dst.normal = load_texture(material.normal_uri, engine::assets::gpu::ColorSpace::Linear);
        dst.metallic_roughness = load_texture(material.metallic_roughness_uri,
            engine::assets::gpu::ColorSpace::Linear);
        dst.metallic = material.metallic;
        dst.roughness = material.roughness;
        if (engine::scene::add_material(out, dst) == engine::scene::kInvalidMaterial) {
            break;  // capacity reached; it logged once and named the knob
        }
    }

    std::vector<engine::assets::MeshHandle> mesh_handles;
    mesh_handles.reserve(src.meshes.size());
    for (engine::usize i = 0; i < src.meshes.size(); ++i) {
        // Index-keyed, so one import per process. That is all this file is for
        // - a second scene in one run would alias the first's meshes, and by
        // then `document` owns this path.
        char key[64];
        std::snprintf(key, sizeof(key), "gltf_mesh_%zu", i);
        mesh_handles.push_back(meshes.store(device, key, src.meshes[i]));
    }

    for (const auto& node : src.nodes) {
        if (node.material >= out.material_count || node.mesh >= mesh_handles.size()) {
            ++stats.skipped_nodes;
            continue;
        }
        engine::scene::Instance inst{};
        inst.mesh = mesh_handles[node.mesh];
        inst.model = node.transform;
        inst.material = static_cast<engine::scene::MaterialHandle>(node.material);
        // cgltf_node_transform_world already composed the parent chain, so a
        // parent here would apply it a second time.
        inst.parent = engine::scene::kInvalidInstance;
        if (engine::scene::add_instance(out, inst) == engine::scene::kInvalidInstance) {
            break;  // capacity reached; logged once
        }
    }

    stats.materials = out.material_count;
    stats.instances = out.instance_count;
    // The delta, not textures.size(): the store is shared with the demo, which
    // has already put its own entries in it, and the question this answers is
    // what the scene cost.
    stats.textures = static_cast<engine::u32>(textures.size() - textures_before);
    return true;
}

} // namespace

bool import_scene_from_cvar(engine::rhi::IDevice& device, engine::assets::IAssetLoader& loader,
    engine::assets::gpu::GpuMeshStore& meshes, engine::assets::gpu::GpuTextureStore& textures,
    engine::scene::World& world) {
    const std::string_view knob = cv_scene.as_string();
    if (knob.empty()) {
        return false;
    }
    const std::string virtual_path = content_virtual_path(knob);
    std::string physical;
    if (!resolve_content(loader, virtual_path, physical)) {
        return false;  // resolve_content logged which path
    }

    auto gltf_loader = engine::assets::gltf::create_mesh_loader();
    engine::assets::gltf::GltfSceneResult scene;
    if (!gltf_loader->load_scene(physical, scene)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Assets,
            std::string("r.scene: failed to load ") + virtual_path);
        return false;
    }

    // The demo's instances and materials go; its lighting stays. Counts rather
    // than a fresh World, which would also wipe the sun, the ambient and the
    // point light setup_forward_demo tuned and leave the scene lit by nothing -
    // a glTF this engine reads carries no lights of its own.
    world.instance_count = 0;
    world.material_count = 0;
    world.names.count = 1;

    ImportStats stats{};
    if (!import_gltf_scene(scene, device, loader, meshes, textures, world, stats)) {
        return false;
    }

    char message[224];
    std::snprintf(message, sizeof(message),
        "instances=%u/%u materials=%u/%u textures=%u skipped_nodes=%u missing_textures=%u",
        stats.instances, engine::scene::kMaxInstances, stats.materials,
        engine::scene::kMaxMaterials, stats.textures, stats.skipped_nodes,
        stats.missing_textures);
    engine::log(stats.missing_textures > 0 ? engine::LogLevel::Warn : engine::LogLevel::Info,
        engine::LogChannel::Assets,
        std::string("Imported ") + virtual_path + ": " + message);
    return true;
}

} // namespace sandbox
