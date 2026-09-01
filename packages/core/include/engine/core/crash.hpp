#pragma once

#include <engine/core/types.hpp>

namespace engine {

// A hook the platform backend fills in, so `core` can trigger a crash dump
// without depending on one. Same shape as set_logger, and for the same reason:
// dependencies only point downward, and MiniDumpWriteDump is a Windows API that
// belongs in platform-win32.
//
// Called from assert_fail before std::abort(), and from the unhandled-exception
// filter. `reason` is short and goes in the dump's file name, so a directory of
// dumps says what each one was without opening it.
using CrashDumpHandler = void (*)(const char* reason);

// Install once at startup. Passing nullptr disables dumps again, which is what
// --gates does: two gate runs must not push a real crash out of the rotation.
void set_crash_dump_handler(CrashDumpHandler handler);

// No-op when nothing is installed. Never throws and never allocates - it runs
// on a path where the process is already going down.
void write_crash_dump(const char* reason);

} // namespace engine
