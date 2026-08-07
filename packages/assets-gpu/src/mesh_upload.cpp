#include <engine/assets/gpu/mesh_upload.hpp>

namespace engine::assets::gpu {

GpuMesh upload_mesh(rhi::IDevice& device, const MeshData& mesh) {
    GpuMesh gpu{};

    rhi::BufferDesc vb_desc{};
    vb_desc.size  = mesh.vertices.size() * sizeof(VertexPN);
    vb_desc.usage = rhi::BufferUsage::Vertex;
    gpu.vertex_buffer = device.create_buffer(vb_desc, mesh.vertices.data());

    rhi::BufferDesc ib_desc{};
    ib_desc.size  = mesh.indices.size() * sizeof(u32);
    ib_desc.usage = rhi::BufferUsage::Index;
    gpu.index_buffer = device.create_buffer(ib_desc, mesh.indices.data());

    gpu.index_count = static_cast<u32>(mesh.indices.size());
    return gpu;
}

} // namespace engine::assets::gpu
