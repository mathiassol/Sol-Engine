#pragma once

#include <engine/core/log.hpp>

#include <memory>
#include <string>
#include <string_view>

namespace engine {

// Where session logs go, given the directory the executable lives in.
//
// One function on purpose: ENGINE_MAP Build #7 moves logs to %LOCALAPPDATA%,
// and this is the only place that decision lives.
std::string default_log_directory(std::string_view executable_directory);

// A session sink: writes `<directory>/log.txt`, renaming any existing one to
// `log.prev.txt` first, and tees every record to stderr so console output is
// unchanged. Flushes every line, because the failure this exists to record is
// std::abort(), which discards a buffered stream.
//
// Returns nullptr if `directory` cannot be created or the file cannot be
// opened — an unwritable install directory is a worse day, not a dead process.
//
// The caller owns the sink and must outlive every log call made through it.
// For the process logger use install_file_logger() instead.
std::unique_ptr<ILogger> create_file_logger(std::string_view directory);

// create_file_logger(), owned for the life of the process and installed via
// set_logger(). Returns false if the directory is not writable, leaving the
// stderr logger in place.
//
// Process lifetime is required, not convenient: main()'s catch handlers log
// after run_app() has returned, so a sink owned by a run_app local would be
// logged through after destruction.
//
// A second call is ignored and returns whether the first one succeeded.
bool install_file_logger(std::string_view directory);

} // namespace engine
