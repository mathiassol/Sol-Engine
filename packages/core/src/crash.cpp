#include <engine/core/crash.hpp>

namespace engine {
namespace {

// Set once at startup from the main thread, like g_logger. Not atomic for the
// same reason: the alternative is a fence on a path that runs during a crash.
CrashDumpHandler g_crash_handler = nullptr;

} // namespace

void set_crash_dump_handler(CrashDumpHandler handler) {
    g_crash_handler = handler;
}

void write_crash_dump(const char* reason) {
    if (g_crash_handler != nullptr) {
        g_crash_handler(reason != nullptr ? reason : "unknown");
    }
}

} // namespace engine
