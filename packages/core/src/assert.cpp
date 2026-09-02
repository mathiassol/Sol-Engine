#include <engine/core/assert.hpp>
#include <engine/core/crash.hpp>
#include <engine/core/log.hpp>

#include <cstdio>
#include <cstdlib>

namespace engine {

[[noreturn]] void assert_fail(const char* expr, const char* file, int line, const char* msg) {
    // Direct write first, and kept: if the installed logger is the thing that
    // is broken, this is the only output that survives.
    if (msg) {
        std::fprintf(stderr, "ASSERT FAILED: %s\n  at %s:%d\n  %s\n", expr, file, line, msg);
    } else {
        std::fprintf(stderr, "ASSERT FAILED: %s\n  at %s:%d\n", expr, file, line);
    }

    // Always, not only when a message was given. The bare ENGINE_ASSERT form is
    // the majority of the 107 assert sites, and it used to abort leaving nothing
    // on disk and nothing in any log sink.
    char message[512];
    if (msg) {
        std::snprintf(message, sizeof(message), "ASSERT FAILED: %s at %s:%d - %s",
            expr, file, line, msg);
    } else {
        std::snprintf(message, sizeof(message), "ASSERT FAILED: %s at %s:%d",
            expr, file, line);
    }
    // The file sink flushes every line, so no explicit flush is needed here for
    // the record to survive the abort below.
    log(LogLevel::Fatal, LogChannel::General, message);

    // After the log line, so the log names the failure even if the dump write
    // is what goes wrong. 107 assert sites are live in Release - ENGINE_ASSERT
    // has no NDEBUG guard - which makes an assert the most likely way this
    // process dies, and the most valuable place to have a stack.
    write_crash_dump("assert");

    std::abort();
}

} // namespace engine
