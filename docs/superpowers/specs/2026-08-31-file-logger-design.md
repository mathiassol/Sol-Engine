# File logger — a durable copy of the session log

Date: 31 Aug 2026
Status: approved

ENGINE_MAP **Foundation #6** ("File logger (not only stderr)"). Closes audit
findings **S4** and **S5** from
[the 31 Aug audit](../../analysis/2026-08-31-1113-full.md).

## Why

`StdoutLogger` in `packages/core/src/log.cpp` writes to `stderr` and nothing
else. It is the only `ILogger` in the tree, and `set_logger()` is never called
anywhere — the seam exists with zero consumers.

That means a shipped `game.exe` produces no record of anything. Three facts
compound:

- `game.exe` is a console-subsystem binary (`dumpbin`: `3 subsystem (Windows
  CUI)`), so its output lives in a console window that closes with the process.
- `assert_fail` (`core/src/assert.cpp:9`) calls `log(LogLevel::Fatal, msg)`
  **only when `msg` is non-null**. Of the 77 `ENGINE_ASSERT` sites in the tree,
  roughly 40 use the bare form, so they abort with output on `stderr` alone.
- `std::abort()` discards a buffered stream.

So the most common hard failure — a violated precondition in a player's
build — currently leaves nothing behind at all. Every later feature is harder
to diagnose without this one.

This row is also what unblocks **Foundation #7** (minidump), whose map entry
names it directly: "Foundation #6 (somewhere to write the dump)".

## Scope

In: a file sink, its installation, and making the abort path reach it.

Out, deliberately:

- **`%LOCALAPPDATA%`** — that is **Build #7** ("Logs + crash dumps in
  `%LOCALAPPDATA%` (or equivalent)"), a separate Later row that blocks on this
  one. Keeping the location behind one function makes Build #7 a small change.
- **Minidumps** — Foundation #7.
- **Level filtering, per-channel files, async writing.** No demand, and each
  adds a knob that has to be justified. A cvar can add filtering later.

## Approaches considered

**Installed by `Engine::init` via `EngineConfig`.** Fewer app-side changes, but
it loses every line emitted before the engine exists — including which backends
came up — and `main()`'s catch handlers log *after* `Engine` is destroyed, so it
needs a process-lifetime owner anyway. Less control for no gain.

**A new `logging` package with pluggable sinks** (tee, file, console, level
filter). Composable and future-proof, but this is one sink with one consumer.
`core` already owns logging, and `docs/packageRules.md` forbids scaffolding a
package ahead of its implementation.

**Chosen: a sink in `core`, installed by the app.** `core` already owns
`ILogger`. The sink is portable C++ (`<fstream>`, `<filesystem>`, `<ctime>`) so
it needs no platform backend and adds no package edge — `core` stays at layer 0
with no dependencies.

## Component

New files, both in `core`:

- `packages/core/include/engine/core/log_file.hpp`
- `packages/core/src/log_file.cpp`

Two entry points, deliberately separate:

```cpp
// Sink only; the caller owns it. Touches no global state, which is what lets
// the gate exercise it without disturbing the process logger.
// Returns nullptr if `directory` cannot be created or written.
std::unique_ptr<ILogger> create_file_logger(std::string_view directory);

// The same sink, owned for the life of the process and installed via
// set_logger(). Returns false if the directory is not writable, leaving the
// stderr logger in place. A second call is ignored.
bool install_file_logger(std::string_view directory);
```

The process-lifetime owner is not incidental. `main()`
(`packages/sandbox/src/main.cpp:5691`) is the process's one exception boundary
and its `catch` handlers call `engine::log()` **after** `run_app` has returned.
A sink owned by a `run_app` local would be logged through after destruction. A
function-local static mirrors the existing `g_frame_profiler` in
`core/src/profile.cpp`.

## Behaviour

**Location.** `<exe_dir>/logs/`. The executable directory, not the content
root — the content root can resolve to a repo root during development, and a
log that moves depending on how the app was launched is worse to support than
one that does not. `<exe_dir>` is always `build/bin/<config>/` or an install
prefix, both already gitignored, so no `.gitignore` change is needed.

**Rotation.** On open, an existing `log.txt` is renamed to `log.prev.txt`,
replacing any older one. Bounded at two files forever with no pruning logic,
and it survives the case the feature exists for: the game crashes, the player
relaunches, and the crash log is still there as `.prev` rather than overwritten
by the relaunch. A failed rename is ignored — overwriting `log.txt` is still
better than refusing to log.

**Tees to stderr.** Everything that reaches the console today still reaches it;
the file is an added copy, not a replacement. One sink type rather than a
console/file/tee composition, because there is exactly one composition in use.

**Flushes every line.** This is the whole point: a buffered stream loses its
contents on `abort()`. The cost is nil here — logging is startup- and
event-driven, not per-frame, and every repeating warning in the tree is already
latched (`warn_physics_capacity`, the profiler scope table, the frame ring, the
shader descriptor window).

**Format.** A header block at open, then one line per record:

```
=== Sol Engine session log ===
started 2026-08-31 14:22:33

[   0.004][INFO][general] Sandbox starting
[   0.181][INFO][audio] XAudio2 audio ready
```

Wall-clock time appears once, in the header, via `std::time_t` + `std::strftime`
— not `std::format`, to avoid a toolchain dependency for one string. Per-line
timestamps are monotonic seconds from `Clock`, which already exists in `core`.
Elapsed answers "how far did it get before it died", which is what a crash log
is read for; the header answers "when".

**Failure.** If the directory cannot be created or the file cannot be opened,
`create_file_logger` returns `nullptr` and `install_file_logger` returns false
without touching the installed logger. The app logs a warning and continues on
stderr. A game installed under `C:\Program Files` hits exactly this, and
degrading is correct: no log is a worse day, not a dead process. The shader
disk cache already degrades this way (`shader_cache_dxc.cpp`, "Failed to write
shader disk cache").

## Integration

**Install point.** In `run_app`, immediately after `create_platform()` — the
earliest point the executable directory is known — and **before** the start
banner, so the banner is the first line in the file. Skipped when
`gates_mode`.

Only `--set` argument-parse warnings precede the sink. They are
developer-facing, rare, and printed to the console the developer is already
looking at.

**Gates mode writes no file.** `--gates` is a test harness, not a play session.
Two gate runs would push a real crash log out of both `log.txt` and
`log.prev.txt`, and on a development machine gates run constantly — so the
rotation would spend itself recycling gate output instead of preserving the one
thing it exists for.

**`assert_fail`.** Gains an unconditional `log(LogLevel::Fatal, …)` carrying
the expression, file and line, and the message when one was given. The existing
`fprintf(stderr, …)` stays: if the logger is the thing that is broken, the
direct write is the fallback. No explicit flush before `abort()` is needed —
per-line flush already guarantees the record is on disk.

## Gate

`run_file_log_gate()` in `packages/sandbox/src/main.cpp`, using
`create_file_logger` directly so it needs neither the app install path (absent
in gates mode) nor any change to the process logger.

Against a temporary directory it creates and removes:

1. Create a sink, write known lines at several levels and channels, destroy it
   to close the file.
2. `logs/log.txt` exists and contains those lines with the right `[LEVEL]` and
   `[channel]` tags and the header.
3. A second sink rotates the first to `logs/log.prev.txt` with its contents
   intact, and starts a fresh `log.txt`.
4. A directory that cannot be created returns `nullptr` rather than crashing or
   aborting.

Message shape:

```
File log gate: created=yes lines=3 header=yes rotated=yes prev_intact=yes unwritable_rejected=yes (pass)
```

Every field is a value read back off disk, not a restatement of the input.

## Verification

- `sandbox.exe --gates` — 71 gates pass (70 today plus this one).
- `game.exe --gates` — same.
- A normal `sandbox.exe` run produces `build/bin/Debug/logs/log.txt` containing
  the startup lines; a second run moves it to `log.prev.txt`.
- `pwsh -NoProfile -File tools/check-invariants.ps1` — 12 checks, including the
  ROADMAP LOC recount, which this change moves.
- No GPU code changes, so no debug-layer run is required; it is cheap to run
  anyway and should stay silent.

## Do not

- Do not filter by level, add per-channel files, or write asynchronously.
- Do not move the location to `%LOCALAPPDATA%` — that is Build #7, and doing it
  here would pull a Later row forward and put Windows-specific path code into a
  package that is currently platform-free.
- Do not install the sink in gates mode.
- Do not make a failed log directory fatal.
