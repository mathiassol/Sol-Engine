#pragma once

#include <engine/rhi/rhi.hpp>

#include <memory>

namespace engine::rhi::vulkan {

std::unique_ptr<IRHI> create_rhi();

} // namespace engine::rhi::vulkan
