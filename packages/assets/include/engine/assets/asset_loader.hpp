#pragma once

#include <engine/core/types.hpp>

#include <string>
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

    // Mount name "content" maps "/content/..." to physical_root.
    virtual bool mount(std::string_view name, std::string_view physical_root) = 0;
    virtual bool resolve_path(
        std::string_view virtual_or_physical, std::string& out_physical) const = 0;

    virtual bool load_bytes(std::string_view path, std::vector<u8>& out) = 0;
    virtual void unload(AssetHandle handle) = 0;
};

} // namespace engine::assets
