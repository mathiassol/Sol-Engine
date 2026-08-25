#pragma once

#include <engine/assets/mesh.hpp>
#include <engine/rhi/device.hpp>

#include <memory>

namespace engine::assets::gpu {

struct GpuMesh {
    std::unique_ptr<rhi::IBuffer> vertex_buffer;
    std::unique_ptr<rhi::IBuffer> index_buffer;
    math::Aabb bounds = math::Aabb::empty();
    u32 index_count = 0;
};

GpuMesh upload_mesh(rhi::IDevice& device, const MeshData& mesh);

} // namespace engine::assets::gpu
