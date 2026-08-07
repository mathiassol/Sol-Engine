#pragma once

#include <engine/platform/platform.hpp>

namespace engine::platform::win32 {

std::unique_ptr<IPlatform> create_platform();

} // namespace engine::platform::win32
