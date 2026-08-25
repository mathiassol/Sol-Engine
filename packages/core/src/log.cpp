#include <engine/core/log.hpp>

#include <cstdio>
#include <mutex>

namespace engine {

namespace {

ILogger* g_logger = nullptr;
std::mutex g_log_mutex;

const char* level_name(LogLevel level) {
    switch (level) {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info:  return "INFO";
    case LogLevel::Warn:  return "WARN";
    case LogLevel::Error: return "ERROR";
    case LogLevel::Fatal: return "FATAL";
    }
    return "???";
}

const char* channel_name(LogChannel channel) {
    switch (channel) {
    case LogChannel::General:  return "general";
    case LogChannel::Platform: return "platform";
    case LogChannel::Render:   return "render";
    case LogChannel::Assets:   return "assets";
    case LogChannel::Audio:    return "audio";
    case LogChannel::Physics:  return "physics";
    }
    return "???";
}

class StdoutLogger final : public ILogger {
public:
    void log(LogLevel level, LogChannel channel, std::string_view message) override {
        std::fprintf(stderr, "[%s][%s] %.*s\n",
            level_name(level),
            channel_name(channel),
            static_cast<int>(message.size()),
            message.data());
    }
};

StdoutLogger g_default_logger;

} // namespace

void set_logger(ILogger* logger) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_logger = logger;
}

ILogger* logger() {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    return g_logger ? g_logger : &g_default_logger;
}

void log(LogLevel level, std::string_view message) {
    log(level, LogChannel::General, message);
}

void log(LogLevel level, LogChannel channel, std::string_view message) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    ILogger* active = g_logger ? g_logger : &g_default_logger;
    active->log(level, channel, message);
}

} // namespace engine
