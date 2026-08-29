#pragma once

#include <engine/assets/image.hpp>

#include <span>
#include <string_view>

namespace engine::assets::png {

bool load_png_bytes(std::span<const u8> bytes, ImageData& out);
bool load_png_file(std::string_view path, ImageData& out);

} // namespace engine::assets::png
