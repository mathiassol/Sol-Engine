# Config / cvars Implementation Plan (Foundation #8)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Named knobs readable from a config file and the command line, without a recompile.

**Architecture:** A self-registering `Cvar` object with a tagged union lives in `core` (layer 0) and parses config *text* only — it never opens a file. The `engine` package reads `config.cfg` through `platform::IFileSystem` and hands the text down, so `core` keeps zero OS dependencies. One precedence rule (`Default < File < CommandLine < Code`; a write is accepted only when its source is at least the current source) lets the engine load the file *after* the command line and still have `--set` win.

**Tech Stack:** C++20, MSVC, CMake. Spec: [2026-08-25-foundation-cvars-design.md](../specs/2026-08-25-foundation-cvars-design.md).

---

## Testing idiom — read this first

**This repo has no unit-test framework.** Every feature is proven by a *gate*:
a `run_*_gate()` function in `packages/sandbox/src/main.cpp` that returns
`bool` and logs one line ending in `(pass)` or `(FAIL)`. The gates run on every
sandbox launch; `sandbox.exe --gates` runs them and exits with the result.
"Write the failing test" in this plan means **write the gate assertions first**,
build, and watch them fail. For the first task the expected red state is a
compile error for a type that does not exist yet.

Build and run, from the repo root in PowerShell:

```powershell
cmake --build build --config Debug --target sandbox
```

```powershell
.\build\bin\Debug\sandbox.exe --gates
```

If `build/` does not exist yet:

```powershell
cmake -B build -G "Visual Studio 18 2026" -A x64
```

Commit directly to `main` and **push after every commit** — that is a standing
instruction in `CLAUDE.md`, not something to ask about.

---

## File structure

| File | Responsibility |
|------|----------------|
| Create `packages/core/include/engine/core/cvar.hpp` | `CvarType`, `CvarSource`, `CvarSetResult`, `Cvar`, `CvarApplyStats`, registry + apply free functions |
| Create `packages/core/src/cvar.cpp` | Registry storage, value parsing, text parser, argv parser |
| Modify `packages/core/CMakeLists.txt` | Add `src/cvar.cpp` |
| Create `packages/engine/include/engine/cvar_file.hpp` | `apply_cvar_file` — the one place a path becomes cvar text |
| Create `packages/engine/src/cvar_file.cpp` | Reads bytes via `IFileSystem`, delegates to `apply_cvar_text` |
| Modify `packages/engine/CMakeLists.txt` | Add `src/cvar_file.cpp` |
| Modify `packages/engine/src/engine.cpp` | Declare the four engine knobs; reorder `init` so filesystem/content-root precede window creation; load `config.cfg`; overlay `WindowDesc`; log one line |
| Modify `packages/platform/include/engine/platform/window.hpp` | Inline `parse_window_mode` next to `window_mode_name` |
| Modify `packages/renderer/include/engine/renderer/aa.hpp` | Inline `parse_mode` next to `mode_name` |
| Modify `packages/sandbox/src/main.cpp` | Gate knobs + `run_cvar_gate`; `apply_cvar_args` in `main`; `r.aa` at demo setup; fix the AA gate's live-state clause |
| Modify `.gitignore` | Ignore `config.cfg` |
| Modify `docs/ARCHITECTURE.md`, `docs/FOUNDATION.md` | Document the knob API |

---

## Task 1: The knob and the registry

**Files:**
- Create: `packages/core/include/engine/core/cvar.hpp`
- Create: `packages/core/src/cvar.cpp`
- Modify: `packages/core/CMakeLists.txt`
- Test (gate): `packages/sandbox/src/main.cpp`

- [ ] **Step 1: Write the failing gate**

In `packages/sandbox/src/main.cpp`, add the include alongside the other core
includes at the top of the file:

```cpp
#include <engine/core/cvar.hpp>
```

Inside the existing anonymous `namespace {` block near the top (where
`kTestFile` and the other file-scope constants live), declare the gate-only
knobs. These exist so the gate never disturbs a knob the running app reads:

```cpp
// Gate-only knobs. Nothing outside run_cvar_gate reads these, which is why the
// gate is free to drive them to any value and leave them there.
engine::Cvar cv_gate_bool{"gate.bool", false, "Cvar gate: bool knob"};
engine::Cvar cv_gate_int{"gate.int", 0, "Cvar gate: int knob"};
engine::Cvar cv_gate_float{"gate.float", 0.f, "Cvar gate: float knob"};
engine::Cvar cv_gate_string{"gate.string", "default", "Cvar gate: string knob"};
engine::Cvar cv_gate_prec{"gate.prec", 0, "Cvar gate: precedence knob"};
engine::Cvar cv_gate_args{"gate.args", 0, "Cvar gate: command-line knob"};
engine::Cvar cv_gate_file{"gate.file", 0, "Cvar gate: config-file knob"};
```

Add the gate function next to the other `run_*_gate` functions — put it just
before `run_window_gate`, which is around line 553:

```cpp
bool run_cvar_gate() {
    using engine::CvarSetResult;
    using engine::CvarSource;

    const engine::usize count = engine::cvar_count();
    bool walked = false;
    for (engine::usize i = 0; i < count; ++i) {
        if (engine::cvar_at(i) == &cv_gate_prec) {
            walked = true;
        }
    }
    const bool registry_ok = count >= 7
        && walked
        && engine::find_cvar("gate.bool") == &cv_gate_bool
        && engine::find_cvar("gate.string") == &cv_gate_string
        && engine::find_cvar("gate.nope") == nullptr
        && engine::cvar_at(count) == nullptr;

    const bool types_ok =
        cv_gate_bool.set("on", CvarSource::Code) == CvarSetResult::Applied
        && cv_gate_bool.as_bool()
        && cv_gate_bool.set("0", CvarSource::Code) == CvarSetResult::Applied
        && !cv_gate_bool.as_bool()
        && cv_gate_bool.set("maybe", CvarSource::Code) == CvarSetResult::Invalid
        && !cv_gate_bool.as_bool()
        && cv_gate_int.set("-42", CvarSource::Code) == CvarSetResult::Applied
        && cv_gate_int.as_int() == -42
        && cv_gate_int.set("4.5", CvarSource::Code) == CvarSetResult::Invalid
        && cv_gate_int.set("", CvarSource::Code) == CvarSetResult::Invalid
        && cv_gate_float.set("-0.25", CvarSource::Code) == CvarSetResult::Applied
        && cv_gate_float.as_float() < -0.24f && cv_gate_float.as_float() > -0.26f
        && cv_gate_string.set("two words", CvarSource::Code) == CvarSetResult::Applied
        && cv_gate_string.as_string() == "two words"
        && cv_gate_bool.type() == engine::CvarType::Bool
        && cv_gate_int.type() == engine::CvarType::Int
        && cv_gate_float.type() == engine::CvarType::Float
        && cv_gate_string.type() == engine::CvarType::String;

    // A lower source never overwrites a higher one. This is what lets the
    // engine load config.cfg after the command line.
    const bool precedence_ok =
        cv_gate_prec.source() == CvarSource::Default
        && cv_gate_prec.set("1", CvarSource::File) == CvarSetResult::Applied
        && cv_gate_prec.set("2", CvarSource::CommandLine) == CvarSetResult::Applied
        && cv_gate_prec.set("3", CvarSource::File) == CvarSetResult::Ignored
        && cv_gate_prec.as_int() == 2
        && cv_gate_prec.source() == CvarSource::CommandLine
        && cv_gate_prec.set("4", CvarSource::Code) == CvarSetResult::Applied
        && cv_gate_prec.as_int() == 4;

    const bool passed = registry_ok && types_ok && precedence_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Cvar gate: count=%llu types=%s precedence=%s (%s)",
        static_cast<unsigned long long>(count),
        types_ok ? "yes" : "no",
        precedence_ok ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::General, message);
    return passed;
}
```

Call it with the other gates. Find
`bool gates_ok = run_mount_gate(*loader) && run_build_gate(app.content_layout());`
(around line 4305) and add immediately after it:

```cpp
    if (!run_cvar_gate()) {
        gates_ok = false;
    }
```

- [ ] **Step 2: Build to verify it fails**

Run:

```powershell
cmake --build build --config Debug --target sandbox
```

Expected: FAIL. MSVC reports
`C1083: Cannot open include file: 'engine/core/cvar.hpp'`. That is the red
state — the type under test does not exist yet.

- [ ] **Step 3: Write the header**

Create `packages/core/include/engine/core/cvar.hpp`:

```cpp
#pragma once

#include <engine/core/types.hpp>

#include <string>
#include <string_view>

namespace engine {

enum class CvarType : u8 { Bool, Int, Float, String };

// Higher wins. A write is accepted only when its source is at least the source
// that last wrote the cvar, so the engine may load config.cfg after the
// command line and --set still wins.
enum class CvarSource : u8 { Default = 0, File = 1, CommandLine = 2, Code = 3 };

enum class CvarSetResult : u8 {
    Applied,  // parsed and stored
    Invalid,  // the text does not parse as this cvar's type
    Ignored,  // parsed, but a higher-precedence source already owns the value
};

const char* cvar_type_name(CvarType type);
const char* cvar_source_name(CvarSource source);

// A named knob. Declare one at file scope next to the code that reads it:
//
//   namespace { engine::Cvar cv_vsync{"r.vsync", true, "Present with vsync"}; }
//
// Construction registers it. Not thread-safe: drive it from startup and the
// main thread only.
class Cvar {
public:
    Cvar(const char* name, bool value, const char* help);
    Cvar(const char* name, i32 value, const char* help);
    Cvar(const char* name, f32 value, const char* help);
    Cvar(const char* name, const char* value, const char* help);

    Cvar(const Cvar&) = delete;
    Cvar& operator=(const Cvar&) = delete;

    const char* name() const { return name_; }
    const char* help() const { return help_; }
    CvarType    type() const { return type_; }
    CvarSource  source() const { return source_; }

    // Reading the wrong type is a programming error, not a config error: these
    // assert. ENGINE_ASSERT is always active and assert_fail is [[noreturn]],
    // so a mismatch aborts rather than returning a wrong value.
    bool             as_bool() const;
    i32              as_int() const;
    f32              as_float() const;
    std::string_view as_string() const;

    CvarSetResult set(std::string_view text, CvarSource source);

private:
    const char* name_;
    const char* help_;
    CvarType    type_;
    CvarSource  source_ = CvarSource::Default;
    bool        bool_   = false;
    i32         int_    = 0;
    f32         float_  = 0.f;
    std::string string_;
};

struct CvarApplyStats {
    usize applied = 0;
    usize unknown = 0;  // no cvar by that name
    usize invalid = 0;  // bad value, or a malformed line
    usize ignored = 0;  // a higher-precedence source already owns the value
};

// Linear scan. There are a few dozen knobs; a map would cost more than it saves.
Cvar* find_cvar(std::string_view name);
usize cvar_count();
Cvar* cvar_at(usize index);  // nullptr when index is out of range

CvarApplyStats apply_cvar_text(std::string_view text, CvarSource source);
CvarApplyStats apply_cvar_args(int argc, const char* const* argv);

} // namespace engine
```

- [ ] **Step 4: Write the implementation**

Create `packages/core/src/cvar.cpp`. `apply_cvar_text` and `apply_cvar_args`
are stubs in this task — Tasks 2 and 3 fill them in and gate them.

```cpp
#include <engine/core/cvar.hpp>

#include <engine/core/assert.hpp>

#include <charconv>
#include <vector>

namespace engine {

namespace {

// Function-local static: safe no matter what order translation units run their
// static constructors in.
std::vector<Cvar*>& registry() {
    static std::vector<Cvar*> list;
    return list;
}

bool ascii_iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (usize i = 0; i < a.size(); ++i) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') { ca = static_cast<char>(ca - 'A' + 'a'); }
        if (cb >= 'A' && cb <= 'Z') { cb = static_cast<char>(cb - 'A' + 'a'); }
        if (ca != cb) {
            return false;
        }
    }
    return true;
}

bool parse_bool(std::string_view text, bool& out) {
    if (ascii_iequals(text, "1") || ascii_iequals(text, "true")
        || ascii_iequals(text, "on") || ascii_iequals(text, "yes")) {
        out = true;
        return true;
    }
    if (ascii_iequals(text, "0") || ascii_iequals(text, "false")
        || ascii_iequals(text, "off") || ascii_iequals(text, "no")) {
        out = false;
        return true;
    }
    return false;
}

bool parse_int(std::string_view text, i32& out) {
    if (text.empty()) {
        return false;
    }
    i32 value = 0;
    const char* first = text.data();
    const char* last = text.data() + text.size();
    const auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc{} || result.ptr != last) {
        return false;  // trailing junk is a typo, not a number
    }
    out = value;
    return true;
}

bool parse_float(std::string_view text, f32& out) {
    if (text.empty()) {
        return false;
    }
    f32 value = 0.f;
    const char* first = text.data();
    const char* last = text.data() + text.size();
    const auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc{} || result.ptr != last) {
        return false;
    }
    out = value;
    return true;
}

void register_cvar(Cvar* cvar) {
    ENGINE_ASSERT(cvar != nullptr);
    for (Cvar* existing : registry()) {
        ENGINE_ASSERT_MSG(!ascii_iequals(existing->name(), cvar->name()),
            "duplicate cvar name");
    }
    registry().push_back(cvar);
}

} // namespace

const char* cvar_type_name(CvarType type) {
    switch (type) {
    case CvarType::Bool:   return "bool";
    case CvarType::Int:    return "int";
    case CvarType::Float:  return "float";
    case CvarType::String: return "string";
    }
    return "bool";
}

const char* cvar_source_name(CvarSource source) {
    switch (source) {
    case CvarSource::File:        return "file";
    case CvarSource::CommandLine: return "cli";
    case CvarSource::Code:        return "code";
    default:                      return "default";
    }
}

Cvar::Cvar(const char* name, bool value, const char* help)
    : name_(name), help_(help), type_(CvarType::Bool), bool_(value) {
    register_cvar(this);
}

Cvar::Cvar(const char* name, i32 value, const char* help)
    : name_(name), help_(help), type_(CvarType::Int), int_(value) {
    register_cvar(this);
}

Cvar::Cvar(const char* name, f32 value, const char* help)
    : name_(name), help_(help), type_(CvarType::Float), float_(value) {
    register_cvar(this);
}

Cvar::Cvar(const char* name, const char* value, const char* help)
    : name_(name), help_(help), type_(CvarType::String), string_(value ? value : "") {
    register_cvar(this);
}

bool Cvar::as_bool() const {
    ENGINE_ASSERT_MSG(type_ == CvarType::Bool, "cvar read as the wrong type");
    return bool_;
}

i32 Cvar::as_int() const {
    ENGINE_ASSERT_MSG(type_ == CvarType::Int, "cvar read as the wrong type");
    return int_;
}

f32 Cvar::as_float() const {
    ENGINE_ASSERT_MSG(type_ == CvarType::Float, "cvar read as the wrong type");
    return float_;
}

std::string_view Cvar::as_string() const {
    ENGINE_ASSERT_MSG(type_ == CvarType::String, "cvar read as the wrong type");
    return string_;
}

CvarSetResult Cvar::set(std::string_view text, CvarSource source) {
    // Parse before the precedence check, so a typo in a shadowed line is still
    // reported as a typo.
    bool bool_value = false;
    i32 int_value = 0;
    f32 float_value = 0.f;
    switch (type_) {
    case CvarType::Bool:
        if (!parse_bool(text, bool_value)) { return CvarSetResult::Invalid; }
        break;
    case CvarType::Int:
        if (!parse_int(text, int_value)) { return CvarSetResult::Invalid; }
        break;
    case CvarType::Float:
        if (!parse_float(text, float_value)) { return CvarSetResult::Invalid; }
        break;
    case CvarType::String:
        if (text.empty()) { return CvarSetResult::Invalid; }
        break;
    }

    if (static_cast<u8>(source) < static_cast<u8>(source_)) {
        return CvarSetResult::Ignored;
    }

    switch (type_) {
    case CvarType::Bool:   bool_ = bool_value; break;
    case CvarType::Int:    int_ = int_value; break;
    case CvarType::Float:  float_ = float_value; break;
    case CvarType::String: string_.assign(text); break;
    }
    source_ = source;
    return CvarSetResult::Applied;
}

Cvar* find_cvar(std::string_view name) {
    for (Cvar* cvar : registry()) {
        if (ascii_iequals(cvar->name(), name)) {
            return cvar;
        }
    }
    return nullptr;
}

usize cvar_count() {
    return registry().size();
}

Cvar* cvar_at(usize index) {
    return index < registry().size() ? registry()[index] : nullptr;
}

CvarApplyStats apply_cvar_text(std::string_view, CvarSource) {
    return {};
}

CvarApplyStats apply_cvar_args(int, const char* const*) {
    return {};
}

} // namespace engine
```

- [ ] **Step 5: Add the source to the build**

In `packages/core/CMakeLists.txt`, add `src/cvar.cpp` to the `SOURCES` list:

```cmake
engine_add_package(core
    SOURCES
        src/log.cpp
        src/assert.cpp
        src/clock.cpp
        src/frame.cpp
        src/arena.cpp
        src/profile.cpp
        src/cvar.cpp
)
```

- [ ] **Step 6: Build and run the gate**

Run:

```powershell
cmake --build build --config Debug --target sandbox
```

Expected: builds clean, no warnings from `cvar.cpp`.

Run:

```powershell
.\build\bin\Debug\sandbox.exe --gates
```

Expected: a line `Cvar gate: count=7 types=yes precedence=yes (pass)`, and the
run still ends in `gates passed`.

- [ ] **Step 7: Commit**

```bash
git add packages/core/include/engine/core/cvar.hpp packages/core/src/cvar.cpp packages/core/CMakeLists.txt packages/sandbox/src/main.cpp
```

```bash
git commit -m "feat(core): cvar registry with typed values and source precedence"
```

```bash
git push
```

---

## Task 2: The config-file text parser

**Files:**
- Modify: `packages/core/src/cvar.cpp` (replace the `apply_cvar_text` stub)
- Test (gate): `packages/sandbox/src/main.cpp` (`run_cvar_gate`)

- [ ] **Step 1: Declare three more gate knobs**

The `types_ok` block already drove `gate.int`, `gate.float` and `gate.string`
to `CvarSource::Code`, so a `File` write to them would be `Ignored`. The text
parser needs knobs no `Code` write has touched. Add these next to the other
gate knobs in the anonymous namespace:

```cpp
engine::Cvar cv_text_int{"gate.text_int", 0, "Cvar gate: text parser int"};
engine::Cvar cv_text_float{"gate.text_float", 0.f, "Cvar gate: text parser float"};
engine::Cvar cv_text_string{"gate.text_string", "unset", "Cvar gate: text parser string"};
```

- [ ] **Step 2: Write the failing gate assertions**

In `run_cvar_gate`, insert this block after the `types_ok` block and before
`precedence_ok`:

```cpp
    // Comments, both separators, blank lines, one unknown key, one bad value.
    static constexpr const char* kText =
        "# hash comment\n"
        "// slash comment\n"
        "\n"
        "   \n"
        "gate.text_int 7\n"
        "gate.text_float = 1.5\n"
        "gate.text_string   hello world  \r\n"
        "gate.nothere 1\n"
        "gate.text_int oops\n";
    const auto text_stats = engine::apply_cvar_text(kText, CvarSource::File);
    const bool text_ok = text_stats.applied == 3
        && text_stats.unknown == 1
        && text_stats.invalid == 1
        && text_stats.ignored == 0
        && cv_text_int.as_int() == 7
        && cv_text_float.as_float() > 1.49f && cv_text_float.as_float() < 1.51f
        && cv_text_string.as_string() == "hello world";
```

Raise `registry_ok`'s count floor from `count >= 7` to `count >= 10`, add
`text_ok` to `passed`, and extend the log line:

```cpp
    const bool passed = registry_ok && text_ok && types_ok && precedence_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Cvar gate: count=%llu text=%s types=%s precedence=%s (%s)",
        static_cast<unsigned long long>(count),
        text_ok ? "yes" : "no",
        types_ok ? "yes" : "no",
        precedence_ok ? "yes" : "no",
        passed ? "pass" : "FAIL");
```

- [ ] **Step 3: Build and run to verify it fails**

Run:

```powershell
cmake --build build --config Debug --target sandbox
```

```powershell
.\build\bin\Debug\sandbox.exe --gates
```

Expected: `Cvar gate: count=10 text=no types=yes precedence=yes (FAIL)` and the
run ends in `gates FAILED`. The stub returns all-zero stats, so
`applied == 3` is false.

- [ ] **Step 4: Implement the parser**

In `packages/core/src/cvar.cpp`, add these helpers inside the existing
anonymous namespace, below `parse_float`:

```cpp
std::string_view trim(std::string_view text) {
    usize begin = 0;
    while (begin < text.size()
        && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r')) {
        ++begin;
    }
    usize end = text.size();
    while (end > begin
        && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r')) {
        --end;
    }
    return text.substr(begin, end - begin);
}

bool is_comment(std::string_view line) {
    return line.starts_with("#") || line.starts_with("//");
}

// "key value", "key = value" and "key=value" all split the same way.
bool split_line(std::string_view line, std::string_view& key, std::string_view& value) {
    const usize split = line.find_first_of(" \t=");
    if (split == std::string_view::npos) {
        return false;  // a bare key with no value
    }
    key = trim(line.substr(0, split));
    std::string_view rest = trim(line.substr(split));
    if (rest.starts_with("=")) {
        rest = trim(rest.substr(1));
    }
    value = rest;
    return !key.empty() && !value.empty();
}

void apply_one(std::string_view key, std::string_view value, CvarSource source,
    CvarApplyStats& stats) {
    Cvar* cvar = find_cvar(key);
    if (!cvar) {
        ++stats.unknown;
        log(LogLevel::Warn, LogChannel::General,
            std::string("Unknown cvar '") + std::string(key) + "'");
        return;
    }
    switch (cvar->set(value, source)) {
    case CvarSetResult::Applied:
        ++stats.applied;
        break;
    case CvarSetResult::Ignored:
        ++stats.ignored;
        break;
    case CvarSetResult::Invalid:
        ++stats.invalid;
        log(LogLevel::Warn, LogChannel::General,
            std::string("Cvar '") + cvar->name() + "' rejected value '"
                + std::string(value) + "' (expected "
                + cvar_type_name(cvar->type()) + ")");
        break;
    }
}
```

`apply_one` logs, so add the log include at the top of `cvar.cpp`, under the
`assert.hpp` include:

```cpp
#include <engine/core/log.hpp>
```

Now replace the `apply_cvar_text` stub with the real thing:

```cpp
CvarApplyStats apply_cvar_text(std::string_view text, CvarSource source) {
    CvarApplyStats stats{};
    usize begin = 0;
    while (begin <= text.size()) {
        const usize newline = text.find('\n', begin);
        const usize end = newline == std::string_view::npos ? text.size() : newline;
        const std::string_view line = trim(text.substr(begin, end - begin));
        if (!line.empty() && !is_comment(line)) {
            std::string_view key;
            std::string_view value;
            if (split_line(line, key, value)) {
                apply_one(key, value, source, stats);
            } else {
                ++stats.invalid;
                log(LogLevel::Warn, LogChannel::General,
                    std::string("Malformed cvar line '") + std::string(line) + "'");
            }
        }
        if (newline == std::string_view::npos) {
            break;
        }
        begin = newline + 1;
    }
    return stats;
}
```

- [ ] **Step 5: Build and run to verify it passes**

Run:

```powershell
cmake --build build --config Debug --target sandbox
```

```powershell
.\build\bin\Debug\sandbox.exe --gates
```

Expected: `Cvar gate: count=10 text=yes types=yes precedence=yes (pass)`.
Two warning lines also appear above it — `Unknown cvar 'gate.nothere'` and
`Cvar 'gate.text_int' rejected value 'oops' (expected int)`. Those are the gate
proving the counters, not a problem.

- [ ] **Step 6: Commit**

```bash
git add packages/core/src/cvar.cpp packages/sandbox/src/main.cpp
```

```bash
git commit -m "feat(core): parse cvar config text with comments and both separators"
```

```bash
git push
```

---

## Task 3: The `--set` command line

**Files:**
- Modify: `packages/core/src/cvar.cpp` (replace the `apply_cvar_args` stub)
- Modify: `packages/sandbox/src/main.cpp` (gate assertions + call it in `main`)

- [ ] **Step 1: Write the failing gate assertions**

In `run_cvar_gate`, add this block after the `precedence_ok` block:

```cpp
    // Both value forms, plus the two malformed tails.
    static const char* kArgvOk[] = {
        "sandbox.exe", "--gates", "--set", "gate.args=11", "--set", "gate.args", "12"};
    const auto args_stats = engine::apply_cvar_args(7, kArgvOk);
    static const char* kArgvDangling[] = {"sandbox.exe", "--set"};
    const auto dangling_stats = engine::apply_cvar_args(2, kArgvDangling);
    static const char* kArgvNoValue[] = {
        "sandbox.exe", "--set", "gate.args", "--set", "gate.args=13"};
    const auto no_value_stats = engine::apply_cvar_args(5, kArgvNoValue);
    const bool args_ok = args_stats.applied == 2
        && args_stats.unknown == 0
        && args_stats.invalid == 0
        && cv_gate_args.as_int() == 12
        && dangling_stats.invalid == 1 && dangling_stats.applied == 0
        && no_value_stats.invalid == 1 && no_value_stats.applied == 1
        && cv_gate_args.as_int() == 13;
```

Add `args_ok` to `passed` and to the log line:

```cpp
    const bool passed = registry_ok && text_ok && types_ok && precedence_ok && args_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Cvar gate: count=%llu text=%s types=%s precedence=%s args=%s (%s)",
        static_cast<unsigned long long>(count),
        text_ok ? "yes" : "no",
        types_ok ? "yes" : "no",
        precedence_ok ? "yes" : "no",
        args_ok ? "yes" : "no",
        passed ? "pass" : "FAIL");
```

- [ ] **Step 2: Build and run to verify it fails**

Run:

```powershell
cmake --build build --config Debug --target sandbox
```

```powershell
.\build\bin\Debug\sandbox.exe --gates
```

Expected: `args=no` and `(FAIL)`. The stub applies nothing, so
`args_stats.applied == 2` is false.

- [ ] **Step 3: Implement the argv parser**

Replace the `apply_cvar_args` stub in `packages/core/src/cvar.cpp`:

```cpp
CvarApplyStats apply_cvar_args(int argc, const char* const* argv) {
    CvarApplyStats stats{};
    if (argc <= 0 || !argv) {
        return stats;
    }
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i] ? argv[i] : "";
        if (arg != "--set") {
            continue;  // not an argument parser; --gates and friends are not ours
        }
        if (i + 1 >= argc) {
            ++stats.invalid;
            log(LogLevel::Warn, LogChannel::General, "--set with no key=value");
            break;
        }
        const std::string_view token = argv[++i] ? argv[i] : "";
        const usize equals = token.find('=');
        if (equals != std::string_view::npos) {
            apply_one(trim(token.substr(0, equals)), trim(token.substr(equals + 1)),
                CvarSource::CommandLine, stats);
            continue;
        }
        if (i + 1 >= argc
            || std::string_view(argv[i + 1] ? argv[i + 1] : "") == "--set") {
            ++stats.invalid;
            log(LogLevel::Warn, LogChannel::General,
                std::string("--set ") + std::string(token) + " has no value");
            continue;
        }
        apply_one(token, trim(argv[++i] ? argv[i] : ""), CvarSource::CommandLine, stats);
    }
    return stats;
}
```

`split_line` is deliberately not reused here: an empty value after `=` reaches
`Cvar::set`, which rejects it as `Invalid`, which is the behaviour we want.

- [ ] **Step 4: Build and run to verify it passes**

Run:

```powershell
cmake --build build --config Debug --target sandbox
```

```powershell
.\build\bin\Debug\sandbox.exe --gates
```

Expected: `args=yes` and `(pass)`.

- [ ] **Step 5: Apply the real command line in `main`**

In `packages/sandbox/src/main.cpp`, find the `--gates` scan at the top of
`main` (around line 4053) and add the cvar pass right after it:

```cpp
int main(int argc, char** argv) {
    bool gates_mode = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--gates") {
            gates_mode = true;
        }
    }
    // Before Engine::init, so config.cfg cannot overwrite a --set value.
    engine::apply_cvar_args(argc, argv);
```

- [ ] **Step 6: Verify the real command line works**

Run:

```powershell
.\build\bin\Debug\sandbox.exe --gates --set gate.args=5
```

Expected: still `gates passed`. The gate overwrites `gate.args` itself, so the
value is not observable here — what this proves is that a real `--set` on the
command line neither warns nor crashes. No `Unknown cvar` warning for
`gate.args` should appear.

Run:

```powershell
.\build\bin\Debug\sandbox.exe --gates --set nope.nope=1
```

Expected: an `Unknown cvar 'nope.nope'` warning early in the log, and the run
still ends `gates passed` — a typo must not stop the engine booting.

- [ ] **Step 7: Commit**

```bash
git add packages/core/src/cvar.cpp packages/sandbox/src/main.cpp
```

```bash
git commit -m "feat(core): --set key=value cvar overrides on the command line"
```

```bash
git push
```

---

## Task 4: Loading `config.cfg` through the filesystem

**Files:**
- Create: `packages/engine/include/engine/cvar_file.hpp`
- Create: `packages/engine/src/cvar_file.cpp`
- Modify: `packages/engine/CMakeLists.txt`
- Test (gate): `packages/sandbox/src/main.cpp`

- [ ] **Step 1: Write the failing gate assertions**

`run_cvar_gate` needs the filesystem and a scratch directory, so change its
signature:

```cpp
bool run_cvar_gate(engine::platform::IFileSystem* fs, const std::string& scratch_dir) {
```

Add the include at the top of `main.cpp`, with the other engine includes:

```cpp
#include <engine/cvar_file.hpp>
```

`main.cpp` already includes `<span>`, `<cstring>` and `<string>`, which this
block needs — do not add them again.

Add these assertions at the end of the gate body, after the `args_ok` block:

```cpp
    // The engine's own loader, not a private copy of it.
    bool file_ok = false;
    bool missing_ok = false;
    if (fs) {
        const std::string path = scratch_dir + "/gate_cvars.cfg";
        static constexpr const char* kBody = "# written by the cvar gate\ngate.file 99\n";
        const std::span<const engine::u8> body{
            reinterpret_cast<const engine::u8*>(kBody), std::strlen(kBody)};
        if (fs->write_file(path, body)) {
            bool found = false;
            const auto stats = engine::apply_cvar_file(*fs, path, &found);
            file_ok = found && stats.applied == 1 && stats.invalid == 0
                && cv_gate_file.as_int() == 99;
        }
        bool missing_found = true;
        const auto missing_stats =
            engine::apply_cvar_file(*fs, scratch_dir + "/no_such_file.cfg", &missing_found);
        missing_ok = !missing_found && missing_stats.applied == 0
            && missing_stats.invalid == 0 && missing_stats.unknown == 0;
    }
```

Add both to `passed` and the log line:

```cpp
    const bool passed = registry_ok && text_ok && types_ok && precedence_ok && args_ok
        && file_ok && missing_ok;
    char message[256];
    std::snprintf(message, sizeof(message),
        "Cvar gate: count=%llu text=%s types=%s precedence=%s args=%s file=%s missing=%s (%s)",
        static_cast<unsigned long long>(count),
        text_ok ? "yes" : "no",
        types_ok ? "yes" : "no",
        precedence_ok ? "yes" : "no",
        args_ok ? "yes" : "no",
        file_ok ? "yes" : "no",
        missing_ok ? "yes" : "no",
        passed ? "pass" : "FAIL");
```

Update the call site to hand it the filesystem and a scratch directory. The
executable directory is under `build/`, which `.gitignore` already covers, so
the gate's throwaway `.cfg` never dirties the tree:

```cpp
    if (!run_cvar_gate(app.filesystem(), app.executable_directory())) {
        gates_ok = false;
    }
```

- [ ] **Step 2: Build to verify it fails**

Run:

```powershell
cmake --build build --config Debug --target sandbox
```

Expected: FAIL — `C1083: Cannot open include file: 'engine/cvar_file.hpp'`.

- [ ] **Step 3: Write the header**

Create `packages/engine/include/engine/cvar_file.hpp`. It is its own header
rather than more surface on `engine.hpp`, because it has exactly one job:

```cpp
#pragma once

#include <engine/core/cvar.hpp>

#include <string_view>

namespace engine::platform {
class IFileSystem;
}

namespace engine {

// Reads path through the filesystem and applies it as CvarSource::File. This is
// the only place a path becomes cvar text; core never opens a file.
//
// found (when given) reports whether the file existed. A missing file is not an
// error: most installs have no config.cfg.
CvarApplyStats apply_cvar_file(platform::IFileSystem& fs, std::string_view path,
    bool* found = nullptr);

// Looked for at <content_root>/config.cfg.
inline constexpr const char* kCvarFileName = "config.cfg";

} // namespace engine
```

- [ ] **Step 4: Write the implementation**

Create `packages/engine/src/cvar_file.cpp`:

```cpp
#include <engine/cvar_file.hpp>

#include <engine/platform/filesystem.hpp>

#include <vector>

namespace engine {

CvarApplyStats apply_cvar_file(platform::IFileSystem& fs, std::string_view path, bool* found) {
    std::vector<u8> bytes;
    const bool read = fs.read_file(path, bytes);
    if (found) {
        *found = read;
    }
    if (!read || bytes.empty()) {
        return {};  // an empty file has nothing to apply, and data() may be null
    }
    return apply_cvar_text(
        std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
        CvarSource::File);
}

} // namespace engine
```

- [ ] **Step 5: Add the source to the build**

In `packages/engine/CMakeLists.txt`, add `src/cvar_file.cpp` to the `SOURCES`
list next to `src/engine.cpp`. Read the file first and match its existing
formatting.

- [ ] **Step 6: Build and run to verify it passes**

Run:

```powershell
cmake --build build --config Debug --target sandbox
```

```powershell
.\build\bin\Debug\sandbox.exe --gates
```

Expected: `file=yes missing=yes` and `(pass)`.

Confirm the scratch file is not tracked:

```bash
git status --porcelain
```

Expected: no `gate_cvars.cfg` entry — it is under `build/`.

- [ ] **Step 7: Commit**

```bash
git add packages/engine/include/engine/cvar_file.hpp packages/engine/src/cvar_file.cpp packages/engine/CMakeLists.txt packages/sandbox/src/main.cpp
```

```bash
git commit -m "feat(engine): load config.cfg into cvars through IFileSystem"
```

```bash
git push
```

---

## Task 5: Wire the window knobs into `Engine::init`

**Files:**
- Modify: `packages/platform/include/engine/platform/window.hpp`
- Modify: `packages/engine/src/engine.cpp:139-183`

- [ ] **Step 1: Add the window-mode parser**

In `packages/platform/include/engine/platform/window.hpp`, add this directly
below the existing `window_mode_name`:

```cpp
// Lowercase tokens only. These come from config text, and the enum-valued cvars
// document their accepted values in their help string.
inline bool parse_window_mode(std::string_view text, WindowMode& out) {
    if (text == "windowed")   { out = WindowMode::Windowed;   return true; }
    if (text == "borderless") { out = WindowMode::Borderless; return true; }
    if (text == "fullscreen") { out = WindowMode::Fullscreen; return true; }
    return false;
}
```

`window.hpp` already includes `<string_view>`, so no include change is needed.

- [ ] **Step 2: Declare the engine knobs**

In `packages/engine/src/engine.cpp`, add the includes at the top with the other
`engine/core` includes:

```cpp
#include <engine/core/cvar.hpp>
#include <engine/cvar_file.hpp>
```

Inside the existing anonymous namespace (the one holding
`looks_like_repo_root`), add the knobs at the top:

```cpp
// EngineConfig still holds the code-level defaults. A cvar only overrides a
// field when something actually set it, which is why every read below checks
// source() != CvarSource::Default.
Cvar cv_window_width{"window.width", 1280, "Window client width in pixels"};
Cvar cv_window_height{"window.height", 720, "Window client height in pixels"};
Cvar cv_window_mode{"window.mode", "windowed", "windowed | borderless | fullscreen"};
Cvar cv_vsync{"r.vsync", true, "Present with vsync"};

u32 positive_dimension_cvar(const Cvar& cvar, u32 fallback) {
    const i32 value = cvar.as_int();
    if (value <= 0) {
        log(LogLevel::Warn, LogChannel::Platform,
            std::string("Cvar '") + cvar.name() + "' must be positive; keeping the default");
        return fallback;
    }
    return static_cast<u32>(value);
}
```

- [ ] **Step 3: Reorder `init` and apply the knobs**

Replace the body of `Engine::init` from the platform check down to the
`frame_arena_.emplace` line. The filesystem and content root move **above**
window creation — neither depends on a window — so `config.cfg` is read before
the window is sized:

```cpp
bool Engine::init(const EngineConfig& config) {
    config_ = config;
    frame_timer_ = FrameTimer(config_.frame);

    if (!modules_.platform) {
        log(LogLevel::Error, LogChannel::Platform, "Engine: no platform module");
        return false;
    }

    // Filesystem and content root come first: neither needs a window, and
    // config.cfg has to be read before the window is sized.
    filesystem_ = modules_.platform->create_filesystem();
    content_root_ = discover_content_root(*modules_.platform, config_.content_root);
    content_layout_ = resolve_content_mounts(content_root_).layout;
    log(LogLevel::Info, LogChannel::General,
        std::string("Content root: ") + content_root_ + " ("
            + content_layout_name(content_layout_) + ")");

    CvarApplyStats file_stats{};
    if (filesystem_) {
        const std::string cvar_path =
            (std::filesystem::path(content_root_) / kCvarFileName).string();
        file_stats = apply_cvar_file(*filesystem_, cvar_path);
    }

    auto window_desc = config.window;
    if (cv_window_width.source() != CvarSource::Default) {
        window_desc.width = positive_dimension_cvar(cv_window_width, window_desc.width);
    }
    if (cv_window_height.source() != CvarSource::Default) {
        window_desc.height = positive_dimension_cvar(cv_window_height, window_desc.height);
    }
    if (cv_window_mode.source() != CvarSource::Default) {
        platform::WindowMode mode = window_desc.mode;
        if (platform::parse_window_mode(cv_window_mode.as_string(), mode)) {
            window_desc.mode = mode;
        } else {
            log(LogLevel::Warn, LogChannel::Platform,
                std::string("Cvar 'window.mode' expects ") + cv_window_mode.help());
        }
    }
    if (cv_vsync.source() != CvarSource::Default) {
        window_desc.vsync = cv_vsync.as_bool();
    }

    usize cli_count = 0;
    for (usize i = 0; i < cvar_count(); ++i) {
        const Cvar* cvar = cvar_at(i);
        if (cvar && cvar->source() == CvarSource::CommandLine) {
            ++cli_count;
        }
    }
    char cvar_message[192];
    std::snprintf(cvar_message, sizeof(cvar_message),
        "Cvars: file=%llu cli=%llu window=%ux%u %s vsync=%s",
        static_cast<unsigned long long>(file_stats.applied),
        static_cast<unsigned long long>(cli_count),
        window_desc.width, window_desc.height,
        platform::window_mode_name(window_desc.mode),
        window_desc.vsync ? "on" : "off");
    log(LogLevel::Info, LogChannel::General, cvar_message);

    window_ = modules_.platform->create_window(window_desc);
    if (!window_) {
        log(LogLevel::Error, LogChannel::Platform, "Engine: failed to create window");
        return false;
    }

    input_ = modules_.platform->create_input(*window_);

    frame_arena_.emplace(config_.frame_arena_bytes);
```

Leave the rest of `init` — the `modules_.rhi` block, the `Engine initialized`
log, `running_ = true` — exactly as it is.

`std::snprintf` needs `<cstdio>`; add it with the other standard includes at
the top of `engine.cpp` if it is not already there.

- [ ] **Step 4: Build and check the defaults did not move**

Run:

```powershell
cmake --build build --config Debug --target sandbox
```

```powershell
.\build\bin\Debug\sandbox.exe --gates
```

Expected: `Cvars: file=0 cli=0 window=1280x720 windowed vsync=on`, the Cvar
gate's count rising from 10 to 14 as the four engine knobs register, the
`Window gate: ... (pass)` line still passing (it asserts the default is
windowed with vsync on), and the run ending `gates passed`. The
`Content root:` line now appears *before* the window is created — that
reorder is the point of this task.

- [ ] **Step 5: Verify the command line drives the window**

Run:

```powershell
.\build\bin\Debug\sandbox.exe --gates --set window.mode=borderless --set window.width=1600 --set window.height=900
```

Expected: `Cvars: file=0 cli=3 window=1600x900 borderless vsync=on`.

The `Window gate` asserts the window comes up windowed with vsync on, so this
run is *expected* to report `Window gate: ... (FAIL)`. That is the gate
correctly observing that a knob changed the startup mode, not a defect. Run it
without `--gates` to see it as a user would:

```powershell
.\build\bin\Debug\sandbox.exe --set window.mode=borderless
```

Expected: the window opens borderless covering the monitor, and F11 still
toggles back to windowed.

- [ ] **Step 6: Verify the config file drives the window**

Create `config.cfg` at the repo root — that is the repo content root:

```
# local dev knobs
window.width 1024
window.height 768
r.vsync 0
```

Run:

```powershell
.\build\bin\Debug\sandbox.exe --gates
```

Expected: `Cvars: file=3 cli=0 window=1024x768 windowed vsync=off`.

Now prove the precedence rule. The file is loaded *after* the command line, and
the command line must still win:

```powershell
.\build\bin\Debug\sandbox.exe --gates --set window.width=1440
```

Expected: `Cvars: file=2 cli=1 window=1440x768 windowed vsync=off`. Note
`file=2`, not 3 — the `window.width` line in the file was `Ignored`, not
applied. That single number is the whole precedence design working end to end.

Leave `config.cfg` in place or delete it; Task 7 gitignores it either way.

- [ ] **Step 7: Commit**

```bash
git add packages/platform/include/engine/platform/window.hpp packages/engine/src/engine.cpp
```

```bash
git commit -m "feat(engine): window size, mode and vsync from cvars at startup"
```

```bash
git push
```

---

## Task 6: The `r.aa` knob and the AA gate's live-state clause

**Files:**
- Modify: `packages/renderer/include/engine/renderer/aa.hpp`
- Modify: `packages/sandbox/src/main.cpp` (demo setup + `run_aa_gate`, around line 2015)

- [ ] **Step 1: Add the AA-mode parser**

In `packages/renderer/include/engine/renderer/aa.hpp`, add `<string_view>` to
the includes, then add this directly below the existing `mode_name`:

```cpp
// Lowercase tokens only. mode_name returns the display spelling, which is
// uppercase, so this is deliberately not its inverse.
inline bool parse_mode(std::string_view text, Mode& out) {
    if (text == "off")  { out = Mode::Off;  return true; }
    if (text == "fxaa") { out = Mode::Fxaa; return true; }
    if (text == "smaa") { out = Mode::Smaa; return true; }
    if (text == "taa")  { out = Mode::Taa;  return true; }
    return false;
}
```

- [ ] **Step 2: Fix the AA gate's live-state clause**

`run_aa_gate` currently ends with `&& demo.aa_mode == kDefault` (around line
2015). That asserts *live demo state*, which `r.aa` is now allowed to change,
so it would turn a working knob into a gate failure. The policy it means to
protect — that the default is Off — is already covered by `policy_ok`'s
`kDefault == Mode::Off`. Replace the live-state clause with a round-trip
assertion on the new parser, which is a real thing to check and cannot go
stale.

Find:

```cpp
    const bool passed = policy_ok && luma_ok && fxaa_ok && smaa_ok && exclusive_ok
        && demo.aa_mode == kDefault;
```

Replace with:

```cpp
    Mode parsed = Mode::Taa;
    const bool parse_ok = parse_mode("off", parsed) && parsed == Mode::Off
        && parse_mode("fxaa", parsed) && parsed == Mode::Fxaa
        && parse_mode("smaa", parsed) && parsed == Mode::Smaa
        && parse_mode("taa", parsed) && parsed == Mode::Taa
        && !parse_mode("FXAA", parsed) && !parse_mode("nope", parsed);

    const bool passed = policy_ok && luma_ok && fxaa_ok && smaa_ok && exclusive_ok
        && parse_ok;
```

Add `parse_mode` to the `using` declarations at the top of `run_aa_gate`, next
to the existing `using engine::renderer::aa::effective_mode;`:

```cpp
    using engine::renderer::aa::parse_mode;
```

- [ ] **Step 3: Declare and read the `r.aa` knob**

In `packages/sandbox/src/main.cpp`, add to the anonymous namespace next to the
gate knobs:

```cpp
// The AA default is demo state, not engine state, so the knob is read here
// rather than in Engine::init.
engine::Cvar cv_aa{"r.aa", "off", "Anti-aliasing: off | fxaa | smaa | taa"};
```

In `setup_forward_demo`, set the mode from the knob. Put this immediately
before the existing `state.forward = std::move(demo);` line (around line 4045):

```cpp
    if (cv_aa.source() != engine::CvarSource::Default) {
        engine::renderer::aa::Mode mode = demo->aa_mode;
        if (engine::renderer::aa::parse_mode(cv_aa.as_string(), mode)) {
            demo->aa_mode = mode;
        } else {
            engine::log(engine::LogLevel::Warn, engine::LogChannel::Render,
                std::string("Cvar 'r.aa' expects ") + cv_aa.help());
        }
    }
```

- [ ] **Step 4: Build and run the gates**

Run:

```powershell
cmake --build build --config Debug --target sandbox
```

```powershell
.\build\bin\Debug\sandbox.exe --gates
```

Expected: `AA gate: default=off exclusive=yes after_tonemap=yes fxaa=yes (pass)`
and `gates passed`. The Cvar gate's registry count is now 15: 7 gate knobs
from Task 1, 3 text knobs from Task 2, the 4 engine knobs from Task 5, and
`r.aa`.

- [ ] **Step 5: Verify the knob, including that `--gates` stays green**

Run:

```powershell
.\build\bin\Debug\sandbox.exe --gates --set r.aa=fxaa
```

Expected: still `gates passed` — the AA gate no longer reads live state.

Run:

```powershell
.\build\bin\Debug\sandbox.exe --set r.aa=fxaa
```

Expected: the F3 overlay reads `AA FXAA` from the first frame, and F5 still
cycles FXAA → SMAA → TAA → Off.

Run:

```powershell
.\build\bin\Debug\sandbox.exe --gates --set r.aa=FXAA
```

Expected: a warning `Cvar 'r.aa' expects Anti-aliasing: off | fxaa | smaa | taa`,
AA stays Off, and the run still ends `gates passed`.

- [ ] **Step 6: Commit**

```bash
git add packages/renderer/include/engine/renderer/aa.hpp packages/sandbox/src/main.cpp
```

```bash
git commit -m "feat(sandbox): r.aa cvar picks the startup AA mode"
```

```bash
git push
```

---

## Task 7: Ignore `config.cfg`, document the knobs, run the full gate

**Files:**
- Modify: `.gitignore`
- Modify: `docs/ARCHITECTURE.md`
- Modify: `docs/FOUNDATION.md`

- [ ] **Step 1: Ignore the local config file**

`config.cfg` is a user's local preferences at the repo root, and sits next to
`game.exe` in the install layout. Neither belongs in git. Add to `.gitignore`,
under the `# Local / secrets — never commit` block:

```
# Local cvar overrides (repo root in dev, next to game.exe when installed)
config.cfg
```

- [ ] **Step 2: Update the architecture doc**

In `docs/ARCHITECTURE.md`, the `core` row of the Packages table currently reads
`Clock, frame timer, log, arena, profile scopes`. Add cvars:

```
| `core` | 0 | lib | Clock, frame timer, log, arena, profile scopes, cvars |
```

In the same table, the `engine` row reads
`Phased loop, module injection, repo vs install content layout`. Change to:

```
| `engine` | 4 | lib | Phased loop, module injection, repo vs install content layout, `config.cfg` load |
```

- [ ] **Step 3: Document the API**

Add a section to `docs/FOUNDATION.md`, following the formatting already in that
file — read it first and match its heading depth and table style. The section
must cover:

- Declaring a knob: `namespace { engine::Cvar cv_vsync{"r.vsync", true, "help"}; }`
- The four types and their accessors, and that a wrong-type read asserts
- The precedence rule `Default < File < CommandLine < Code`, and why it lets
  the engine load the file after the command line
- `config.cfg` at `<content_root>`, the line format, and that unknown keys and
  bad values warn rather than abort
- `--set key=value` and `--set key value`
- The shipped knobs: `window.width`, `window.height`, `window.mode`, `r.vsync`,
  `r.aa`
- That enum-valued knobs take lowercase tokens only

- [ ] **Step 4: Run the full gate suite**

Run:

```powershell
cmake --build build --config Debug
```

```powershell
.\build\bin\Debug\sandbox.exe --gates
```

Expected:
`Cvar gate: count=15 text=yes types=yes precedence=yes args=yes file=yes missing=yes (pass)`
and `Sandbox gates passed`. Every other gate still passes.

- [ ] **Step 5: Run with the GPU debug layer**

This row touches no GPU code, but `Engine::init` changed, so confirm the debug
layer is silent:

```powershell
$env:ENGINE_GPU_DEBUG=1; .\build\bin\Debug\sandbox.exe --gates; Remove-Item Env:\ENGINE_GPU_DEBUG
```

Expected: `D3D12 debug layer enabled (ENGINE_GPU_DEBUG=1)`, `gates passed`, and
no debug-layer error or warning lines. Treat any debug-layer message as a
build-breaking bug, not a warning to skip.

- [ ] **Step 6: Verify the player build**

`Engine::init` is shared with `game.exe`, and the install layout resolves
`config.cfg` next to the exe:

```powershell
cmake --build build --config Release --target game
```

```powershell
.\build\bin\Release\game.exe --gates
```

Expected: `gates passed`, with a `Cvars: file=0 cli=0 ...` line.

- [ ] **Step 7: Commit**

```bash
git add .gitignore docs/ARCHITECTURE.md docs/FOUNDATION.md
```

```bash
git commit -m "docs(core): document cvars and ignore local config.cfg"
```

```bash
git push
```

- [ ] **Step 8: Ship the row**

Run `/ship-feature`. It marks Foundation #8 **Done** in `docs/ENGINE_MAP.md`,
adds the dated Why/Choice/Gate/Do-not entry to `docs/ROADMAP.md`, and commits
and pushes. Two Later rows name Foundation #8 as a blocker and should be
re-checked when it lands: Build #10 (fullscreen options, quality presets),
whose only other named blocker Platform #6 is already Done, so it becomes
**Ready**; and Debug #7 (in-engine console), which still waits on UI #2 and
Platform #4. Confirm the map reflects that.

---

## Done when

- `sandbox.exe --gates` prints
  `Cvar gate: count=15 text=yes types=yes precedence=yes args=yes file=yes missing=yes (pass)`
  and ends `gates passed`.
- `sandbox.exe --set window.mode=borderless --set r.aa=fxaa` opens a borderless
  window with FXAA on from the first frame; F11 and F5 still cycle.
- A `config.cfg` holding `r.vsync 0` reports `vsync=off` in the startup
  `Cvars:` line, and a `--set window.width=1440` alongside a `window.width`
  line in that file wins over the file.
- An unknown key or a bad value warns and the engine still boots.
- `ENGINE_GPU_DEBUG=1` is silent; `game.exe --gates` passes.
