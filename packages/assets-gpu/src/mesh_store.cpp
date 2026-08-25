#include <engine/assets/gpu/mesh_store.hpp>

namespace engine::assets::gpu {

MeshHandle GpuMeshStore::store(rhi::IDevice& device, std::string_view key, const MeshData& mesh) {
    GpuMesh gpu = upload_mesh(device, mesh);
    if (!gpu.vertex_buffer || !gpu.index_buffer) {
        return {};
    }

    const MeshHandle key_handle = make_mesh_handle(key);
    auto it = entries_.find(key_handle.id);
    if (it == entries_.end()) {
        Entry entry{};
        entry.key = std::string(key);
        entry.gpu = std::move(gpu);
        entry.generation = 1;
        entry.live = true;
        entries_.emplace(key_handle.id, std::move(entry));
        return MeshHandle{key_handle.id, 1};
    }

    Entry& entry = it->second;
    if (entry.live) {
        entry.gpu = std::move(gpu);
        entry.key = std::string(key);
        return MeshHandle{key_handle.id, entry.generation};
    }

    ++entry.generation;
    if (entry.generation == 0) {
        entry.generation = 1;
    }
    entry.gpu = std::move(gpu);
    entry.key = std::string(key);
    entry.live = true;
    return MeshHandle{key_handle.id, entry.generation};
}

bool GpuMeshStore::unload(MeshHandle handle) {
    auto it = entries_.find(handle.id);
    if (it == entries_.end() || !it->second.live || it->second.generation != handle.generation) {
        return false;
    }
    it->second.live = false;
    it->second.gpu = {};
    return true;
}

const GpuMesh* GpuMeshStore::get(MeshHandle handle) const {
    const auto it = entries_.find(handle.id);
    if (it == entries_.end() || !it->second.live || it->second.generation != handle.generation) {
        return nullptr;
    }
    return &it->second.gpu;
}

} // namespace engine::assets::gpu
