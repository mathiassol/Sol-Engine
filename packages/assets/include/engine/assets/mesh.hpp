#pragma once

#include <engine/core/types.hpp>

#include <string_view>
#include <vector>

namespace engine::assets {

struct VertexPN {
    f32 px = 0.f, py = 0.f, pz = 0.f;
    f32 nx = 0.f, ny = 0.f, nz = 0.f;
};

struct MeshData {
    std::vector<VertexPN> vertices;
    std::vector<u32> indices;
};

class IMeshLoader {
public:
    virtual ~IMeshLoader() = default;
    virtual bool load(std::string_view path, MeshData& out) = 0;
};

} // namespace engine::assets
