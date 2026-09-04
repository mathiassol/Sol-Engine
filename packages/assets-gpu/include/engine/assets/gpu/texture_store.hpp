#pragma once

#include <engine/assets/image.hpp>
#include <engine/assets/texture.hpp>
#include <engine/rhi/device.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace engine::assets::gpu {

// Which of the two RGBA8 formats a texture's bytes want. The rule - and why
// the two are not interchangeable - is owned by the `Format` comment in
// packages/rhi/include/engine/rhi/resources.hpp; this enum only names the
// choice so a caller has to make it.
enum class ColorSpace : u8 { Srgb, Linear };

// GPU textures keyed by their resolved content path *and* colour space.
//
// The dedupe is the point, not an optimisation: a scene shares one texture
// across many materials, and a store that uploaded every reference separately
// would render identically while costing several times the VRAM. Nothing but
// a count catches that, which is what run_texture_store_gate asserts.
//
// Two things a caller has to know:
//
// - **A live re-store discards the new pixels.** `image` is read only when
//   there is no live entry under the key; a second store() of a resident path
//   hands back the existing handle and uploads nothing. That is deliberate
//   path-keyed cache semantics - refreshing a texture's contents means
//   unload() first.
// - **The store must be destroyed before the device.** Entries own
//   `unique_ptr<rhi::ITexture>`, so a store that outlives the IDevice that
//   created its textures releases them against a dead device.
class GpuTextureStore {
public:
    // `space` has no default on purpose. A default is how a normal map ends up
    // gamma-decoded on sample - the caller who did not think about colour
    // space silently gets whichever one this header guessed, a stored 0.5
    // samples as ~0.21, and every TBN normal is wrong with nothing failing.
    // The choice is forced at every call site instead.
    //
    // Returns `{}` and logs when the upload fails; nothing is inserted, so a
    // transient out-of-VRAM does not poison the key for the process lifetime.
    TextureHandle store(rhi::IDevice& device, std::string_view key, const ImageData& image,
        ColorSpace space);
    bool unload(TextureHandle handle);
    const rhi::ITexture* get(TextureHandle handle) const;
    // Non-const twin. Everything that actually uses a texture takes it by
    // non-const reference - ICommandList::set_shader_resource, cmd.transition,
    // IDevice::read_texture - so the const accessor alone cannot serve them.
    rhi::ITexture* get(TextureHandle handle);

    // Live entries: residency, which unload() decrements.
    usize size() const { return live_count_; }

    // Successful create_texture calls, monotonic and never decremented.
    //
    // Separate from size() because they answer different questions and only
    // this one can fail the gate: a store that re-uploaded over a live entry
    // would leave live_count_ at the distinct-path count while doing twice the
    // work, so an assertion on residency alone passes green.
    usize upload_count() const { return upload_count_; }

private:
    struct Entry {
        // The composed key (path plus colour space), kept so store() can
        // detect a hash collision instead of handing back another path's
        // texture. Compared, not just stored - see the check in store().
        std::string key;
        std::unique_ptr<rhi::ITexture> gpu;
        u32 generation = 0;
        bool live = false;
    };

    std::unordered_map<u64, Entry> entries_;
    usize live_count_ = 0;
    usize upload_count_ = 0;
};

} // namespace engine::assets::gpu
