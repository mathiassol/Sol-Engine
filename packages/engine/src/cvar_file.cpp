#include <engine/cvar_file.hpp>

#include <engine/platform/filesystem.hpp>

#include <vector>

namespace engine {

CvarApplyStats apply_cvar_file(platform::IFileSystem& fs, std::string_view path, bool* found) {
    std::vector<u8> bytes;
    const bool read = fs.read_file(path, bytes);
    if (found) {
        *found = read;
    }
    if (!read || bytes.empty()) {
        return {};  // an empty file has nothing to apply, and data() may be null
    }
    return apply_cvar_text(
        std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
        CvarSource::File);
}

} // namespace engine
