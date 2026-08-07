#include <engine/shaders/dxc/shader_hot_reload_dxc.hpp>

#include <engine/core/log.hpp>
#include <engine/shaders/dxc/shader_compiler_dxc.hpp>

#include <algorithm>
#include <filesystem>
#include <optional>

namespace engine::shaders::dxc {

namespace {

std::optional<std::filesystem::file_time_type> file_mtime(std::string_view path) {
    std::error_code ec;
    const auto fs_path = std::filesystem::path(path);
    if (!std::filesystem::exists(fs_path, ec)) {
        return std::nullopt;
    }
    const auto write_time = std::filesystem::last_write_time(fs_path, ec);
    if (ec) {
        return std::nullopt;
    }
    return write_time;
}

std::optional<std::filesystem::file_time_type> newest_mtime(
    std::string_view vertex_path, std::string_view pixel_path) {
    std::optional<std::filesystem::file_time_type> newest;
    for (const auto path : {vertex_path, pixel_path}) {
        if (path.empty()) continue;
        const auto mtime = file_mtime(path);
        if (!mtime) continue;
        if (!newest || *mtime > *newest) {
            newest = mtime;
        }
    }
    return newest;
}

class DxcShaderHotReloader final : public IShaderHotReloader {
public:
    DxcShaderHotReloader() : compiler_(create_compiler()) {}

    void begin_watch(const WatchedShaderPair& sources) override {
        sources_ = sources;
        last_mtime_.reset();
        failed_mtime_.reset();
        last_mtime_ = newest_mtime(sources_.vertex.file_path, sources_.pixel.file_path);
    }

    ShaderReloadStatus poll(
        ShaderBytecode& out_vertex,
        ShaderBytecode& out_pixel,
        std::string& error_log) override {
        if (!compiler_ || sources_.vertex.file_path.empty()) {
            return ShaderReloadStatus::Unchanged;
        }

        const auto write_time = newest_mtime(sources_.vertex.file_path, sources_.pixel.file_path);
        if (!write_time) {
            return ShaderReloadStatus::Unchanged;
        }

        if (last_mtime_ && *write_time == *last_mtime_) {
            return ShaderReloadStatus::Unchanged;
        }
        if (failed_mtime_ && *write_time == *failed_mtime_) {
            return ShaderReloadStatus::Unchanged;
        }

        if (!compiler_->compile(sources_.vertex, out_vertex, error_log)) {
            failed_mtime_ = write_time;
            last_mtime_.reset();
            engine::log(LogLevel::Error, LogChannel::Render, "Shader hot-reload failed (vertex)");
            return ShaderReloadStatus::Failed;
        }
        if (!compiler_->compile(sources_.pixel, out_pixel, error_log)) {
            failed_mtime_ = write_time;
            last_mtime_.reset();
            engine::log(LogLevel::Error, LogChannel::Render, "Shader hot-reload failed (pixel)");
            return ShaderReloadStatus::Failed;
        }

        last_mtime_  = write_time;
        failed_mtime_.reset();
        engine::log(LogLevel::Info, LogChannel::Render, "Shader hot-reload compiled");
        return ShaderReloadStatus::Reloaded;
    }

private:
    std::unique_ptr<IShaderCompiler> compiler_;
    WatchedShaderPair sources_{};
    std::optional<std::filesystem::file_time_type> last_mtime_;
    std::optional<std::filesystem::file_time_type> failed_mtime_;
};

} // namespace

std::unique_ptr<IShaderHotReloader> create_hot_reloader() {
    return std::make_unique<DxcShaderHotReloader>();
}

} // namespace engine::shaders::dxc
