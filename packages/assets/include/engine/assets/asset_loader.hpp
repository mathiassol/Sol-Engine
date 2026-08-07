#pragma once

#include <engine/core/types.hpp>

#include <span>
#include <string_view>
#include <vector>

namespace engine::assets {

struct AssetHandle {
    u64 id = 0;

    bool valid() const { return id != 0; }
};

// Asset data is separate from GPU resources (see Philosophy.md).
class IAssetLoader {
public:
    virtual ~IAssetLoader() = default;

    virtual bool load_bytes(std::string_view path, std::vector<u8>& out) = 0;
    virtual void unload(AssetHandle handle) = 0;
};

} // namespace engine::assets
