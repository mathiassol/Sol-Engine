#include <engine/shaders/dxc/shader_hot_reload_dxc.hpp>

#include <engine/core/log.hpp>
#include <engine/shaders/dxc/shader_compiler_dxc.hpp>

#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

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

struct CompileJob {
    WatchedShaderPair sources{};
    std::filesystem::file_time_type mtime{};
};

struct CompileResult {
    bool ok = false;
    ShaderBytecode vertex;
    ShaderBytecode pixel;
    std::string error;
    std::filesystem::file_time_type mtime{};
};

class DxcShaderHotReloader final : public IShaderHotReloader {
public:
    DxcShaderHotReloader() {
        worker_ = std::thread([this] { worker_loop(); });
    }

    ~DxcShaderHotReloader() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_one();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    void begin_watch(const WatchedShaderPair& sources) override {
        std::lock_guard<std::mutex> lock(mutex_);
        sources_ = sources;
        last_mtime_.reset();
        failed_mtime_.reset();
        dirty_ = false;
        result_.reset();
        last_mtime_ = newest_mtime(sources_.vertex.file_path, sources_.pixel.file_path);
    }

    void request_compile() override {
        queue_compile(true);
    }

    ShaderReloadStatus poll(
        ShaderBytecode& out_vertex,
        ShaderBytecode& out_pixel,
        std::string& error_log) override {
        CompileResult finished{};
        bool has_result = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (result_) {
                finished = std::move(*result_);
                result_.reset();
                compiling_ = false;
                has_result = true;
            }
        }

        if (has_result) {
            if (finished.ok) {
                last_mtime_ = finished.mtime;
                failed_mtime_.reset();
                out_vertex = std::move(finished.vertex);
                out_pixel = std::move(finished.pixel);
                engine::log(LogLevel::Info, LogChannel::Render, "Shader hot-reload compiled");
                if (dirty_) {
                    dirty_ = false;
                    queue_compile(true);
                }
                return ShaderReloadStatus::Reloaded;
            }
            failed_mtime_ = finished.mtime;
            last_mtime_.reset();
            error_log = std::move(finished.error);
            engine::log(LogLevel::Error, LogChannel::Render, "Shader hot-reload failed");
            return ShaderReloadStatus::Failed;
        }

        const auto write_time = newest_mtime(sources_.vertex.file_path, sources_.pixel.file_path);
        bool compiling = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            compiling = compiling_;
        }
        if (compiling) {
            if (write_time && last_mtime_ && *write_time != *last_mtime_) {
                dirty_ = true;
            }
            return ShaderReloadStatus::Busy;
        }

        if (!write_time) {
            return ShaderReloadStatus::Unchanged;
        }
        if (last_mtime_ && *write_time == *last_mtime_) {
            return ShaderReloadStatus::Unchanged;
        }
        if (failed_mtime_ && *write_time == *failed_mtime_) {
            return ShaderReloadStatus::Unchanged;
        }

        queue_compile(false);
        return ShaderReloadStatus::Busy;
    }

private:
    void queue_compile(bool force) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (sources_.vertex.file_path.empty()) {
            return;
        }
        if (compiling_) {
            dirty_ = true;
            return;
        }
        const auto write_time = newest_mtime(sources_.vertex.file_path, sources_.pixel.file_path);
        if (!write_time && !force) {
            return;
        }
        job_ = CompileJob{};
        job_->sources = sources_;
        job_->mtime = write_time ? *write_time : std::filesystem::file_time_type{};
        compiling_ = true;
        dirty_ = false;
        cv_.notify_one();
    }

    void worker_loop() {
        auto compiler = create_compiler();
        for (;;) {
            CompileJob job{};
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stop_ || job_.has_value(); });
                if (stop_) {
                    return;
                }
                job = std::move(*job_);
                job_.reset();
            }

            CompileResult result{};
            result.mtime = job.mtime;
            if (!compiler) {
                result.error = "DXC compiler is not available";
            } else if (!compiler->compile(job.sources.vertex, result.vertex, result.error)) {
                result.ok = false;
            } else if (!compiler->compile(job.sources.pixel, result.pixel, result.error)) {
                result.ok = false;
            } else {
                result.ok = true;
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                result_ = std::move(result);
            }
        }
    }

    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread worker_;
    bool stop_ = false;
    bool compiling_ = false;
    bool dirty_ = false;
    std::optional<CompileJob> job_;
    std::optional<CompileResult> result_;
    WatchedShaderPair sources_{};
    std::optional<std::filesystem::file_time_type> last_mtime_;
    std::optional<std::filesystem::file_time_type> failed_mtime_;
};

} // namespace

std::unique_ptr<IShaderHotReloader> create_hot_reloader() {
    return std::make_unique<DxcShaderHotReloader>();
}

} // namespace engine::shaders::dxc
