#include <engine/assets/filesystem/asset_loader_filesystem.hpp>

#include <engine/core/log.hpp>

#include <unordered_map>

namespace engine::assets::filesystem {

namespace {

class FileSystemAssetLoader final : public IAssetLoader {
public:
    explicit FileSystemAssetLoader(platform::IFileSystem& fs) : fs_(fs) {}

    bool load_bytes(std::string_view path, std::vector<u8>& out) override {
        return fs_.read_file(path, out);
    }

    void unload(AssetHandle handle) override {
        cache_.erase(handle.id);
    }

private:
    platform::IFileSystem& fs_;
    std::unordered_map<u64, std::vector<u8>> cache_;
};

} // namespace

std::unique_ptr<IAssetLoader> create_asset_loader(platform::IFileSystem& fs) {
    return std::make_unique<FileSystemAssetLoader>(fs);
}

} // namespace engine::assets::filesystem
