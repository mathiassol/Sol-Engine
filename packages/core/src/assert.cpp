#include <engine/core/assert.hpp>
#include <engine/core/log.hpp>

#include <cstdio>
#include <cstdlib>

namespace engine {

[[noreturn]] void assert_fail(const char* expr, const char* file, int line, const char* msg) {
    if (msg) {
        std::fprintf(stderr, "ASSERT FAILED: %s\n  at %s:%d\n  %s\n", expr, file, line, msg);
        log(LogLevel::Fatal, msg);
    } else {
        std::fprintf(stderr, "ASSERT FAILED: %s\n  at %s:%d\n", expr, file, line);
    }
    std::abort();
}

} // namespace engine
