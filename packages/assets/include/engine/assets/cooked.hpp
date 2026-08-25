#pragma once

#include <engine/assets/image.hpp>
#include <engine/assets/mesh.hpp>
#include <engine/core/types.hpp>

#include <span>
#include <vector>

namespace engine::assets {

inline constexpr u32 kCookedVersion = 1;

enum class CookedKind : u32 {
    Mesh = 1,
    Image = 2,
    Audio = 3,
};

struct CookedAudio {
    u32 sample_rate = 0;
    u16 channels = 0;
    u16 bits_per_sample = 0;
    std::vector<u8> pcm;
};

bool peek_cooked_kind(std::span<const u8> bytes, CookedKind& out);

bool write_cooked_mesh(const MeshData& mesh, std::vector<u8>& out);
bool read_cooked_mesh(std::span<const u8> bytes, MeshData& out);

bool write_cooked_image(const ImageData& image, std::vector<u8>& out);
bool read_cooked_image(std::span<const u8> bytes, ImageData& out);

bool write_cooked_audio(const CookedAudio& audio, std::vector<u8>& out);
bool read_cooked_audio(std::span<const u8> bytes, CookedAudio& out);

} // namespace engine::assets
