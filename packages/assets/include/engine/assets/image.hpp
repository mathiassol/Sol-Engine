#pragma once

#include <engine/core/types.hpp>

#include <vector>

namespace engine::assets {

struct ImageData {
    u32 width = 0;
    u32 height = 0;
    std::vector<u8> rgba;
};

} // namespace engine::assets
