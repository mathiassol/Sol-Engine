#include <engine/assets/gpu/texture_store.hpp>

namespace engine::assets::gpu {

namespace {

std::unique_ptr<rhi::ITexture> upload(rhi::IDevice& device, const ImageData& image) {
    rhi::TextureDesc desc{};
    desc.width = image.width;
    desc.height = image.height;
    desc.mip_levels = 0; // 0 means "generate a full chain" - see TextureDesc
    desc.format = rhi::Format::RGBA8_UNORM_SRGB;
    desc.usage = rhi::TextureUsage::ShaderResource;
    return device.create_texture(desc, image.rgba.data());
}

} // namespace

TextureHandle GpuTextureStore::store(
    rhi::IDevice& device, std::string_view key, const ImageData& image) {
    const TextureHandle handle = make_texture_handle(key);
    const auto it = entries_.find(handle.id);
    if (it != entries_.end() && it->second.live) {
        // Already resident under this path. Returning the existing handle is
        // what makes a shared texture cost one upload instead of one per
        // material that names it.
        return TextureHandle{handle.id, it->second.generation};
    }
    Entry entry;
    entry.key = std::string(key);
    entry.gpu = upload(device, image);
    entry.generation = (it != entries_.end()) ? it->second.generation + 1 : 1;
    entry.live = true;
    entries_[handle.id] = std::move(entry);
    ++live_count_;
    return TextureHandle{handle.id, entries_[handle.id].generation};
}

bool GpuTextureStore::unload(TextureHandle handle) {
    const auto it = entries_.find(handle.id);
    if (it == entries_.end() || !it->second.live || it->second.generation != handle.generation) {
        return false;
    }
    it->second.gpu.reset();
    it->second.live = false;
    it->second.generation += 1;
    --live_count_;
    return true;
}

const rhi::ITexture* GpuTextureStore::get(TextureHandle handle) const {
    const auto it = entries_.find(handle.id);
    if (it == entries_.end() || !it->second.live || it->second.generation != handle.generation) {
        return nullptr;
    }
    return it->second.gpu.get();
}

} // namespace engine::assets::gpu
