#pragma once

#include <engine/assets/asset_loader.hpp>
#include <engine/core/types.hpp>

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine::assets {

inline constexpr u32 kPakVersion = 1;

struct PakEntry {
    std::string name;
    std::vector<u8> bytes;
};

bool peek_pak(std::span<const u8> bytes);
bool write_pak(std::span<const PakEntry> entries, std::vector<u8>& out);
bool read_pak_entry(std::span<const u8> bytes, std::string_view name, std::vector<u8>& out);

std::unique_ptr<IAssetLoader> create_pak_loader(std::span<const u8> bytes);

} // namespace engine::assets
