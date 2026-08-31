# File Logger Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the engine a durable on-disk copy of its session log, so a crash in a shipped `game.exe` leaves evidence behind instead of nothing.

**Architecture:** A `FileLogger` implementing the existing `ILogger` interface, living in `core` (portable C++ — no platform backend, no new package edge). It tees to `stderr` so console behaviour is unchanged, flushes every line so `abort()` cannot swallow the tail, and rotates the previous run's file. The app installs it right after `create_platform()` — the earliest point the executable directory is known — and skips it in `--gates` mode.

**Tech Stack:** C++20, `<fstream>`, `<filesystem>`, `<format>` + `<chrono>` (verified to compile clean at `/W4` on MSVC 14.51 — chosen over `std::localtime`, which triggers C4996, and over `#ifdef _WIN32`, which would put platform code in a package that has none).

**Spec:** [2026-08-31-file-logger-design.md](../specs/2026-08-31-file-logger-design.md)

---

## Testing model — read this first

**This project has no test framework.** Per `CLAUDE.md`, a test is a *gate*: a
plain `bool run_<name>_gate()` in `packages/sandbox/src/main.cpp` that asserts
on real values and logs its measurements. TDD maps onto this as: **write the
gate first, watch it fail, then implement.**

Task 2 therefore creates the header with a **stub that always returns
`nullptr`**, so Task 3's gate compiles, runs, and fails with real output rather
than failing to link. Task 4 makes it pass.

## File Structure

| File | Responsibility |
|------|----------------|
| `packages/core/include/engine/core/log.hpp` *(modify)* | Export `level_name` / `channel_name` so the file sink formats tags identically to the stderr sink instead of duplicating the tables |
| `packages/core/include/engine/core/log_file.hpp` *(create)* | Public surface: `create_file_logger`, `install_file_logger`, `default_log_directory` |
| `packages/core/src/log_file.cpp` *(create)* | `FileLogger` — open, rotate, header, format, flush, tee |
| `packages/core/src/log.cpp` *(modify)* | Move the two name helpers out of the anonymous namespace |
| `packages/core/src/assert.cpp` *(modify)* | Log `Fatal` unconditionally, not only when given a message |
| `packages/core/CMakeLists.txt` *(modify)* | Add `src/log_file.cpp` |
| `packages/sandbox/src/main.cpp` *(modify)* | `run_file_log_gate()`, its call site, and the install point in `run_app` |
| `docs/ROADMAP.md`, `docs/ENGINE_MAP.md`, the spec *(modify)* | Decision-log entry, LOC audit recount, Foundation #6 → Done, `Status: implemented` |

---

## Task 1: Export the log tag helpers

`log.cpp` has `level_name` and `channel_name` in an anonymous namespace. The
file sink needs the same tags. Exporting beats duplicating: two copies would
drift the first time a channel is added.

**Files:**
- Modify: `packages/core/include/engine/core/log.hpp`
- Modify: `packages/core/src/log.cpp`

- [ ] **Step 1: Declare the helpers in the header**

In `packages/core/include/engine/core/log.hpp`, add these two lines immediately
after the `class ILogger { ... };` block and before `void set_logger(...)`:

```cpp
// Stable text for a level/channel. Shared so every sink tags identically.
const char* level_name(LogLevel level);
const char* channel_name(LogChannel channel);
```

- [ ] **Step 2: Move the definitions out of the anonymous namespace**

In `packages/core/src/log.cpp`, the file currently reads:

```cpp
namespace {

ILogger* g_logger = nullptr;
std::mutex g_log_mutex;

const char* level_name(LogLevel level) {
```

Cut both functions (`level_name` and `channel_name`, through their closing
braces) out of the `namespace { ... }` block and paste them *after* the closing
`} // namespace`, so they become `engine::level_name` / `engine::channel_name`.
`g_logger`, `g_log_mutex`, `StdoutLogger` and `g_default_logger` stay inside the
anonymous namespace.

- [ ] **Step 3: Build and confirm nothing moved**

```powershell
cmake --build build --config Debug 2>&1 | Select-String -Pattern 'error|warning'
```

Expected: no output (0 errors, 0 warnings).

```powershell
.\build\bin\Debug\sandbox.exe --gates
```

Expected: exit 0, 70 `(pass)` lines — this task changes no behaviour.

- [ ] **Step 4: Commit**

```bash
git add packages/core/include/engine/core/log.hpp packages/core/src/log.cpp
git commit -m "refactor(core): export level_name/channel_name for reuse by a second sink"
```

---

## Task 2: Header and stub, so the gate can fail honestly

**Files:**
- Create: `packages/core/include/engine/core/log_file.hpp`
- Create: `packages/core/src/log_file.cpp`
- Modify: `packages/core/CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `packages/core/include/engine/core/log_file.hpp`:

```cpp
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
```

- [ ] **Step 2: Write the stub implementation**

Create `packages/core/src/log_file.cpp`:

```cpp
#include <engine/core/log_file.hpp>

#include <filesystem>

namespace engine {

std::string default_log_directory(std::string_view executable_directory) {
    return (std::filesystem::path(executable_directory) / "logs").string();
}

std::unique_ptr<ILogger> create_file_logger(std::string_view) {
    return nullptr;
}

bool install_file_logger(std::string_view) {
    return false;
}

} // namespace engine
```

- [ ] **Step 3: Add the source to the package**

In `packages/core/CMakeLists.txt`, add `src/log_file.cpp` to the `SOURCES`
list, after `src/log.cpp`:

```cmake
engine_add_package(core
    SOURCES
        src/log.cpp
        src/log_file.cpp
        src/assert.cpp
        src/clock.cpp
        src/frame.cpp
        src/arena.cpp
        src/profile.cpp
        src/cvar.cpp
)
```

- [ ] **Step 4: Configure and build**

```powershell
cmake -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Debug 2>&1 | Select-String -Pattern 'error|warning'
```

Expected: no output. A new source file needs the reconfigure to enter the
project.

- [ ] **Step 5: Commit**

```bash
git add packages/core/include/engine/core/log_file.hpp packages/core/src/log_file.cpp packages/core/CMakeLists.txt
git commit -m "feat(core): file logger surface with a stub, so its gate can fail first"
```

---

## Task 3: Write the gate and watch it fail

**Files:**
- Modify: `packages/sandbox/src/main.cpp`

- [ ] **Step 1: Add the includes**

In `packages/sandbox/src/main.cpp`, add to the `<engine/core/...>` include block
near the top (it is sorted, so put it after `#include <engine/core/log.hpp>`):

```cpp
#include <engine/core/log_file.hpp>
```

Then confirm `<filesystem>`, `<fstream>`, `<memory>` and `<string>` are present
in the standard-library include block lower down; add any that are missing.

- [ ] **Step 2: Write the gate**

Insert this immediately before `bool run_arena_gate() {` (currently around line
633, under the comment about capacity limits that used to abort):

```cpp
// Reads a whole file. The gate asserts on what landed on disk, not on what it
// asked for, so it has to read it back.
std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
}

bool run_file_log_gate() {
    std::error_code ec;
    const std::filesystem::path root =
        std::filesystem::temp_directory_path(ec) / "sol_file_log_gate";
    std::filesystem::remove_all(root, ec);

    const std::filesystem::path log_dir = root / "logs";
    const std::filesystem::path current = log_dir / "log.txt";
    const std::filesystem::path previous = log_dir / "log.prev.txt";

    // Run one: three records, then close by destroying the sink.
    bool created = false;
    {
        auto sink = engine::create_file_logger(log_dir.string());
        created = sink != nullptr;
        if (sink) {
            sink->log(engine::LogLevel::Info, engine::LogChannel::General, "first line");
            sink->log(engine::LogLevel::Warn, engine::LogChannel::Assets, "second line");
            sink->log(engine::LogLevel::Error, engine::LogChannel::Render, "third line");
        }
    }

    const std::string run_one = read_text_file(current);
    const bool header_ok = run_one.find("Sol Engine session log") != std::string::npos
        && run_one.find("started ") != std::string::npos;
    const bool lines_ok = run_one.find("[INFO][general] first line") != std::string::npos
        && run_one.find("[WARN][assets] second line") != std::string::npos
        && run_one.find("[ERROR][render] third line") != std::string::npos;

    // Run two: rotates run one to log.prev.txt and starts fresh.
    {
        auto sink = engine::create_file_logger(log_dir.string());
        if (sink) {
            sink->log(engine::LogLevel::Info, engine::LogChannel::General, "fourth line");
        }
    }

    const std::string prev = read_text_file(previous);
    const std::string run_two = read_text_file(current);
    const bool rotated = !prev.empty() && !run_two.empty();
    const bool prev_intact = prev.find("[INFO][general] first line") != std::string::npos
        && prev.find("[ERROR][render] third line") != std::string::npos;
    const bool fresh = run_two.find("[INFO][general] fourth line") != std::string::npos
        && run_two.find("first line") == std::string::npos;

    // An undirectory: create_directories cannot make a directory under a file,
    // so this is a deterministic unwritable path on any platform.
    const std::filesystem::path blocker = root / "not_a_directory";
    {
        std::ofstream make_file(blocker);
        make_file << "x";
    }
    auto rejected_sink = engine::create_file_logger((blocker / "logs").string());
    const bool unwritable_rejected = rejected_sink == nullptr;

    std::filesystem::remove_all(root, ec);

    const bool passed = created && header_ok && lines_ok && rotated && prev_intact
        && fresh && unwritable_rejected;
    char message[224];
    std::snprintf(message, sizeof(message),
        "File log gate: created=%s header=%s lines=%s rotated=%s prev_intact=%s "
        "fresh=%s unwritable_rejected=%s (%s)",
        created ? "yes" : "no", header_ok ? "yes" : "no", lines_ok ? "yes" : "no",
        rotated ? "yes" : "no", prev_intact ? "yes" : "no", fresh ? "yes" : "no",
        unwritable_rejected ? "yes" : "no", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::General, message);
    return passed;
}
```

- [ ] **Step 3: Call it from the gate sequence**

In `run_app`, find:

```cpp
    if (!run_arena_gate()) {
        gates_ok = false;
    }
```

Insert immediately **before** it:

```cpp
    if (!run_file_log_gate()) {
        gates_ok = false;
    }
```

- [ ] **Step 4: Build and watch the gate fail**

```powershell
cmake --build build --config Debug 2>&1 | Select-String -Pattern 'error|warning'
.\build\bin\Debug\sandbox.exe --gates
```

Expected: build clean; the run reports

```
[ERROR][general] File log gate: created=no header=no lines=no rotated=no prev_intact=no fresh=no unwritable_rejected=yes (FAIL)
```

and `sandbox.exe --gates` exits **1**. `unwritable_rejected=yes` is expected
even now — the stub returns `nullptr` for everything, which happens to be right
for that one case. Everything else must be `no`. **Do not proceed until you
have seen this line.**

- [ ] **Step 5: Commit the failing gate**

```bash
git add packages/sandbox/src/main.cpp
git commit -m "test(core): file log gate, failing against the stub"
```

---

## Task 4: Implement the sink

**Files:**
- Modify: `packages/core/src/log_file.cpp`

- [ ] **Step 1: Replace the stub with the implementation**

Replace the whole contents of `packages/core/src/log_file.cpp` with:

```cpp
#include <engine/core/log_file.hpp>

#include <engine/core/clock.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <utility>

namespace engine {

namespace {

constexpr const char* kCurrentName = "log.txt";
constexpr const char* kPreviousName = "log.prev.txt";

// UTC, and labelled as such. Local time would need either std::localtime
// (C4996 under /W4) or a platform call, and `core` has no platform code.
// Comparing two machines' logs is easier in UTC anyway.
std::string utc_now_text() {
    const auto now = std::chrono::floor<std::chrono::seconds>(
        std::chrono::system_clock::now());
    return std::format("{:%Y-%m-%d %H:%M:%S}", now);
}

class FileLogger final : public ILogger {
public:
    static std::unique_ptr<FileLogger> open(const std::filesystem::path& directory) {
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        // create_directories reports false with no error when the directory
        // already existed, so ask the filesystem rather than trust the return.
        if (!std::filesystem::is_directory(directory, ec)) {
            return nullptr;
        }

        const std::filesystem::path current = directory / kCurrentName;
        if (std::filesystem::exists(current, ec)) {
            const std::filesystem::path previous = directory / kPreviousName;
            std::filesystem::remove(previous, ec);
            std::filesystem::rename(current, previous, ec);
            // A failed rotation is deliberately not fatal: overwriting
            // log.txt still leaves a log, which is the whole point.
        }

        std::ofstream file(current, std::ios::out | std::ios::trunc);
        if (!file) {
            return nullptr;
        }
        return std::unique_ptr<FileLogger>(new FileLogger(std::move(file)));
    }

    void log(LogLevel level, LogChannel channel, std::string_view message) override {
        const char* level_text = level_name(level);
        const char* channel_text = channel_name(channel);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            char stamp[16];
            std::snprintf(stamp, sizeof(stamp), "[%8.3f]", clock_.now());
            file_ << stamp << '[' << level_text << "][" << channel_text << "] ";
            file_.write(message.data(), static_cast<std::streamsize>(message.size()));
            file_ << '\n';
            // Every line, not on a timer: the record this exists to keep is the
            // one written immediately before std::abort().
            file_.flush();
        }
        // Tee. Console behaviour is unchanged; the file is an added copy.
        std::fprintf(stderr, "[%s][%s] %.*s\n", level_text, channel_text,
            static_cast<int>(message.size()), message.data());
    }

private:
    explicit FileLogger(std::ofstream file) : file_(std::move(file)) {
        file_ << "=== Sol Engine session log ===\n"
              << "started " << utc_now_text() << " UTC\n"
              << "times below are seconds since this file was opened\n\n";
        file_.flush();
    }

    Clock clock_;
    std::ofstream file_;
    std::mutex mutex_;
};

} // namespace

std::string default_log_directory(std::string_view executable_directory) {
    return (std::filesystem::path(executable_directory) / "logs").string();
}

std::unique_ptr<ILogger> create_file_logger(std::string_view directory) {
    return FileLogger::open(std::filesystem::path(directory));
}

bool install_file_logger(std::string_view directory) {
    // Function-local static: outlives run_app(), so main()'s catch handlers can
    // still log through it. Mirrors g_frame_profiler in profile.cpp.
    static std::unique_ptr<ILogger> installed;
    static bool attempted = false;
    if (attempted) {
        return installed != nullptr;
    }
    attempted = true;
    installed = create_file_logger(directory);
    if (!installed) {
        return false;
    }
    set_logger(installed.get());
    return true;
}

} // namespace engine
```

- [ ] **Step 2: Build and watch the gate pass**

```powershell
cmake --build build --config Debug 2>&1 | Select-String -Pattern 'error|warning'
.\build\bin\Debug\sandbox.exe --gates
```

Expected: build clean; the run reports

```
[INFO][general] File log gate: created=yes header=yes lines=yes rotated=yes prev_intact=yes fresh=yes unwritable_rejected=yes (pass)
```

and `sandbox.exe --gates` exits **0** with **71** `(pass)` lines.

- [ ] **Step 3: Commit**

```bash
git add packages/core/src/log_file.cpp
git commit -m "feat(core): file logger — rotate, header, per-line flush, stderr tee (Foundation #6)"
```

---

## Task 5: Make the abort path reach the log

`assert_fail` logs only when given a message, so the ~40 bare `ENGINE_ASSERT`
sites write nothing anywhere. Per-line flush means no explicit flush is needed
before `abort()`.

**Files:**
- Modify: `packages/core/src/assert.cpp`

- [ ] **Step 1: Log unconditionally**

Replace the body of `assert_fail` in `packages/core/src/assert.cpp` with:

```cpp
[[noreturn]] void assert_fail(const char* expr, const char* file, int line, const char* msg) {
    // Direct write first, and kept: if the installed logger is the thing that
    // is broken, this is the only output that survives.
    if (msg) {
        std::fprintf(stderr, "ASSERT FAILED: %s\n  at %s:%d\n  %s\n", expr, file, line, msg);
    } else {
        std::fprintf(stderr, "ASSERT FAILED: %s\n  at %s:%d\n", expr, file, line);
    }

    // Always, not only when a message was given. The bare form is the majority
    // of assert sites, and it used to abort leaving nothing on disk.
    char message[512];
    if (msg) {
        std::snprintf(message, sizeof(message), "ASSERT FAILED: %s at %s:%d — %s",
            expr, file, line, msg);
    } else {
        std::snprintf(message, sizeof(message), "ASSERT FAILED: %s at %s:%d",
            expr, file, line);
    }
    log(LogLevel::Fatal, LogChannel::General, message);

    std::abort();
}
```

- [ ] **Step 2: Build and confirm no behaviour moved**

```powershell
cmake --build build --config Debug 2>&1 | Select-String -Pattern 'error|warning'
.\build\bin\Debug\sandbox.exe --gates
```

Expected: build clean, 71 `(pass)`, exit 0. No gate reaches an assert — this
change is only visible when one fires.

- [ ] **Step 3: Commit**

```bash
git add packages/core/src/assert.cpp
git commit -m "fix(core): log every assert failure, not only the ones with a message (analizeMax S5)"
```

---

## Task 6: Install it in the app

**Files:**
- Modify: `packages/sandbox/src/main.cpp`

- [ ] **Step 1: Install after the platform, before the banner**

In `run_app`, the code currently reads:

```cpp
    char start_message[64];
    std::snprintf(start_message, sizeof(start_message),
        gates_mode ? "%s starting (--gates)" : "%s starting", kAppName);
    engine::log(engine::LogLevel::Info, engine::LogChannel::General, start_message);

    engine::EngineModules modules{};

#ifdef ENGINE_HAS_WIN32_PLATFORM
    modules.platform = engine::platform::win32::create_platform();
#else
    engine::log(engine::LogLevel::Fatal, engine::LogChannel::Platform, "No platform backend");
    return 1;
#endif
```

Replace that whole span with:

```cpp
    engine::EngineModules modules{};

#ifdef ENGINE_HAS_WIN32_PLATFORM
    modules.platform = engine::platform::win32::create_platform();
#else
    engine::log(engine::LogLevel::Fatal, engine::LogChannel::Platform, "No platform backend");
    return 1;
#endif

    // Earliest point the executable directory is known, and before the banner
    // below so the banner is the log's first line.
    //
    // Not in gates mode: --gates is a test harness, not a session, and two gate
    // runs would push a real crash log out of both log.txt and log.prev.txt.
    if (!gates_mode && modules.platform) {
        const std::string log_dir =
            engine::default_log_directory(modules.platform->executable_directory());
        if (!engine::install_file_logger(log_dir)) {
            char warning[320];
            std::snprintf(warning, sizeof(warning),
                "Could not open a log file in %s — continuing on stderr only",
                log_dir.c_str());
            engine::log(engine::LogLevel::Warn, engine::LogChannel::General, warning);
        }
    }

    char start_message[64];
    std::snprintf(start_message, sizeof(start_message),
        gates_mode ? "%s starting (--gates)" : "%s starting", kAppName);
    engine::log(engine::LogLevel::Info, engine::LogChannel::General, start_message);
```

- [ ] **Step 2: Build**

```powershell
cmake --build build --config Debug 2>&1 | Select-String -Pattern 'error|warning'
```

Expected: no output.

- [ ] **Step 3: Confirm gates still write no file**

```powershell
Remove-Item -Recurse -Force build/bin/Debug/logs -ErrorAction SilentlyContinue
.\build\bin\Debug\sandbox.exe --gates
Test-Path build/bin/Debug/logs
```

Expected: 71 `(pass)`, exit 0, and `Test-Path` prints **False** — gates mode
installs no sink.

- [ ] **Step 4: Commit**

```bash
git add packages/sandbox/src/main.cpp
git commit -m "feat(sandbox): install the file logger for real sessions (Foundation #6)"
```

---

## Task 7: Verify a real session end to end

No code changes. This is the check the gate cannot make, because gates mode
deliberately installs no sink.

- [ ] **Step 1: Run the sandbox normally and quit**

```powershell
.\build\bin\Debug\sandbox.exe
```

Let it open, then press `Esc`.

- [ ] **Step 2: Read the log back**

```powershell
Get-Content build/bin/Debug/logs/log.txt -TotalCount 12
```

Expected: the header (`=== Sol Engine session log ===`, `started <date> UTC`),
then `[   0.0xx][INFO][general] Sandbox starting` as the first record, followed
by the backend and device lines. Confirm elapsed times increase down the file
and the last line is the shutdown record.

- [ ] **Step 3: Run again and confirm rotation**

```powershell
.\build\bin\Debug\sandbox.exe
```

Press `Esc`, then:

```powershell
Get-ChildItem build/bin/Debug/logs
(Get-Content build/bin/Debug/logs/log.prev.txt -TotalCount 5) -join ' | '
```

Expected: both `log.txt` and `log.prev.txt` present; `log.prev.txt` holds the
*first* run's header and lines.

- [ ] **Step 4: Confirm the Release player writes one too**

```powershell
cmake --build build --config Release --target game
.\build\bin\Release\game.exe --gates
Test-Path build/bin/Release/logs
```

Expected: 71 `(pass)`, exit 0, `Test-Path` **False** (gates mode). Then run
`.\build\bin\Release\game.exe`, press `Esc`, and confirm
`build/bin/Release/logs/log.txt` exists with `Game starting` as its first
record.

---

## Task 8: Close the row out

**Files:**
- Modify: `docs/ENGINE_MAP.md`
- Modify: `docs/ROADMAP.md`
- Modify: `docs/superpowers/specs/2026-08-31-file-logger-design.md`

- [ ] **Step 1: Mark the map row Done**

In `docs/ENGINE_MAP.md`, change the Foundation #6 row from:

```
| 6 | File logger (not only stderr) | **Ready** | |
```

to:

```
| 6 | File logger (not only stderr) | **Done** | |
```

- [ ] **Step 2: Add the ROADMAP decision entry**

Add a new section to `docs/ROADMAP.md` following the existing
Why / Gate (met) / Do not shape used by the other entries. Content: why
(stderr was the only sink, `game.exe` is a console app whose window dies with
the process, and ~40 bare asserts logged nothing); the choice
(`<exe_dir>/logs/`, two-file rotation, per-line flush, stderr tee, no file in
gates mode); the gate (`File log gate: … (pass)`); and **Do not (still)**:
`%LOCALAPPDATA%` (Build #7), minidumps (Foundation #7), level filtering.

- [ ] **Step 3: Recount the LOC audit line**

The `roadmap-audit` invariant recounts LOC on every run, so this line goes
stale on any source change. Run the checker to get the true number:

```powershell
pwsh -NoProfile -File tools/check-invariants.ps1
```

It will report `docs/ROADMAP.md: audit claims 23437 lines, recount says <N>`.
Update the `Measured 31 Aug 2026: **N lines** of C++/HLSL in **M files**`
line in `docs/ROADMAP.md` to the reported values — note **files** goes from 140
to **142**, since this task adds two.

- [ ] **Step 4: Mark the spec implemented**

In `docs/superpowers/specs/2026-08-31-file-logger-design.md`, change
`Status: approved` to `Status: implemented`.

- [ ] **Step 5: Full verification**

```powershell
pwsh -NoProfile -File tools/check-invariants.ps1
cmake --build build --config Debug
.\build\bin\Debug\sandbox.exe --gates
$env:ENGINE_GPU_DEBUG=1; .\build\bin\Debug\sandbox.exe --gates; $env:ENGINE_GPU_DEBUG=$null
cmake --build build --config Release --target game
.\build\bin\Release\game.exe --gates
```

Expected: 12/12 invariants; both builds clean; 71 `(pass)` and exit 0 in both
configurations; D3D12 debug layer `0 message(s), 0 error(s), 0 warning(s)` (no
GPU code changed, so this must not move).

- [ ] **Step 6: Commit**

```bash
git add docs/ENGINE_MAP.md docs/ROADMAP.md docs/superpowers/specs/2026-08-31-file-logger-design.md
git commit -m "docs: close Foundation #6 — file logger (Category #1)"
```

---

## Self-review notes

**Spec coverage.** Location → Task 2 (`default_log_directory`) + Task 6.
Rotation, tee, per-line flush, header format, failure-returns-nullptr → Task 4.
Process-lifetime owner → Task 4 (`install_file_logger`). Install point and
gates-mode exclusion → Task 6. `assert_fail` → Task 5. Gate → Tasks 3 and 4.
Out-of-scope items → recorded in Task 8's "Do not (still)". Every spec section
maps to a task.

**Naming consistency.** `create_file_logger`, `install_file_logger`,
`default_log_directory`, `run_file_log_gate`, `read_text_file`, `log.txt`,
`log.prev.txt` — each spelled identically in every task that mentions it.

**One deviation from the skill's template**, deliberate: there is no test
framework here, so "write the failing test" is "write the failing gate" (Task 3)
against a stub (Task 2). `CLAUDE.md` requires the gate before the
implementation, and user instructions outrank the skill's default.
