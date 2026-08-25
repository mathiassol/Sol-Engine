#pragma once

#include <engine/core/hash.hpp>
#include <engine/core/types.hpp>
#include <engine/math/aabb.hpp>

#include <string_view>
#include <vector>

namespace engine::assets {

struct MeshHandle {
    u64 id = 0;
    u32 generation = 0;

    bool valid() const { return id != 0 && generation != 0; }
    bool operator==(MeshHandle other) const {
        return id == other.id && generation == other.generation;
    }
    bool operator!=(MeshHandle other) const { return !(*this == other); }
};

inline MeshHandle make_mesh_handle(std::string_view key, u32 generation = 1) {
    MeshHandle handle{fnv1a64(key), generation};
    if (handle.id == 0) {
        handle.id = 1;
    }
    return handle;
}

struct VertexPN {
    f32 px = 0.f, py = 0.f, pz = 0.f;
    f32 nx = 0.f, ny = 0.f, nz = 0.f;
    f32 u = 0.f, v = 0.f;
};

struct MeshData {
    std::vector<VertexPN> vertices;
    std::vector<u32> indices;
    math::Aabb bounds = math::Aabb::empty();
};

inline void compute_mesh_bounds(MeshData& mesh) {
    mesh.bounds = math::Aabb::empty();
    for (const VertexPN& vertex : mesh.vertices) {
        mesh.bounds.include({vertex.px, vertex.py, vertex.pz});
    }
}

class IMeshLoader {
public:
    virtual ~IMeshLoader() = default;
    virtual bool load(std::string_view path, MeshData& out) = 0;
};

} // namespace engine::assets
