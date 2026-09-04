#pragma once

#include <engine/core/hash.hpp>
#include <engine/core/types.hpp>

#include <string_view>

namespace engine::assets {

// Mirrors MeshHandle. A generation makes a handle stale after unload rather
// than silently addressing whatever took the slot.
struct TextureHandle {
    u64 id = 0;
    u32 generation = 0;

    bool valid() const { return id != 0 && generation != 0; }
    bool operator==(TextureHandle other) const {
        return id == other.id && generation == other.generation;
    }
    bool operator!=(TextureHandle other) const { return !(*this == other); }
};

inline TextureHandle make_texture_handle(std::string_view key, u32 generation = 1) {
    TextureHandle handle{fnv1a64(key), generation};
    if (handle.id == 0) {
        handle.id = 1;
    }
    return handle;
}

} // namespace engine::assets
