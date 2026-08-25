#pragma once

#include <engine/core/cvar.hpp>

#include <string_view>

namespace engine::platform {
class IFileSystem;
}

namespace engine {

// Reads path through the filesystem and applies it as CvarSource::File. This is
// the only place a path becomes cvar text; core never opens a file.
//
// found (when given) reports whether the file existed. A missing file is not an
// error: most installs have no config.cfg.
CvarApplyStats apply_cvar_file(platform::IFileSystem& fs, std::string_view path,
    bool* found = nullptr);

// Looked for at <content_root>/config.cfg.
inline constexpr const char* kCvarFileName = "config.cfg";

} // namespace engine
