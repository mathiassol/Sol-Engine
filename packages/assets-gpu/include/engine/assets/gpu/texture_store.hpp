#pragma once

#include <engine/assets/image.hpp>
#include <engine/assets/texture.hpp>
#include <engine/rhi/device.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace engine::assets::gpu {

// GPU textures keyed by their resolved content path.
//
// The dedupe is the point, not an optimisation: a scene shares one texture
// across many materials, and a store that uploaded every reference separately
// would render identically while costing several times the VRAM. Nothing but
// a count catches that, which is what run_texture_store_gate asserts.
class GpuTextureStore {
public:
    TextureHandle store(rhi::IDevice& device, std::string_view key, const ImageData& image);
    bool unload(TextureHandle handle);
    const rhi::ITexture* get(TextureHandle handle) const;

    // Live entries. The gate compares this against the number of store()
    // calls; equality means dedupe is not happening.
    usize size() const { return live_count_; }

private:
    struct Entry {
        std::string key;
        std::unique_ptr<rhi::ITexture> gpu;
        u32 generation = 0;
        bool live = false;
    };

    std::unordered_map<u64, Entry> entries_;
    usize live_count_ = 0;
};

} // namespace engine::assets::gpu
