#pragma once

#include <engine/core/types.hpp>

#include <string_view>

namespace engine {

enum class LogLevel : u8 {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
};

enum class LogChannel : u8 {
    General,
    Platform,
    Render,
    Assets,
};

class ILogger {
public:
    virtual ~ILogger() = default;

    virtual void log(LogLevel level, LogChannel channel, std::string_view message) = 0;
};

void set_logger(ILogger* logger);
ILogger* logger();

void log(LogLevel level, std::string_view message);
void log(LogLevel level, LogChannel channel, std::string_view message);

} // namespace engine
