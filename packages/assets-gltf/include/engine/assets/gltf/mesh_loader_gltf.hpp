#pragma once

#include <engine/assets/mesh.hpp>

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

class IGltfLoader {
public:
    virtual ~IGltfLoader() = default;
    virtual bool load(std::string_view path, GltfLoadResult& out) = 0;
};

std::unique_ptr<IGltfLoader> create_mesh_loader();

} // namespace engine::assets::gltf
