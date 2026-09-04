#include <engine/assets/gpu/texture_store.hpp>

#include <engine/core/log.hpp>

#include <cstdio>

namespace engine::assets::gpu {

namespace {

const char* space_suffix(ColorSpace space) {
    return space == ColorSpace::Srgb ? "#srgb" : "#linear";
}

// The colour space is part of the key, not just of the desc.
//
// The same PNG legitimately serves as an sRGB albedo in one material and a
// linear mask in another. Keyed on the path alone, the second caller gets the
// first caller's format - and the dedupe count still reads as correct, because
// one upload for one path is exactly what the store is supposed to do. So the
// key is the path *plus* the space, which makes the two variants two entries.
std::string compose_key(std::string_view key, ColorSpace space) {
    std::string composed(key);
    composed += space_suffix(space);
    return composed;
}

std::unique_ptr<rhi::ITexture> upload(
    rhi::IDevice& device, const ImageData& image, ColorSpace space) {
    rhi::TextureDesc desc{};
    desc.width = image.width;
    desc.height = image.height;
    desc.mip_levels = 0; // 0 means "generate a full chain" - see TextureDesc
    desc.format = space == ColorSpace::Srgb ? rhi::Format::RGBA8_UNORM_SRGB
                                            : rhi::Format::RGBA8_UNORM;
    desc.usage = rhi::TextureUsage::ShaderResource;
    return device.create_texture(desc, image.rgba.data());
}

} // namespace

TextureHandle GpuTextureStore::store(
    rhi::IDevice& device, std::string_view key, const ImageData& image, ColorSpace space) {
    const std::string entry_key = compose_key(key, space);
    const TextureHandle handle = make_texture_handle(entry_key);
    const auto it = entries_.find(handle.id);
    if (it != entries_.end() && it->second.live) {
        if (it->second.key != entry_key) {
            // Two keys collided in fnv1a64. Without this comparison store()
            // would hand back the other key's texture and every material
            // naming this one would sample the wrong pixels, with no log line
            // and no gate. Vanishingly unlikely; the field was already paid
            // for, so refuse instead of guessing.
            char message[288];
            std::snprintf(message, sizeof(message),
                "GpuTextureStore: key collision - '%s' and '%s' share an id; no handle issued",
                it->second.key.c_str(), entry_key.c_str());
            log(LogLevel::Error, LogChannel::Render, message);
            return {};
        }
        // Already resident under this path and colour space. Returning the
        // existing handle is what makes a shared texture cost one upload
        // instead of one per material that names it.
        return TextureHandle{handle.id, it->second.generation};
    }

    std::unique_ptr<rhi::ITexture> gpu = upload(device, image, space);
    if (!gpu) {
        // Nothing is inserted and no counter moves. Storing the null as a live
        // entry made get() return nullptr for a *live* handle - so a caller
        // could not tell a stale handle from a failed upload - overstated
        // size(), and sent every later store() of this key down the dedupe
        // branch with the poisoned handle, breaking the texture for the
        // process lifetime after one transient VRAM spike.
        char message[288];
        std::snprintf(message, sizeof(message),
            "GpuTextureStore: upload failed for '%s' (%ux%u) - no handle issued",
            entry_key.c_str(), image.width, image.height);
        log(LogLevel::Error, LogChannel::Render, message);
        return {};
    }
    device.set_debug_name(*gpu, entry_key);

    Entry entry;
    entry.key = entry_key;
    entry.gpu = std::move(gpu);
    entry.generation = (it != entries_.end()) ? it->second.generation + 1 : 1;
    if (entry.generation == 0) {
        entry.generation = 1; // Wrap guard, matching GpuMeshStore::store.
    }
    entry.live = true;
    const u32 generation = entry.generation;
    entries_[handle.id] = std::move(entry);
    ++live_count_;
    ++upload_count_;
    return TextureHandle{handle.id, generation};
}

bool GpuTextureStore::unload(TextureHandle handle) {
    const auto it = entries_.find(handle.id);
    if (it == entries_.end() || !it->second.live || it->second.generation != handle.generation) {
        return false;
    }
    it->second.gpu.reset();
    it->second.live = false;
    it->second.generation += 1;
    if (it->second.generation == 0) {
        it->second.generation = 1;
    }
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

rhi::ITexture* GpuTextureStore::get(TextureHandle handle) {
    const auto it = entries_.find(handle.id);
    if (it == entries_.end() || !it->second.live || it->second.generation != handle.generation) {
        return nullptr;
    }
    return it->second.gpu.get();
}

} // namespace engine::assets::gpu
