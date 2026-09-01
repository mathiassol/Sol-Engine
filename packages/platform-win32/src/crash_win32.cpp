#include <engine/platform/win32/crash_win32.hpp>

#include <engine/core/crash.hpp>
#include <engine/core/log.hpp>

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <system_error>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <dbghelp.h>

namespace engine::platform::win32 {
namespace {

// Set once by install_crash_dumps. A plain buffer rather than std::string: this
// is read from a crash path, where allocating is the last thing to attempt.
char g_dump_directory[512] = {};

bool write_dump_to(const char* path, EXCEPTION_POINTERS* exception) {
    const HANDLE file = ::CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    MINIDUMP_EXCEPTION_INFORMATION info{};
    info.ThreadId = ::GetCurrentThreadId();
    info.ExceptionPointers = exception;
    info.ClientPointers = FALSE;

    // WithIndirectlyReferencedMemory plus thread info: small enough to attach to
    // a bug report, and enough to walk the stack and read the locals that
    // pointed at the problem. A full memory dump of this process would be
    // hundreds of megabytes of texture.
    const MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(
        MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo);

    const BOOL ok = ::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), file,
        type, exception != nullptr ? &info : nullptr, nullptr, nullptr);
    ::CloseHandle(file);
    return ok == TRUE;
}

void handler_for_assert(const char* reason) {
    if (g_dump_directory[0] == '\0') {
        return;
    }
    char path[640];
    std::snprintf(path, sizeof(path), "%s/crash-%s.dmp", g_dump_directory,
        reason != nullptr ? reason : "unknown");
    // No logging here on success: the log line naming the dump is written by
    // the caller, and this runs while the process is going down.
    (void)write_dump_to(path, nullptr);
}

LONG WINAPI unhandled_filter(EXCEPTION_POINTERS* exception) {
    if (g_dump_directory[0] != '\0') {
        char path[640];
        std::snprintf(path, sizeof(path), "%s/crash-exception.dmp", g_dump_directory);
        write_dump_to(path, exception);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

bool write_minidump(std::string_view path) {
    const std::string target(path);
    std::error_code ec;
    const std::filesystem::path parent = std::filesystem::path(target).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
    }
    if (!write_dump_to(target.c_str(), nullptr)) {
        log(LogLevel::Error, LogChannel::General,
            std::string("Could not write a minidump to ") + target);
        return false;
    }
    return true;
}

std::string install_crash_dumps(std::string_view directory) {
    std::error_code ec;
    const std::filesystem::path dir(directory);
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        log(LogLevel::Warn, LogChannel::General,
            std::string("Could not create a crash-dump directory at ") + std::string(directory));
        return {};
    }
    const std::string generic = dir.generic_string();
    if (generic.size() + 1 >= sizeof(g_dump_directory)) {
        log(LogLevel::Warn, LogChannel::General, "Crash-dump directory path is too long");
        return {};
    }
    std::snprintf(g_dump_directory, sizeof(g_dump_directory), "%s", generic.c_str());

    ::SetUnhandledExceptionFilter(unhandled_filter);
    set_crash_dump_handler(&handler_for_assert);
    return generic;
}

} // namespace engine::platform::win32
