#include <engine/shaders/dxc/shader_compiler_dxc.hpp>

#include <engine/core/hash.hpp>
#include <engine/core/log.hpp>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace engine::shaders::dxc {

namespace {

constexpr char kCacheMagic[4] = {'E', 'N', 'S', 'C'};
constexpr u32 kCacheVersion = 3;

bool gpu_debug_enabled() {
#ifdef NDEBUG
    return false;
#else
    char value[8] = {};
    return GetEnvironmentVariableA("ENGINE_GPU_DEBUG", value, sizeof(value)) > 0 && value[0] == '1';
#endif
}

bool read_file_bytes(const std::filesystem::path& path, std::vector<u8>& out) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }
    const auto size = file.tellg();
    if (size < 0) {
        return false;
    }
    out.resize(static_cast<size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(out.data()), size);
    return file.good() || file.eof();
}

void fold_source_file(u64& hash, const std::filesystem::path& path, int depth,
    std::unordered_set<std::string>& seen) {
    if (depth > 8) {
        return;
    }
    std::error_code ec;
    const std::string key = std::filesystem::weakly_canonical(path, ec).string();
    const std::string seen_key = key.empty() ? path.lexically_normal().string() : key;
    if (!seen.insert(seen_key).second) {
        return;
    }

    std::vector<u8> bytes;
    if (!read_file_bytes(path, bytes)) {
        return;
    }
    hash ^= fnv1a64(std::span<const u8>{bytes.data(), bytes.size()});
    hash *= kFnvPrime64;

    const std::string text(bytes.begin(), bytes.end());
    usize pos = 0;
    while ((pos = text.find("#include", pos)) != std::string::npos) {
        const usize quote = text.find('"', pos);
        const usize angle = text.find('<', pos);
        const usize newline = text.find('\n', pos);
        if (newline != std::string::npos && quote > newline) {
            pos += 8;
            continue;
        }
        if (angle != std::string::npos && (quote == std::string::npos || angle < quote)) {
            pos += 8;
            continue;
        }
        if (quote == std::string::npos) {
            pos += 8;
            continue;
        }
        const usize quote_end = text.find('"', quote + 1);
        if (quote_end == std::string::npos) {
            break;
        }
        const std::string include_path = text.substr(quote + 1, quote_end - quote - 1);
        fold_source_file(hash, path.parent_path() / include_path, depth + 1, seen);
        pos = quote_end + 1;
    }
}

u64 cache_key(const std::filesystem::path& file_path, const ShaderCompileDesc& desc, bool debug) {
    u64 hash = kFnvOffset64;
    std::unordered_set<std::string> seen;
    fold_source_file(hash, file_path, 0, seen);
    hash ^= fnv1a64(desc.entry_point);
    hash *= kFnvPrime64;
    hash ^= fnv1a64(desc.target_profile);
    hash *= kFnvPrime64;
    hash ^= static_cast<u64>(desc.target);
    hash *= kFnvPrime64;
    hash ^= fnv1a64("dxc");
    hash *= kFnvPrime64;
    hash ^= debug ? 1ull : 0ull;
    hash *= kFnvPrime64;
    return hash;
}

std::filesystem::path cache_file_path(const std::filesystem::path& dir, u64 hash) {
    char name[21];
    std::snprintf(name, sizeof(name), "%016llx.cso", static_cast<unsigned long long>(hash));
    return dir / name;
}

bool load_cache(const std::filesystem::path& path, u64 expected_hash, ShaderBytecode& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    char magic[4]{};
    u32 version = 0;
    u64 hash = 0;
    u32 size = 0;
    file.read(magic, 4);
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    file.read(reinterpret_cast<char*>(&hash), sizeof(hash));
    file.read(reinterpret_cast<char*>(&size), sizeof(size));
    if (!file || std::memcmp(magic, kCacheMagic, 4) != 0 || version != kCacheVersion
        || hash != expected_hash) {
        return false;
    }

    out.data.resize(size);
    file.read(reinterpret_cast<char*>(out.data.data()), static_cast<std::streamsize>(size));
    return static_cast<bool>(file);
}

bool save_cache(const std::filesystem::path& path, u64 hash, const ShaderBytecode& bytecode) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }

    const u32 size = static_cast<u32>(bytecode.data.size());
    file.write(kCacheMagic, 4);
    file.write(reinterpret_cast<const char*>(&kCacheVersion), sizeof(kCacheVersion));
    file.write(reinterpret_cast<const char*>(&hash), sizeof(hash));
    file.write(reinterpret_cast<const char*>(&size), sizeof(size));
    file.write(reinterpret_cast<const char*>(bytecode.data.data()),
        static_cast<std::streamsize>(bytecode.data.size()));
    return static_cast<bool>(file);
}

class CachedShaderCompiler final : public IShaderCompiler {
public:
    explicit CachedShaderCompiler(std::string cache_directory)
        : inner_(create_compiler()), cache_dir_(std::move(cache_directory)) {}

    bool last_compile_from_cache() const override { return from_cache_; }

    bool compile(
        const ShaderCompileDesc& desc, ShaderBytecode& out, std::string& error_log) override {
        from_cache_ = false;
        if (!inner_) {
            error_log = "Shader compiler missing";
            return false;
        }
        // No target check here. This wrapper caches whatever the inner
        // compiler can produce, and cache_key already folds desc.target, so
        // DXIL and SPIR-V of the same source land in different files. A second
        // rejection here is what made the SPIR-V path look unimplemented after
        // it was implemented - the sandbox uses the cached compiler, not the
        // bare one.

        const std::filesystem::path shader_path{std::string(desc.file_path)};
        if (!std::filesystem::exists(shader_path)) {
            error_log = "Failed to open shader file";
            return false;
        }

        const u64 hash = cache_key(shader_path, desc, gpu_debug_enabled());
        const auto cached_path = cache_file_path(cache_dir_, hash);
        if (load_cache(cached_path, hash, out)) {
            from_cache_ = true;
            return true;
        }

        if (!inner_->compile(desc, out, error_log)) {
            return false;
        }

        if (!save_cache(cached_path, hash, out)) {
            log(LogLevel::Warn, LogChannel::Render, "Failed to write shader disk cache");
        }
        return true;
    }

private:
    std::unique_ptr<IShaderCompiler> inner_;
    std::filesystem::path cache_dir_;
    bool from_cache_ = false;
};

} // namespace

std::unique_ptr<IShaderCompiler> create_cached_compiler(std::string_view cache_directory) {
    return std::make_unique<CachedShaderCompiler>(std::string(cache_directory));
}

} // namespace engine::shaders::dxc
