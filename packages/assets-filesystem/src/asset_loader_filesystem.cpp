#include <engine/assets/filesystem/asset_loader_filesystem.hpp>

#include <engine/core/log.hpp>

#include <filesystem>
#include <string>
#include <unordered_map>

namespace engine::assets::filesystem {

namespace {

std::string normalize_mount_name(std::string_view name) {
    while (!name.empty() && (name.front() == '/' || name.front() == '\\')) {
        name.remove_prefix(1);
    }
    while (!name.empty() && (name.back() == '/' || name.back() == '\\')) {
        name.remove_suffix(1);
    }
    return std::string(name);
}

class FileSystemAssetLoader final : public IAssetLoader {
public:
    explicit FileSystemAssetLoader(platform::IFileSystem& fs) : fs_(fs) {}

    bool mount(std::string_view name, std::string_view physical_root) override {
        const std::string mount_name = normalize_mount_name(name);
        if (mount_name.empty() || physical_root.empty()) {
            return false;
        }

        std::error_code ec;
        const auto root
            = std::filesystem::weakly_canonical(std::filesystem::path(physical_root), ec);
        if (ec || root.empty()) {
            log(LogLevel::Error, LogChannel::Assets, "Failed to mount content root");
            return false;
        }

        mounts_[mount_name] = root.string();
        return true;
    }

    bool resolve_path(
        std::string_view virtual_or_physical, std::string& out_physical) const override {
        if (virtual_or_physical.empty()) {
            return false;
        }

        if (virtual_or_physical.front() == '/' || virtual_or_physical.front() == '\\') {
            std::string_view rest = virtual_or_physical;
            while (!rest.empty() && (rest.front() == '/' || rest.front() == '\\')) {
                rest.remove_prefix(1);
            }

            const usize slash = rest.find_first_of("/\\");
            const std::string mount_name = std::string(
                rest.substr(0, slash == std::string_view::npos ? rest.size() : slash));
            const auto it = mounts_.find(mount_name);
            if (it == mounts_.end()) {
                return false;
            }

            const std::filesystem::path root{it->second};
            std::filesystem::path physical = root;
            if (slash != std::string_view::npos) {
                const std::string relative(rest.substr(slash + 1));

                // operator/= *discards the left side* when the right has a
                // root name, so "/content/C:/Windows/win.ini" resolved to
                // C:/Windows/win.ini and "/content//server/share/x" opened a
                // UNC connection. Reject anything that is not plainly relative.
                const std::filesystem::path tail{relative};
                if (tail.is_absolute() || tail.has_root_name() || tail.has_root_directory()) {
                    log(LogLevel::Error, LogChannel::Assets,
                        "Asset path rejected: absolute component after the mount name");
                    return false;
                }
                physical /= tail;
            }

            physical = physical.lexically_normal();

            // `..` was previously collapsed rather than rejected, so a path
            // could climb out of its mount. Confirm the normalised result is
            // still inside the root - this is the containment check the mount
            // is supposed to provide.
            const std::filesystem::path normal_root = root.lexically_normal();
            const auto relative_to_root = physical.lexically_relative(normal_root);
            if (relative_to_root.empty() || *relative_to_root.begin() == "..") {
                log(LogLevel::Error, LogChannel::Assets,
                    "Asset path rejected: escapes its mount root");
                return false;
            }

            out_physical = physical.string();
            return true;
        }

        out_physical = fs_.resolve(virtual_or_physical);
        return !out_physical.empty();
    }

    bool load_bytes(std::string_view path, std::vector<u8>& out) override {
        std::string physical;
        if (!resolve_path(path, physical)) {
            return false;
        }
        return fs_.read_file(physical, out);
    }

    void unload(AssetHandle handle) override {
        cache_.erase(handle.id);
    }

private:
    platform::IFileSystem& fs_;
    std::unordered_map<std::string, std::string> mounts_;
    std::unordered_map<u64, std::vector<u8>> cache_;
};

} // namespace

std::unique_ptr<IAssetLoader> create_asset_loader(platform::IFileSystem& fs) {
    return std::make_unique<FileSystemAssetLoader>(fs);
}

} // namespace engine::assets::filesystem
