# Developer Setup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** remove every solvable weakness in developer setup, so that the next
`/analizeMax` finds its bottleneck in Capabilities and nowhere else. Concretely:
disarm the format-on-save trap the audit named and *machine-check* that it stays
disarmed, make `.editorconfig` an enforced contract rather than an aspiration,
give the project one build definition that the CLI, Visual Studio, VS Code and
CI all read, and write down the four things nobody has measured before.

**Why now:** all seven of the audit's D-findings have commits, but a research
pass on 1 Sep 2026 found that the *failure* D1 described is still live. The rest
of this plan comes from the four areas the audit explicitly said it did not
look at.

**Grade mechanics:** exactly one item here can move the number. `G3 · uncovered
failure` caps Developer experience at B+ while a **High** finding has no gate,
test or check. N1 is that finding and Task 3 is that check. Everything else is
the evidence for *Solid*'s own test — "a professional could inherit this and
would not rewrite it; weaknesses are known, bounded, and written down". This
plan does not grade and must not: only a fresh `/analizeMax` can.

**Tech stack:** CMake 4.2.1, Visual Studio 18 2026 (MSVC `/W4 /permissive-`,
C++20), Ninja 1.13.2 (bundled with VS), clang-format 20.1.8 (bundled with VS)
and 22.1.1, Windows PowerShell 5.1 and PowerShell 7.

**Source:** [docs/analysis/metric-devex.md](../../analysis/metric-devex.md) for
the audit's own D1–D7, all closed. The N-findings below are new, from this
research pass, and are numbered separately so they are never confused with the
audit's D-codes.

---

## The findings this plan closes

Every number below was measured on 1 Sep 2026 at commit `c99e2be`, not inferred.
The reproduction script for each is named in its task.

| ID | Finding | Sev | Moves the grade |
|----|---------|-----|-----------------|
| **N1** | The format-on-save trap D1 named is **still armed**. Visual Studio applies `.clang-format` as you type by default whenever the file is present; the file's `DESCRIPTIVE, NOT ENFORCED` header is a comment and no editor reads it. 92 of 123 C++ files diverge, 1,961 sites. | High | **yes** — this is what G3 fires on |
| **N2** | `.editorconfig` declares six checkable properties for source files and nothing checks any of them. The tree already obeys five of the six perfectly; only the 100-column limit is broken, by 97 lines across 27 files, plus 2 markdown files missing a final newline. | Medium | no — but Task 3's check is what clears G3 |
| **N3** | `CMAKE_EXPORT_COMPILE_COMMANDS ON` in `cmake/EngineDefaults.cmake` produces nothing. The Visual Studio generator ignores it; there is no `compile_commands.json` anywhere in `build/`. clangd and every compile-database tool get nothing while the setting implies they work. | Medium | no |
| **N4** | Three different configure commands and no `CMakePresets.json`. README says `-G "Visual Studio 18 2026" -A x64`, CI's build job says `-A x64`, CI's matrix says `-A x64 -D<OPT>=OFF`. Nothing binds them, so they can drift apart silently — and they already differ. | Medium | no |
| **N5** | A clone root longer than ~140 characters fails `cmake` configure with `MSB4018 … exceeds the OS max path limit`, an error that names nothing about this project. Reproduced at a 145-char root; the build generates paths up to 119 chars and try-compile scratch paths exceed that. Nothing documents or checks it. | Medium | no |
| **N6** | **README's PowerShell 5.1 fallback does not work on a default machine.** `powershell -NoProfile -File tools/check-invariants.ps1` fails with `running scripts is disabled on this system` — 5.1's `LocalMachine` policy is `Undefined`, which resolves to `Restricted` on client Windows. Installing PowerShell 7 sets `LocalMachine = RemoteSigned`, which is the only reason the documented `pwsh` command works. So D4's conclusion — "the dependency is avoidable" — is true of the *script* and false of the *invocation*. | Medium | no |
| **N7** | No committed editor configuration, and `.gitignore` makes one impossible: `.vscode/` excludes the directory, and git cannot re-include a file whose parent directory is excluded. | Low | no |
| **N8** | No timings anywhere. Measured cold, at a 42-char root: configure 4.7 s, Debug 68.3 s, Release `game` 74.9 s, no-op rebuild 8.6 s, `build/` 279 MB, working tree 13.6 MB. | Low | no |
| **N9** | `cmake_minimum_required(VERSION 3.24)` against README's "CMake 4.2+". Both are defensible and neither explains the other. | Low | no |

**Considered and deliberately not in this plan.** A `CONTRIBUTING.md` — README,
`CLAUDE.md`, `docs/PICKING.md` and `docs/packageRules.md` already cover
everything one would contain, and a fourth door into the same room is drift
waiting to happen. CI build caching — the build job is 2 m 30 s and the whole
run is under 3 minutes; caching MSVC output would cost more maintenance than it
saves. Running `--gates` in CI — the D3D12 backend rejects software adapters by
design and the CI comment saying so is true; changing that is engine work in the
Stability/Portability dimensions, not developer setup.

---

## Read this before Task 1 — six project rules that will bite you

1. **There is no test framework. A test is a *gate*** — a plain function in
   `packages/sandbox/src/main.cpp` asserting on real values and logging a line
   ending `(pass)` or `(FAIL)`. **This plan adds no gates**: nothing here changes
   runtime behaviour. Its equivalent of red-green is the *invariant* check in
   Task 3, and that one must be watched failing before it is trusted.

2. **Every commit must be green.** `--gates` exits 0 with **74 `(pass)`, 0
   `FAIL`**, and `check-invariants.ps1` passes — 13/13 before Task 3, **14/14**
   from Task 3 onward.

3. **`docs/ROADMAP.md` is an implicit dependency of every source change.** The
   `roadmap-audit` invariant recounts C++/HLSL lines and fails if
   `docs/ROADMAP.md`'s audit line disagrees. **Task 2 adds source lines** — line
   wrapping is not free — so that line must be updated in the same commit. Take
   the number from the failure message; it prints the recount.

4. **Never run `clang-format` over an existing file.** After Task 1 the root
   config makes that a no-op anyway, but the rule stands for
   `tools/house-style.clang-format` too: it is for *new* files only.

5. **`.gitattributes` pins `*.ps1`, `*.bat` and `*.cmd` to CRLF.** Editing
   `tools/check-invariants.ps1` with `sed -i` strips the CR from touched lines
   and the file becomes mixed. Edit it with a tool that preserves line endings,
   or normalise the whole file afterwards.

6. **Trunk-based.** Commit to `main` and push after each task. No branches, no
   worktrees, no PRs.

### Commit tags

Tasks 1–3 close the residue of audit finding **D1**, so their commits carry
`(analizeMax D1)`. Tasks 4–10 come from this research pass rather than from the
audit and take no tag — the `(Category #N)` suffix is only for ENGINE_MAP rows,
and none of this is one.

### The verification block

Run this at the end of every task. It is the definition of green.

```powershell
cmake --build build --config Debug
.\build\bin\Debug\sandbox.exe --gates
$env:ENGINE_GPU_DEBUG=1; .\build\bin\Debug\sandbox.exe --gates; $env:ENGINE_GPU_DEBUG=$null
cmake --build build --config Release --target game
.\build\bin\Release\game.exe --gates
pwsh -NoProfile -File tools/check-invariants.ps1
```

Expected: build clean; **74 `(pass)` lines, 0 `FAIL`** in both configurations;
`D3D12 debug layer: 0 message(s), 0 error(s), 0 warning(s)`; `all 13 checks
passed` (14 from Task 3).

**Use `pwsh`, not `powershell`, unless you add `-ExecutionPolicy Bypass`** — see
N6. A bare `powershell -NoProfile -File` fails on a default machine, and it will
appear to work in any shell that inherited a `Process`-scope bypass, which is
how this went unnoticed.

### The measurement scripts

Three scripts in this session's scratchpad produced every number above and are
the proof harness for Tasks 1–3. Copy the two that survive into `tools/` as part
of Task 3 rather than leaving them in a temp directory; the third is a one-off.

| Script | Measures | Fate |
|--------|----------|------|
| `measure-editorconfig.ps1` | tabs / columns / trailing space / final newline / BOM / line endings across 142 source files, 67 markdown files, 2 shell scripts | becomes the body of invariant #14 |
| `probe-disableformat.ps1` | that `DisableFormat: true` is a hard no-op in clang-format 20.1.8 **and** 22.1.1, with a control proving the probe is otherwise dirty | keep as `tools/probe-formatter.ps1`, run by Task 1's proof |
| `measure-format.ps1` | divergence across seven candidate configs | one-off; its conclusion is recorded in Task 1 |

**A trap the harness itself hit, and the check must not repeat.** A clang-format
config with a duplicate key fails to parse, clang-format then prints nothing,
and a naive "count the violation lines" reader scores that as *perfect
conformance*. The first run of `measure-format.ps1` reported 0/123 files
divergent for exactly this reason. Any check that shells out to clang-format
must validate the config with `--dump-config` and assert a non-zero baseline
before trusting a silent result.

---

## Task 1: Disarm the formatter, and keep a house style for new files

**From:** N1 · **Cost:** ~1 hour · **Moves the grade:** yes, this is the finding

The decision is taken: the root config becomes a verified no-op, and the
descriptive style moves somewhere no editor auto-discovers it. Rejected
alternatives, with reasons, so this is not relitigated:

- *Bulk reformat and enforce clang-format in CI.* Satisfies the audit's upward
  sentence literally, but costs a 1,961-site diff across 92 files, destroys the
  hand-tuned column alignment that is the house style, and contradicts
  `.claude/rules/cpp-conventions.md`. Measured across seven candidate configs:
  no clang-format configuration describes this tree. The best of them still left
  92 of 123 C++ files and 1,945 sites divergent, and
  `AlignConsecutiveShortCaseStatements` — the option that looked most likely to
  rescue the aligned `case` returns — moved only 16 sites.
- *Delete `.clang-format` entirely.* Also disarms the trap and is simpler, but
  it loses the new-file starting point the file exists for, and Visual Studio
  then falls back to its own built-in C++ auto-format-on-typing — a different
  unknown rather than a known no-op.

### Steps

- [ ] **1.1** Create `tools/house-style.clang-format` holding the current
  contents of `.clang-format` verbatim from `BasedOnStyle: LLVM` down, with a
  header saying it is for new files only and is applied explicitly:
  `clang-format --style=file:tools/house-style.clang-format -i <newfile>`.
  Keep the existing measurement paragraph — the `BreakBeforeBinaryOperators` /
  `AlignAfterOpenBracket` reasoning and the rejected
  `SpacesBeforeTrailingComments: 2` — it is the record of why these settings
  and not others.
- [ ] **1.2** Replace `.clang-format` with `DisableFormat: true` and a header
  that explains the mechanism rather than asserting a policy: that Visual Studio
  and the VS Code C/C++ extension both shell out to `clang-format.exe` with the
  discovered config, that `DisableFormat: true` makes that a no-op, that this is
  what actually protects the hand-tuned formatting from a format-on-save, and
  that the house style for new files lives at `tools/house-style.clang-format`.
- [ ] **1.3** Update `.editorconfig`'s comment block. It currently points at
  "the header of `.clang-format`" for the measurement; point it at
  `tools/house-style.clang-format` instead, and state that the root config is a
  deliberate no-op.
- [ ] **1.4** Update `.claude/rules/cpp-conventions.md`'s first bullet the same
  way: the rule is unchanged, the mechanism is new, and the file currently
  describes a config that no longer exists in that form.
- [ ] **1.5** Copy `probe-disableformat.ps1` to `tools/probe-formatter.ps1`,
  pointing it at the repository's own root config rather than a synthetic one,
  and keeping its control case.

### Proof

```powershell
# 1. The root config is a hard no-op over the whole tree, in both installed
#    clang-format versions. Not "reports conformant" — byte-identical output.
pwsh -NoProfile -File tools/probe-formatter.ps1

# 2. The explicit house style still formats a new file.
"int  main( ){int   x=1;return x;}" | Out-File -Encoding utf8 $env:TEMP\new.cpp
& $cf --style=file:tools/house-style.clang-format $env:TEMP\new.cpp
```

Expected: every file byte-identical under the discovered config; the probe's
control case still reported dirty (a silent pass means the config failed to
parse — see the harness trap above); the new file reformatted by the explicit
path. Then the verification block, 13/13.

**Commit:** `fix(tooling): .clang-format is a verified no-op, not a comment (analizeMax D1)`

---

## Task 2: Bring the tree to the column limit it declares

**From:** N2 · **Cost:** 2–4 hours · **Moves the grade:** no, but Task 3 needs it

97 lines exceed the 100-column limit `.editorconfig` and the house style both
declare. Everything else `.editorconfig` claims is already true: across 142
source files there are **zero** tabs, **zero** trailing-whitespace lines,
**zero** CRLF line endings, **zero** BOMs and **zero** missing final newlines.
Two markdown files lack a final newline.

Rejected alternative: *raise the declared limit to 120*. Only 2 of the 97 lines
exceed 120, so this would legalise 95 lines rather than fix them, and it would
move the declared house style away from the one the other 23,950 lines follow.

### Where the work is

| File | Lines over |
|------|-----------|
| `packages/sandbox/src/main.cpp` | 35 |
| `packages/rhi-d3d12/src/device_d3d12.cpp` | 17 |
| `packages/assets/src/cooked.cpp` | 4 |
| `packages/assets/src/pak.cpp` | 4 |
| `packages/physics-cpu/src/physics_cpu.cpp`, `renderer/src/ibl.cpp`, `scene/src/prefab.cpp` | 3 each |
| 9 files | 2 each |
| 11 files | 1 each |

The outlier is `packages/sandbox/src/main.cpp:5523` at **385 characters** — a
single user-facing string literal. Split it with adjacent string-literal
concatenation; do not shorten the message.

### Steps

- [ ] **2.1** Wrap all 95 over-limit C++ lines by hand, matching the break style
  of the lines around each one. Rule 4 applies: no formatter.
- [ ] **2.2** Wrap the 2 over-limit lines in
  `packages/sandbox/content/shaders/taa.hlsl` (94 and 117).
- [ ] **2.3** Add the missing final newline to `Scaffold.md` and
  `docs/superpowers/specs/2026-08-21-karis-taa-design.md`.
- [ ] **2.4** Update `docs/ROADMAP.md`'s audit line — wrapping adds lines and
  `roadmap-audit` will go red otherwise. Take the count from the failure output.

### Proof

`measure-editorconfig.ps1` reports 0 in every category for every set. Then the
verification block: **74 `(pass)`, 0 `FAIL`** in both configurations, and
13/13 — the gate count is unchanged because nothing about behaviour changed, and
if a gate count moves, a wrap broke something.

**Commit:** `style: 97 lines to the 100-column limit the project declares (analizeMax D1)`

---

## Task 3: `format-hygiene`, invariant #14 — the check that clears G3

**From:** N1, N2 · **Cost:** 2–3 hours · **Moves the grade:** yes, indirectly

This is the red-green of the plan. `.editorconfig` becomes a contract the tree
is held to on every push, and the root `.clang-format` cannot silently stop
being a no-op. Godot and bgfx both enforce formatting in CI; after this, so does
Sol — over the rules that are actually true here, rather than over a config that
cannot describe the tree.

### Steps

- [ ] **3.1** Add a `# ── 14. …` section to `tools/check-invariants.ps1`,
  after `conditional-target-links`, reporting through `Add-Result` like the
  other thirteen. It checks, over `packages/**` excluding `third_party`:
  - no tab characters in `*.cpp`, `*.hpp`, `*.h`, `*.hlsl`, `*.hlsli`
  - no line over 100 columns in those files
  - no trailing whitespace in those files
  - a final newline on every one of them, and on every tracked `*.md` outside
    `build/`, `reasarch/` and `third_party/`
  - no UTF-8 BOM anywhere
  - no CRLF in LF-pinned files, and **only** CRLF in `*.ps1`/`*.bat`/`*.cmd`
  - the root `.clang-format` still contains `DisableFormat: true`

  Report each violation as `path:line`, the way `header-layout` and `no-add-pass`
  already do. Markdown keeps its trailing whitespace — `.editorconfig` exempts it
  because two trailing spaces are a hard line break — so the trailing-whitespace
  rule must not run over `*.md`.
- [ ] **3.2** **Control-test every rule.** Introduce one violation of each of
  the seven, one at a time, confirm the check fails and names the right
  `path:line`, and revert. A check nobody has watched fail is a check nobody
  should trust — and the false-clean trap above is exactly how this class of
  check lies.
- [ ] **3.3** Confirm it runs identically under PowerShell 7 and under Windows
  PowerShell 5.1 — the latter as `powershell -NoProfile -ExecutionPolicy Bypass
  -File …`, see N6. It must not use `??`, `?:`, or any 7-only syntax.
- [ ] **3.4** Update `CLAUDE.md:107` — "Thirteen checks:" and its enumeration
  become fourteen, with `format-hygiene` named and described in one clause.
  This is the exact drift finding T1 was about; do not skip it.

### Proof

`all 14 checks passed`, under both shells. Each of the seven rules observed
failing on an injected violation and passing after the revert. CI green — the
`invariants` job picks the new check up with no workflow change.

**Commit:** `feat(tooling): format-hygiene invariant — .editorconfig is enforced now (analizeMax D1)`

---

## Task 4: `CMakePresets.json`, and a Ninja preset that emits a compile database

**From:** N3, N4 · **Cost:** 3–5 hours · **Moves the grade:** no

One file that CMake 4.2, Visual Studio, VS Code and CI all read, replacing three
hand-spelled configure commands that already differ from each other. The
decision is taken: the VS generator stays the documented default and Ninja is a
second, secondary preset.

**Measured on the real tree before planning this**, so none of it is speculative:
Ninja Multi-Config configures and builds Debug in **25.8 s** against the VS
generator's 68.3 s, emits a `compile_commands.json` with **147 entries**, cooks
`content.pak`, copies content and the DXC runtime next to both binaries, and the
resulting `sandbox.exe --gates` reports **74 `(pass)`, 0 `FAIL`, exit 0**.

**The one caveat, which must be documented and not discovered:** the Ninja preset
needs the MSVC environment. From a plain PowerShell it fails to find `cl.exe`.
Visual Studio and VS Code set that up when they open the folder; from a terminal
it needs a Developer Command Prompt or `vcvars64.bat`. The VS-generator preset
has no such requirement, which is why it stays the default.

### Steps

- [ ] **4.1** Write `CMakePresets.json` at the repo root, schema version 3 or
  later (CMake 4.2 supports well past it; version 3 is the widest-compatible
  floor that has what this needs). Configure presets:
  - `vs2026` — `Visual Studio 18 2026`, `x64`, binary dir `build`. The default,
    and byte-identical in effect to the README's current command.
  - `ninja` — `Ninja Multi-Config`, binary dir `build-ninja`, with
    `CMAKE_MAKE_PROGRAM` left unset so an IDE or devcmd supplies it. Describe it
    as "IDE or Developer Command Prompt only" in its `description`.
  - `no-d3d12`, `no-win32`, `no-sandbox`, `no-game` — inheriting `vs2026`, each
    setting its one `ENGINE_*` option `OFF`, binary dir `build-off`. These are
    CI's matrix, stated once.

  Build presets: `debug` (configure `vs2026`, config Debug), `release-game`
  (config Release, target `game`), and the same two for `ninja`.
- [ ] **4.2** `cmake/EngineDefaults.cmake`: keep `CMAKE_EXPORT_COMPILE_COMMANDS
  ON` and add a comment saying the Visual Studio generator ignores it and the
  `ninja` preset is what produces `compile_commands.json`. The setting is now
  true for one of the two presets instead of neither.
- [ ] **4.3** `.gitignore`: `build/` becomes `build*/` so `build-ninja/` and
  `build-off/` are covered. Check nothing named `build*` is tracked first
  (`git ls-files | grep '^build'`).
- [ ] **4.4** README: give the preset commands as the documented path with the
  raw `cmake -B build -G …` retained beside them for anyone without presets,
  and add a short paragraph on the Ninja preset — what it buys (compile database
  for clangd, ~2.6× faster full build) and its devcmd requirement.
- [ ] **4.5** `.github/workflows/ci.yml`: switch the `build` job and the
  `configure-options` matrix to `--preset`. **Keep the existing comment** about
  deliberately not pinning `-G` on the runner, and reconcile it: the `vs2026`
  preset *does* pin a generator, so either add a `ci` preset that inherits
  `vs2026` without the generator field, or keep the build job on the raw command
  and use presets only for the matrix. Decide from what the runner image
  actually has — do not guess, and do not silently drop a true comment.

### Proof

```powershell
cmake --list-presets
cmake --preset vs2026 && cmake --build --preset debug
cmake --preset ninja   # from a Developer Command Prompt
```

Expected: both presets configure; `build-ninja/compile_commands.json` exists
with ~147 entries; `build-ninja/bin/Debug/sandbox.exe --gates` reports 74
`(pass)`, 0 `FAIL`, exit 0; the VS-generator path is unchanged and the
verification block still passes 14/14; CI green on all six jobs.

**Commit:** `build: CMakePresets — one build definition for the CLI, both IDEs and CI`

---

## Task 5: Guard the path-length cliff

**From:** N5 · **Cost:** ~1 hour · **Moves the grade:** no

Reproduced this pass: configure from a 145-character clone root dies inside
CMake's try-compile with `MSB4018 … The item metadata "%(FullPath)" cannot be
applied … exceeds the OS max path limit`, naming an MSBuild internal and nothing
about Sol Engine. At a 42-character root the same command takes 4.7 s. The build
generates relative paths up to 119 characters, and try-compile scratch paths run
longer still.

### Steps

- [ ] **5.1** In `CMakeLists.txt`, after `project()`, emit a
  `message(WARNING)` when `WIN32` and the source directory path exceeds 140
  characters, naming the measured limit, the actual length, and the two fixes
  (clone somewhere shorter, or enable `LongPathsEnabled`). A warning, not a
  `FATAL_ERROR` — a machine with long paths enabled configures fine and must not
  be blocked.
- [ ] **5.2** README, under Requirements: one line giving the number and the
  reason.

### Proof

Configure from a root over 140 characters and see the warning by name; configure
from the normal root and see no warning. Verification block, 14/14.

**Commit:** `build: warn before MAX_PATH bites, with the measured limit`

---

## Task 6: Reconcile the two CMake version numbers

**From:** N9 · **Cost:** 15 minutes · **Moves the grade:** no

`cmake_minimum_required(VERSION 3.24)` and README's "CMake 4.2+" are both
correct about different things — 3.24 is what the CMake code needs, 4.2 is what
the documented generator needs — and nothing says so.

- [ ] **6.1** Add a one-line comment above `cmake_minimum_required` stating both
  facts and which is which.
- [ ] **6.2** README's CMake bullet already explains the 4.2 half; extend it to
  say the project's own floor is 3.24, so another generator works on older CMake.

**Proof:** `doc-links` still resolves; 14/14.

**Commit:** `docs: say why cmake_minimum_required is 3.24 and the README says 4.2`

---

## Task 7: Make the PowerShell fallback actually work

**From:** N6 · **Cost:** ~1 hour · **Moves the grade:** no

This task started life as "drop the PowerShell 7 prerequisite, 5.1 is faster
anyway" — 5.1 runs the invariants in **3.2 s** against pwsh 7's **9.1 s**, both
all-checks-pass. Measuring it properly killed that idea, and the reason is worth
stating because it is exactly the class of thing the audit's missing
clean-machine pass would have caught.

Reproduced this pass, from a shell with no inherited `Process`-scope bypass:

```
> powershell -NoProfile -Command "Get-ExecutionPolicy"
Restricted
> powershell -NoProfile -File tools/check-invariants.ps1
... cannot be loaded because running scripts is disabled on this system.
> pwsh -NoProfile -Command "Get-ExecutionPolicy"
RemoteSigned
> pwsh -NoProfile -File tools/check-invariants.ps1
all 13 checks passed
```

Windows PowerShell 5.1 has `LocalMachine = Undefined`, which resolves to
`Restricted` on client Windows. **Installing PowerShell 7 is what sets
`RemoteSigned`** — so the prerequisite is not redundant, it is the thing making
the documented command work. The README's fallback sentence is false as written,
and 5.1 needs `-ExecutionPolicy Bypass` to run a local unsigned script.

**This also means CI cannot catch it.** GitHub's `windows-latest` runner sets a
permissive policy for both shells, so a 5.1 job would pass while a contributor's
machine still failed. A CI job here checks *script compatibility*, not the claim
that bit. Say so rather than implying the job covers it.

### Steps

- [ ] **7.1** README: keep PowerShell 7 in Requirements and correct *why* it is
  there — not because the script needs 7, but because installing it sets an
  execution policy that lets a local script run at all. Correct the fallback
  sentence to `powershell -NoProfile -ExecutionPolicy Bypass -File
  tools/check-invariants.ps1`, and give both timings so the trade is visible.
- [ ] **7.2** `CLAUDE.md`: same correction in the Invariant checks block.
- [ ] **7.3** `.github/workflows/ci.yml`: add a Windows PowerShell 5.1 run to
  the `invariants` job so 5.1 *compatibility* stays true as the script grows —
  it costs ~15 s. Comment it with what it does and does not cover, in the style
  of the existing CI comments.

### Proof

`pwsh -NoProfile -File tools/check-invariants.ps1` → `all 14 checks passed`.
`powershell -NoProfile -ExecutionPolicy Bypass -File …` → the same. A bare
`powershell -NoProfile -File …` from a clean shell still fails, and the README
now says it will. CI shows both invariant runs green.

**Commit:** `docs: the 5.1 fallback needs -ExecutionPolicy Bypass, and says so`

---

## Task 8: A committed editor configuration

**From:** N7 · **Cost:** 1–2 hours · **Moves the grade:** no

Task 1 disarms the formatter at the clang-format layer, which is the layer that
actually bites. This adds the belt to that pair of braces, and gives a
contributor a working debugger on first open.

**The `.gitignore` mechanic that makes this non-obvious:** git cannot re-include
a file whose parent directory is excluded. `.vscode/` excludes the directory, so
`!.vscode/settings.json` does nothing. The pattern must become `.vscode/*` with
negations beside it.

### Steps

- [ ] **8.1** `.gitignore`: `.vscode/` → `.vscode/*`, then
  `!.vscode/settings.json`, `!.vscode/extensions.json`, `!.vscode/launch.json`.
  Leave `.idea/` and `.cursor/` alone. Keep the surrounding comment accurate.
- [ ] **8.2** `.vscode/settings.json` — format-on-save off for C++, the C/C++
  extension's formatting disabled, and clangd pointed at
  `build-ninja/compile_commands.json`. Comment each setting with *why*, the way
  the rest of this repo's config files do.
- [ ] **8.3** `.vscode/extensions.json` — recommend the C/C++ and CMake Tools
  extensions, nothing more.
- [ ] **8.4** `.vscode/launch.json` — two configurations: run `sandbox.exe`, and
  run it with `--gates`. Both against `build/bin/Debug` so they work with the
  default preset.

### Proof

`git check-ignore -v .vscode/settings.json` reports no match; the three files
appear in `git status` as untracked before adding and are tracked after; `.vscode/`
otherwise stays ignored (drop a scratch file in it and confirm it is invisible).
14/14.

**Commit:** `build: ship the three .vscode files, and make .gitignore allow them`

---

## Task 9: Write down what is now measured

**From:** N8 · **Cost:** 30 minutes · **Moves the grade:** no

The audit could say the documented path works but not how long it takes, which
is most of how setup actually feels.

- [ ] **9.1** README, after the build commands: the cold numbers from a
  42-character root — configure 4.7 s, Debug 68.3 s, Release `game` 74.9 s,
  no-op rebuild 8.6 s, `build/` 279 MB after both configurations, working tree
  13.6 MB. Say what they were measured on and when, so a later reader can tell a
  stale number from a slow machine.
- [ ] **9.2** Add the Ninja comparison — 25.8 s for the same cold Debug build —
  beside the Ninja preset paragraph from Task 4.

**Proof:** `doc-links` resolves; 14/14.

**Commit:** `docs: what a cold clone-to-running actually costs, measured`

---

## Task 10: Record the decisions where the project keeps them

**From:** all of it · **Cost:** 30 minutes · **Moves the grade:** no

- [ ] **10.1** Add a `docs/ROADMAP.md` decision-log entry in the house
  Why / Choice / Gate / Do-not shape, covering the formatter settlement (why
  `DisableFormat` rather than a bulk reformat, and the seven-config measurement
  that decided it) and the preset split (why the VS generator stays default).
  The **Do-not** lines matter most: do not bulk-reformat, do not make the Ninja
  preset the documented default, do not add clang-format to CI.
- [ ] **10.2** Do **not** hand-edit `docs/analysis/metric-devex.md`. It is a
  derived document owned by `/analizeMax-metric`, which derives purely from the
  audit; editing it in place would make it disagree with its own frontmatter.
  The right closing move is a fresh `/analizeMax`, which is a separate, expensive
  run and explicitly out of this plan's scope.

**Commit:** `docs(roadmap): the formatter settlement and the preset split`

---

## Definition of done

- [ ] `.clang-format` is a verified byte-for-byte no-op in clang-format 20.1.8
      and 22.1.1, and `tools/house-style.clang-format` still formats a new file
- [ ] 0 tabs, 0 over-limit lines, 0 trailing whitespace, 0 missing final
      newlines, 0 BOMs, correct line endings — across all 142 source files, 67
      markdown files and 2 shell scripts
- [ ] `all 14 checks passed`, under PowerShell 7 **and** under Windows
      PowerShell 5.1 with `-ExecutionPolicy Bypass`
- [ ] each of `format-hygiene`'s seven rules has been observed failing on an
      injected violation
- [ ] `cmake --list-presets` shows six configure presets; both build paths
      produce a `sandbox.exe` whose `--gates` reports **74 `(pass)`, 0 `FAIL`**
- [ ] `build-ninja/compile_commands.json` exists with ~147 entries
- [ ] a configure from a >140-character root warns by name
- [ ] the three `.vscode` files are tracked and the rest of `.vscode/` is not
- [ ] README names no prerequisite the project does not need, and CI proves the
      one shell claim it makes
- [ ] CI green on every job
- [ ] `docs/ROADMAP.md` carries the decision entry, and its audit line matches
      the recount

**What this plan does not claim.** It removes every ceiling and every weakness
found in developer setup by two passes, and it makes the one High finding
machine-checked. Whether that lands the dimension in *Solid* — or higher, on the
strength of an invariant suite that validates project documentation as a graph,
which the audit already said neither Godot nor bgfx does — is a question for a
fresh `/analizeMax`. This plan quotes grades and does not award them.
