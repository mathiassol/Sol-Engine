#pragma once

#include <engine/rhi/rhi.hpp>

#include <memory>

namespace engine::rhi::d3d12 {

std::unique_ptr<IRHI> create_rhi();

} // namespace engine::rhi::d3d12
