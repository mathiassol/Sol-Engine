#pragma once

#include <engine/assets/mesh.hpp>
#include <engine/math/mat4.hpp>

#include <memory>
#include <string>
#include <vector>

namespace engine::assets::gltf {

struct GltfPrimitive {
    u32 first_index = 0;
    u32 index_count = 0;
    std::string albedo_uri;
    std::string metallic_roughness_uri;
    std::string normal_uri;
    engine::f32 metallic = 0.f;
    engine::f32 roughness = 1.f;
};

struct GltfLoadResult {
    MeshData mesh;
    std::vector<GltfPrimitive> primitives;
    std::string albedo_uri;
    engine::f32 metallic = 0.f;
    engine::f32 roughness = 1.f;
};

// One drawable node: a world transform and the mesh and material it draws.
// Additive - the baked GltfLoadResult path above stays, because the husky and
// three gates use it.
struct GltfNode {
    math::Mat4 transform = math::Mat4::identity();
    u32 mesh = 0;
    u32 material = 0;
};

struct GltfMaterial {
    std::string albedo_uri;
    std::string metallic_roughness_uri;
    std::string normal_uri;
    f32 metallic = 0.f;
    f32 roughness = 1.f;
};

struct GltfSceneResult {
    std::vector<MeshData> meshes;
    std::vector<GltfMaterial> materials;
    std::vector<GltfNode> nodes;
};

class IGltfLoader {
public:
    virtual ~IGltfLoader() = default;
    virtual bool load(std::string_view path, GltfLoadResult& out) = 0;
    // Returns nodes with their transforms rather than one welded mesh.
    virtual bool load_scene(std::string_view path, GltfSceneResult& out) = 0;
};

std::unique_ptr<IGltfLoader> create_mesh_loader();

} // namespace engine::assets::gltf
