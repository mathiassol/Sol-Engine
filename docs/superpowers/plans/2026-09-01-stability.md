# Stability Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** close the three gaps the 31 Aug audit named as the places Sol is
behind *every* reference engine — no sanitizer job, no fuzzing, no crash
reporter — so a fresh `/analizeMax` cannot write that sentence again.

**Run [the architecture plan](2026-09-01-architecture.md) first.** Task 1 here
needs the gate registry its Tasks 1–3 build. Written the other way round, the
CPU/GPU split gets hand-maintained twice and drifts.

---

## Where this dimension actually stands

The audit graded Stability **B** (Solid), no ceilings fired, on four findings.
**All four are closed**, verified in the tree today:

| ID | Finding | Sev | State |
|----|---------|-----|-------|
| **S1** | Four content parsers fail with no diagnostic at all | Medium | **closed** — every one now has more `reject`/`log` calls than bare `return false`s |
| **S2** | `scene::add_instance` aborts on overflow, and it is a public API | Medium | **closed** — returns `kInvalidInstance`/`kInvalidMaterial` with a latched warning |
| **S3** | One `&&` lets a failing gate hide the next | Medium | **closed** — two `if` blocks, matching the other ~25 sites |
| **S4** | `add_pass` does not validate the ref counts it will iterate | Low | **closed** — clamps and logs, naming the pass |

S1 landed *before* C1 made the parsers reachable from disk, which is the right
order: the audit warned S1 "becomes High the day a scene file is loaded from
disk", and by then it was fixed.

So what holds this dimension is not a finding. It is one paragraph:

> **Where Sol is behind every reference:** there is no sanitizer job, no
> fuzzing, and no crash reporter. Unity, Unreal and Godot all run ASan/UBSan or
> equivalent in CI. 71 hand-written assertions on one developer's GPU is a
> narrower net than any of them, however good the assertions are.

That is the whole plan. The Linux CI job added today makes the first two
affordable for the first time, and the third is Foundation #7, already **Ready**.

**This plan does not grade.** Only a fresh `/analizeMax` can.

---

## What was measured before this plan was written

**The gates are more CPU-bound than they look.** Of 72 gate functions, **23 take
no arguments and touch no device** — `run_arena_gate`, `run_math_guard_gate`,
`run_scene_file_gate`, `run_scene_prefab_gate`, `run_pak_gate`, `run_cook_gate`,
`run_pbr_gate`, `run_pcf_gate`, `run_graph_gate`, the three glTF gates and the
rest. Another ~17 take arguments that are themselves CPU-only — `IPhysics*`
(six gates), `const World&`, `IAssetLoader&`, `IFileSystem*`, `MeshData`. Only
two of the no-argument gates build a device internally (`run_aspect_gate`,
`run_swap_gate`).

So roughly **40 of 72 gates could run with no GPU at all** — and every package
they exercise (`core`, `math`, `scene`, `physics-cpu`, `assets`,
`assets-filesystem`, `assets-obj`, `assets-gltf`, `renderer`'s CPU maths,
`gameplay`) is already added unconditionally and already compiles on Linux.

**Nothing runs on Linux today.** The job added in `005b7eb` compiles and links
`sandbox` and `game`, then stops, because `run_app` needs a platform and an RHI.
A sanitizer job over code that never executes finds nothing. That is the gap
Task 1 closes, and it is why Task 1 comes first.

**The parsers are now worth fuzzing, and were not before.** They are reachable
from disk (C1) and they diagnose (S1). The audit noted the remaining hole
precisely:

> *No check covers this.* `Scene file gate: … reject=yes` proves rejection
> happens; nothing proves a diagnostic is produced.

Task 3 is that check, and it is the one that makes S1's fix permanent rather
than true-on-the-day.

---

## Read this before Task 1 — the rules that will bite you

1. **A test is a gate**, and three of the four tasks here add one. Write it,
   run it, **watch it fail**, then implement.
2. **Every commit is green.** After the architecture plan the counts are
   **76 `(pass)`, 0 `FAIL`** and `all 16 checks passed`. Each task here moves
   one or both; the task says which.
3. **`docs/ROADMAP.md` is an implicit dependency of every source change.**
4. **`format-hygiene` (invariant 14)** fails a push over 100 columns, a tab, a
   missing final newline, or a `.ps1` that is not CRLF.
5. **A sanitizer finding is a bug, not a warning to suppress.** This project
   treats a D3D12 debug-layer message as build-breaking; the same rule applies
   here. Suppression files are for third-party code, and this tree vendors
   exactly one file.
6. **Trunk-based.** Commit to `main`, push after each task.

### The verification block

```powershell
cmake --build --preset debug
.\build\bin\Debug\sandbox.exe --gates
$env:ENGINE_GPU_DEBUG=1; .\build\bin\Debug\sandbox.exe --gates; $env:ENGINE_GPU_DEBUG=$null
cmake --build --preset release-game
.\build\bin\Release\game.exe --gates
pwsh -NoProfile -File tools/check-invariants.ps1
```

---

## Task 1: A headless gate run

**Cost:** 2–3 hours · **Needs:** the architecture plan's `kGates` registry

- [ ] **1.1** Add `--gates-cpu`, handled in `run_app` **before** `create_platform()`
  — that branch is where the non-Windows build currently logs Fatal and returns
  1, so the headless path has to come first or Linux never reaches it.
- [ ] **1.2** Build a `GateContext` with only what exists without a GPU: the
  filesystem asset loader, physics, the demo `World`, the content layout, a
  scratch directory. Everything else stays null, which the context already
  documents as legal.
- [ ] **1.3** Run every `kGates` entry marked `Cpu`, in registry order, with the
  same `if (!x) { ok = false; }` shape. Exit 0/1. Log the count that ran, so a
  silently-empty run is visible rather than a green no-op — that failure mode is
  exactly how a sanitizer job comes back clean while testing nothing.
- [ ] **1.4** Classify all 72 registry entries `Cpu` or `Gpu`. When in doubt,
  `Gpu` — a misclassified gate crashes the headless run on a null device, which
  is loud, but the reverse is a gate quietly not running anywhere.
- [ ] **1.5** Add a gate asserting the headless count equals the number of `Cpu`
  entries in `kGates`, so the two cannot drift.

**Proof:** `sandbox.exe --gates-cpu` on Windows reports the same pass count as
the `Cpu` subset of a full run, exit 0. The full `--gates` run is unchanged at
76. On Linux — build with the existing job's configure line and run it there;
this is the first time anything in this repository has executed off Windows.

**Commit:** `feat(sandbox): --gates-cpu runs every device-free gate headless`

---

## Task 2: ASan and UBSan over that run, in CI

**Cost:** 1 day, and **the iteration count is genuinely unknown** — this is the
first time this code has been instrumented, and the honest expectation is that
UBSan finds something in the parsers or the maths

- [ ] **2.1** Add a CMake option `ENGINE_SANITIZE` (`off` | `address` |
  `undefined` | `address,undefined`), GCC/Clang only, adding
  `-fsanitize=… -fno-omit-frame-pointer -g`. Do not wire MSVC's
  `/fsanitize=address` in the same task: it is incompatible with incremental
  linking and needs every module instrumented, and mixing that question into
  this one is how neither gets finished.
- [ ] **2.2** Add a `linux-sanitize` CI job: configure with
  `-DENGINE_RHI_D3D12=OFF -DENGINE_PLATFORM_WIN32=OFF -DENGINE_SANITIZE=address,undefined`,
  build, run `--gates-cpu`. Set `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`
  and `ASAN_OPTIONS=detect_leaks=1` — a sanitizer that reports and continues
  turns a red build green, which is worse than not running it.
- [ ] **2.3** Fix what it reports, one finding at a time, each with its own
  commit naming what was undefined and why. Expect signed overflow, unaligned
  loads in the binary parsers, and float-to-int conversions.
- [ ] **2.4** If a finding is genuinely not a bug, suppress it **by name with a
  written reason** in a committed suppression file — never by dropping the
  check. Expect to need none.
- [ ] **2.5** Comment the job with what it covers (the ~40 CPU gates and every
  package they touch) and what it does not (nothing GPU-side; `rhi-d3d12`,
  `shaders-dxc`, `platform-win32`, `audio-xaudio2` and `assets-png-wic` are not
  even compiled there). Overstating coverage is worse than having none.

**Proof:** the job green on a real run, not locally simulated. Then the control
that matters: introduce a deliberate heap-buffer-overflow in a CPU gate, confirm
the job goes red naming the file and line, and revert. A sanitizer job nobody
has watched fail is indistinguishable from one that is not instrumented.

**Commit:** `ci: ASan and UBSan over the headless gates`

---

## Task 3: Fuzz the four parsers, deterministically

**Cost:** half a day

`cooked.cpp`, `scene_file.cpp`, `pak.cpp` and `prefab.cpp` take untrusted bytes,
are reachable from disk, and now diagnose. Nothing proves the diagnostics
actually fire, and nothing has ever fed them a malformed file they were not
written to expect.

A seeded mutation gate rather than libFuzzer: it needs no framework, it runs
everywhere the engine runs, it is deterministic so a failure reproduces from its
seed, and — because it is CPU-only — Task 2 runs the whole thing under ASan for
free. That last point is what makes this cheap and worth doing now.

- [ ] **3.1** Add `run_parser_fuzz_gate`: for each of the four formats, start
  from a valid buffer the gates already build, then apply N seeded mutations —
  single-byte flips, truncation at a random offset, length-field corruption,
  and count-field inflation (the case that reaches the bound checks S2 added).
- [ ] **3.2** Assert three things per iteration: the parser returns rather than
  crashing or hanging; a rejected input produces **at least one log line**; and
  an accepted mutant round-trips to a structurally valid object. The middle one
  is the audit's uncovered hole, stated as an assertion.
- [ ] **3.3** Fixed seed and fixed iteration count in the gate so `--gates` stays
  deterministic and fast. Take both from cvars (`gate.fuzz_seed`,
  `gate.fuzz_iterations`) so a longer soak is a command-line argument rather
  than a rebuild.
- [ ] **3.4** **Watch it fail first** — run it before wiring the log assertion,
  against a parser path deliberately reverted to a bare `return false`, and
  confirm it reports which format and which seed.
- [ ] **3.5** Whatever it finds is a separate commit per fix, each naming the
  seed that reproduces it.

**Proof:** 77 `(pass)`, 0 `FAIL`, Debug and Release; the gate names the format,
seed and iteration count in its message. Green under the Task 2 sanitizer job,
which is where a real memory bug would surface.

**Commit:** `feat(sandbox): a deterministic mutation-fuzz gate over the four parsers`

---

## Task 4: A crash reporter (Foundation #7)

**Cost:** half a day · **Closes an ENGINE_MAP row**

The third named gap. Foundation #6 shipped the file logger and the reasoning
that goes with it — flush every line, because "the record worth keeping is the
one written immediately before `abort()`". A minidump is the other half: the log
says how far it got, the dump says what the stack was.

- [ ] **4.1** Add `write_minidump(path)` to `platform-win32` —
  `MiniDumpWriteDump` from `dbghelp`, `MiniDumpWithIndirectlyReferencedMemory`
  plus thread info, which is small and enough to read a stack. It goes in the
  backend package, not `core`: it is a Windows API, and
  `graphics-api-isolation`'s sibling rule keeps platform APIs inside their own
  package.
- [ ] **4.2** Install a `SetUnhandledExceptionFilter` at startup, next to
  `install_file_logger` and for the same reason — earliest point the executable
  directory is known. Write beside the log, rotate the same way, and log the
  dump's path before writing so the log names it even if the write fails.
- [ ] **4.3** **Not under `--gates`**, matching the file logger's decision: two
  gate runs must not push a real crash dump out of the rotation.
- [ ] **4.4** Also hook `assert_fail`. `ENGINE_ASSERT` has no `NDEBUG` guard and
  76 assert sites are live in Release, so an assert is the most likely way this
  process dies — a dump there is worth more than one from an access violation.
- [ ] **4.5** **Write the gate first.** `run_minidump_gate` calls the writer
  directly against a temp directory — no real crash needed, `MiniDumpWriteDump`
  captures a running process — and asserts the file exists, is non-trivial in
  size, and starts with the `MDMP` magic. Same shape as `run_file_log_gate`
  driving `create_file_logger` against a temp dir.
- [ ] **4.6** Flip Foundation #7 to Done, recount the category subtotal and the
  header totals, and update Build #7's *Finish first*, which names this row.
- [ ] **4.7** `docs/GPU_BASELINE.md` and README: where a dump lands and what to
  send with a bug report.

**Proof:** 78 `(pass)`, 0 `FAIL`; the gate watched failing before the writer
exists. Then the end-to-end check the gate cannot do: run the sandbox with a
deliberately failing `ENGINE_ASSERT`, confirm a `.dmp` appears beside the log
and opens in Visual Studio with a readable stack. Revert.

**Commit:** `feat(platform): minidump on an unhandled exception or a failed assert (Foundation #7)`

---

## Task 5: Record it

**Cost:** 30 minutes

- [ ] **5.1** A `docs/ROADMAP.md` Why / Choice / Gate / Do-not entry covering
  the headless run, the sanitizer job, the fuzz gate and the minidump. **Do-not**
  lines: do not suppress a sanitizer finding without a written reason; do not
  let the fuzz gate become non-deterministic; do not read the Linux sanitizer job
  as covering GPU code; do not install the crash handler under `--gates`.
- [ ] **5.2** Add a Later row for **MSVC `/fsanitize=address`** over the full
  Windows gate run, with *Finish first* naming what makes it hard — incremental
  linking, and every module needing instrumentation. Deliberately left out of
  Task 2, and worth recording so it is not rediscovered as a surprise.
- [ ] **5.3** Add a Later row for **symbol archiving** (Build #18) if the
  minidump work made it more concrete — a dump is only readable against the PDBs
  of the build that produced it, and the release workflow currently ships
  neither.

**Commit:** `docs(roadmap): sanitizers, fuzzing, and a crash reporter`

---

## Definition of done

- [ ] `--gates-cpu` runs on **Linux**, reporting a non-zero count that matches
      the `Cpu` entries in `kGates`
- [ ] a `linux-sanitize` CI job runs those gates under ASan + UBSan with
      `halt_on_error=1`, and has been **watched failing** on an injected overflow
- [ ] every sanitizer finding is fixed, or suppressed by name with a written
      reason (expected: none suppressed)
- [ ] the fuzz gate asserts that every rejection logs — the hole the audit named
      as uncovered — and names format, seed and iteration count in its message
- [ ] a minidump lands beside the log on an unhandled exception **and** on a
      failed assert, verified once end to end outside the gate
- [ ] Foundation #7 is Done, with the map's totals and subtotals recounted
- [ ] **78 `(pass)`, 0 `FAIL`** in Debug and Release; debug layer silent
- [ ] `all 16 checks passed` under both shells; CI green on every job

## What this plan does not do

No MSVC ASan over the Windows GPU run (Task 5.2 records it). No libFuzzer or
OSS-Fuzz — a seeded mutation gate that runs everywhere beats a framework that
runs in one place, at this size. No crash *reporting* in the network sense:
Task 4 writes a dump to disk, and uploading it is a product decision this engine
has no business making yet. No symbol archiving (Build #18, **Later**), which is
what would make a shipped dump readable — worth knowing before treating the
minidump as a finished story.
