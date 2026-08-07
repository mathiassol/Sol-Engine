#pragma once

namespace engine {

[[noreturn]] void assert_fail(const char* expr, const char* file, int line, const char* msg);

} // namespace engine

#define ENGINE_ASSERT(expr) \
    do { \
        if (!(expr)) { \
            ::engine::assert_fail(#expr, __FILE__, __LINE__, nullptr); \
        } \
    } while (0)

#define ENGINE_ASSERT_MSG(expr, msg) \
    do { \
        if (!(expr)) { \
            ::engine::assert_fail(#expr, __FILE__, __LINE__, (msg)); \
        } \
    } while (0)
