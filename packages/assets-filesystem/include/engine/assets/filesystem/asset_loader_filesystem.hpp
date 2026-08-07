#pragma once

#include <engine/assets/asset_loader.hpp>
#include <engine/platform/filesystem.hpp>

#include <memory>

namespace engine::assets::filesystem {

std::unique_ptr<IAssetLoader> create_asset_loader(platform::IFileSystem& fs);

} // namespace engine::assets::filesystem
