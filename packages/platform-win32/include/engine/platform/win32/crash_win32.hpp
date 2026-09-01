#pragma once

#include <string>
#include <string_view>

namespace engine::platform::win32 {

// Writes a minidump of the *current* process to `path`. MiniDumpWriteDump can
// capture a running process, which is what makes this gateable without staging
// a real crash. Returns false and logs if the write fails.
bool write_minidump(std::string_view path);

// Installs an unhandled-exception filter and the core crash-dump hook, so both
// an access violation and a failed ENGINE_ASSERT leave a .dmp beside the log.
//
// Call once at startup, next to install_file_logger and for the same reason:
// it is the earliest point the executable directory is known. Not under
// --gates - two gate runs would push a real crash dump out of the rotation,
// the same decision Foundation #6 made for log.txt.
//
// Returns the directory it will write into, or an empty string if it could not
// be created.
std::string install_crash_dumps(std::string_view directory);

} // namespace engine::platform::win32
