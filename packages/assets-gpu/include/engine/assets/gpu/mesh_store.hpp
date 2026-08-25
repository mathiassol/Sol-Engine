#pragma once

#include <engine/assets/gpu/mesh_upload.hpp>
#include <engine/assets/mesh.hpp>

#include <string>
#include <string_view>
#include <unordered_map>

namespace engine::assets::gpu {

class GpuMeshStore {
public:
    MeshHandle store(rhi::IDevice& device, std::string_view key, const MeshData& mesh);
    bool unload(MeshHandle handle);
    const GpuMesh* get(MeshHandle handle) const;

private:
    struct Entry {
        std::string key;
        GpuMesh gpu;
        u32 generation = 0;
        bool live = false;
    };

    std::unordered_map<u64, Entry> entries_;
};

} // namespace engine::assets::gpu
