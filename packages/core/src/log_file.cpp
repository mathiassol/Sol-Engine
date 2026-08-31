#include <engine/core/log_file.hpp>

#include <engine/core/clock.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <utility>

namespace engine {

namespace {

constexpr const char* kCurrentName = "log.txt";
constexpr const char* kPreviousName = "log.prev.txt";

// UTC, and labelled as such. Local time would need either std::localtime
// (C4996 under /W4) or a platform call, and `core` has no platform code.
// Comparing two machines' logs is easier in UTC anyway.
std::string utc_now_text() {
    const auto now = std::chrono::floor<std::chrono::seconds>(
        std::chrono::system_clock::now());
    return std::format("{:%Y-%m-%d %H:%M:%S}", now);
}

class FileLogger final : public ILogger {
public:
    static std::unique_ptr<FileLogger> open(const std::filesystem::path& directory) {
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        // create_directories reports false with no error when the directory
        // already existed, so ask the filesystem rather than trust the return.
        if (!std::filesystem::is_directory(directory, ec)) {
            return nullptr;
        }

        const std::filesystem::path current = directory / kCurrentName;
        if (std::filesystem::exists(current, ec)) {
            const std::filesystem::path previous = directory / kPreviousName;
            std::filesystem::remove(previous, ec);
            std::filesystem::rename(current, previous, ec);
            // A failed rotation is deliberately not fatal: overwriting
            // log.txt still leaves a log, which is the whole point.
        }

        std::ofstream file(current, std::ios::out | std::ios::trunc);
        if (!file) {
            return nullptr;
        }
        return std::unique_ptr<FileLogger>(new FileLogger(std::move(file)));
    }

    void log(LogLevel level, LogChannel channel, std::string_view message) override {
        const char* level_text = level_name(level);
        const char* channel_text = channel_name(channel);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            char stamp[16];
            std::snprintf(stamp, sizeof(stamp), "[%8.3f]", clock_.now());
            file_ << stamp << '[' << level_text << "][" << channel_text << "] ";
            file_.write(message.data(), static_cast<std::streamsize>(message.size()));
            file_ << '\n';
            // Every line, not on a timer: the record this exists to keep is the
            // one written immediately before std::abort().
            file_.flush();
        }
        // Tee. Console behaviour is unchanged; the file is an added copy.
        std::fprintf(stderr, "[%s][%s] %.*s\n", level_text, channel_text,
            static_cast<int>(message.size()), message.data());
    }

private:
    explicit FileLogger(std::ofstream file) : file_(std::move(file)) {
        file_ << "=== Sol Engine session log ===\n"
              << "started " << utc_now_text() << " UTC\n"
              << "times below are seconds since this file was opened\n\n";
        file_.flush();
    }

    Clock clock_;
    std::ofstream file_;
    std::mutex mutex_;
};

} // namespace

std::string default_log_directory(std::string_view executable_directory) {
    return (std::filesystem::path(executable_directory) / "logs").string();
}

std::unique_ptr<ILogger> create_file_logger(std::string_view directory) {
    return FileLogger::open(std::filesystem::path(directory));
}

bool install_file_logger(std::string_view directory) {
    // Function-local static: outlives run_app(), so main()'s catch handlers can
    // still log through it. Mirrors g_frame_profiler in profile.cpp.
    static std::unique_ptr<ILogger> installed;
    static bool attempted = false;
    if (attempted) {
        return installed != nullptr;
    }
    attempted = true;
    installed = create_file_logger(directory);
    if (!installed) {
        return false;
    }
    set_logger(installed.get());
    return true;
}

} // namespace engine
