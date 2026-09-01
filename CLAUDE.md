# Sol Engine — Claude Code guide

General-purpose C++20 game engine (Unity/Godot/Unreal category), Windows/D3D12
today. Full design principles: [Philosophy.md](Philosophy.md) and
[Scaffold.md](Scaffold.md). Read those before making architectural calls this
file doesn't cover.

## Start here

- **What to work on**: [docs/ENGINE_MAP.md](docs/ENGINE_MAP.md) — pick one
  **Ready** row. Picking rules: [docs/PICKING.md](docs/PICKING.md).
  Not sure what to pick? `/whatnow` reads the tree, the gates, CI, the
  backlog's dependency graph and the newest audit, and offers three
  directions with the evidence for each. It suggests; it does not start.
- **Canonical plan / changelog**: [docs/ROADMAP.md](docs/ROADMAP.md) — every
  shipped feature has a dated Why/Choice/Gate/Do-not entry there.
- These two files are the source of truth. There is no separate dashboard —
  read them directly.

## The loop, per Ready row

1. Pick **one** Ready row. Don't start a Later or Far row (see ENGINE_MAP.md's
   Status table for what that means).
2. Brainstorm and write a design spec with the `superpowers:brainstorming`
   skill → `docs/superpowers/specs/YYYY-MM-DD-<topic>-design.md`.
3. Plan with `superpowers:writing-plans`, then implement.
4. Gate it: `sandbox --gates` must pass. If GPU code changed, also run with
   `ENGINE_GPU_DEBUG=1` and confirm the debug layer is silent.
5. Run `/ship-feature` to close it out (updates the map/roadmap, commits,
   pushes).

`/roadmap <category> #<row>` does all five in one unbroken run: deep research
(codebase **and** web), one mandatory question round that ends by asking
whether to execute directly, across subagents, or as a workflow, then build,
gate, `/ship-feature`, and refresh the published roadmap page. The question
round is its only interruption.

Separately, `/analizeMax` audits the whole engine — code and non-code — and
grades six dimensions against an absolute standard. It is expensive and
deliberate: run it to get the real picture, not during feature work. Output
lands in [docs/analysis/](docs/analysis/README.md), including a phased plan
that `/analizeMax-execute` applies — normally, across subagents, or as a
workflow. Only fixes that need no approval reach that plan; judgement calls
stay on a separate list.

By default it publishes only the report and the scorecard; `/analizeMax core`
adds the stability, architecture and capability reports, `/analizeMax max` adds
all six, and naming metrics adds just those.

`/analizeMax-metric <name>` expands one graded dimension into a 1-2 page
report plus a full ordered action list. It derives purely from the newest full
report — no build, no research, no re-grading — so it is cheap to re-run.

All of it publishes to permanent artifact URLs tracked in
`docs/analysis/artifacts.json`: the scorecard is a hub linking to a page per
metric, each linking back, so sharing the scorecard shares the whole set.
`/analizeMax-repair` creates anything missing and fixes the links.

Reports use two code systems: a dimension letter plus a number is a **finding**
(`D3` = developer setup, third finding), and `G1`–`G6` are **ceilings** that cap
a grade. Both are tabulated in [docs/analysis/README.md](docs/analysis/README.md),
which is also where a `(analizeMax D3)` commit tag can be looked up.

## Non-negotiables

These outrank convenience every time, regardless of what a session's context
window still holds:

- Renderer never includes a graphics-API header (`d3d12.h` or equivalent) —
  only `rhi`. A new pass is registered with `add_pass` in
  `packages/renderer/src/standard_frame.cpp`, never from the sandbox — but the
  pass's shader and pipeline *are* app-owned, so a working pass spans eight
  files — four structs to plumb through, plus the shader, the pass
  registration, the recorder, and the gate. Full checklist:
  [.claude/rules/renderer-boundaries.md](.claude/rules/renderer-boundaries.md).
- Dependencies only point downward. No circular dependencies.
- Engine ≠ editor. No inspector, hierarchy, or content-browser UI inside any
  engine package or the sandbox. An editor, if it ever exists, is a separate
  executable (see ENGINE_MAP.md category 17).
- One feature, one gate, one package. Don't scaffold an empty package with no
  implementation (see [docs/packageRules.md](docs/packageRules.md)).
- Don't skip a Ready row to add an unrelated graphics-paper feature.

Language-level conventions (namespaces, ownership, header layout):
[.claude/rules/cpp-conventions.md](.claude/rules/cpp-conventions.md).

Full lists: [docs/packageRules.md](docs/packageRules.md),
[docs/ENGINE_MAP.md](docs/ENGINE_MAP.md)'s "Cross-cutting rules" section.

## Build & gate

```powershell
cmake -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Debug
.\build\bin\Debug\sandbox.exe --gates
```

Optional: `ENGINE_GPU_DEBUG=1` in Debug enables the D3D12 debug layer. Treat
any debug-layer message as a build-breaking bug, not a warning to skip.

### Invariant checks

```powershell
pwsh -NoProfile -File tools/check-invariants.ps1
```

Machine-checks the non-negotiables above plus doc-level drift. Fourteen checks:
every package declaring a layer, graphics-API
isolation, `renderer` never including `scene`, downward-only dependencies, no
empty packages, no `add_pass` from an app, header layout, resolvable doc links,
the ROADMAP LOC audit, spec statuses, the ENGINE_MAP dependency graph, the
analizeMax analysis set, that a
package added under an `if()` is never linked unconditionally (which would fail
`cmake` at generate time wherever that condition is false), and
**format-hygiene** — the tree obeying the `.editorconfig` it ships (no tabs, 100
columns, no trailing whitespace, a final newline, no BOM, LF for sources and
CRLF for shell scripts) plus the root `.clang-format` still being the
`DisableFormat: true` no-op that keeps Visual Studio's format-as-you-type off
this hand-tuned tree. That map check reads
ENGINE_MAP.md as a graph: every `Category #N` in a **Finish first** must
resolve, a Later row whose named blockers are all Done must be flipped to
Ready, and no two rows may block each other — a loop means neither ever
becomes Ready. No compiler or GPU needed — this is
what CI runs, since `--gates` cannot run on a hosted runner (the D3D12 backend
skips software adapters). Run it alongside the gates before shipping.

### What a gate is

There is no test framework. A gate is a plain function in
`packages/sandbox/src/main.cpp`:

```cpp
bool run_<name>_gate(/* the things it needs */) {
    const bool passed = /* real assertions on real values */;
    char message[224];
    std::snprintf(message, sizeof(message),
        "<Name> gate: thing=%d other=%s (%s)", thing, other,
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::<Channel>, message);
    return passed;
}
```

Rules that make a gate worth having:

- **Assert on values, not on "it didn't crash."** Compare against a number you
  derived independently — a resting height, an analytic camera delta, a
  readback magic value. `a != b` on two distinct enum values proves nothing.
- **Put the real measurements in the message**, including on failure. A `FAIL`
  line that hard-codes `thing=yes` tells you nothing when it goes red.
- Call it from the gate sequence in `main()`. `--gates` exits `0`/`1`; a normal
  run also executes gates and logs `FAIL` but still exits `0`.
- Write the gate **before** the implementation and watch it fail first.

### CLAUDE.local.md

`CLAUDE.local.md` at the repo root is gitignored — use it for machine-specific
notes (local paths, scratch state) that shouldn't be shared. Same for
`.claude/settings.local.json`.

Release / player build:

```powershell
cmake --build build --config Release --target game
.\build\bin\Release\game.exe --gates
```

## Git workflow

- Trunk-based: commit directly to `main`. No branches, no PRs.
- Commit message: `type(package): summary (Category #N)` — the
  `(Category #N)` suffix only when the commit closes an ENGINE_MAP.md row.
  Types: `feat`, `fix`, `refactor`, `docs`, `build`, `chore`, `test`.
  Example: `feat(ui): screen-space quads (UI #2)`.
- **Push automatically after every commit to `main`** — this is a standing,
  pre-authorized instruction, not something to confirm each time.
- Exception: force-push, `reset --hard`, rebasing pushed history, or any
  other destructive/history-rewriting operation always requires asking first,
  no matter the above.
- Review `git status` before staging broadly. Never `git add -A` / `git add .`
  as a reflex — name paths explicitly when a change touches many files.

## How this project maps onto the installed skills

The `superpowers` set is installed and its preamble tells you to invoke any
skill that might apply. Several of them assume a workflow this repo does not
use. The mapping, so it does not have to be re-derived every session:

| Installed skill | Here |
|-----------------|------|
| `test-driven-development` | Use it, but the test *is* a gate. There is no test framework — see "What a gate is" above. Write the gate first and watch it fail; that is this project's red-green. |
| `finishing-a-development-branch`, `using-git-worktrees` | Skip. Trunk-based: commit to `main`, push. There is no branch to finish and no PR to open. `/ship-feature` is the closeout. |
| `requesting-code-review`, `code-review:code-review` | Skip the PR-shaped parts. `--gates` plus `tools/check-invariants.ps1` is the review gate; `/analizeMax` is the audit. |
| `brainstorming`, `writing-plans`, `executing-plans` | Use as written — steps 2 and 3 of the loop above call for them by name. |
| `systematic-debugging`, `verification-before-completion` | Use as written. Nothing here conflicts. |

Where a skill and this file disagree, this file wins — that is the documented
precedence, not a judgement call.

## Docs map

| File | Purpose |
|------|---------|
| [README.md](README.md) | Setup, build, controls |
| [Philosophy.md](Philosophy.md) | Design principles |
| [Scaffold.md](Scaffold.md) | Original project scaffold statement |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Package graph, conventions |
| [docs/FOUNDATION.md](docs/FOUNDATION.md) | Core/math/platform API reference |
| [docs/packageRules.md](docs/packageRules.md) | Rules every package follows |
| [docs/STABILITY_NORTH_STAR.md](docs/STABILITY_NORTH_STAR.md) | Why stability-first; industry research |
| [docs/ROADMAP.md](docs/ROADMAP.md) | Canonical phase sequence + decision log |
| [docs/ENGINE_MAP.md](docs/ENGINE_MAP.md) | Canonical backlog (Done/Ready/Later/Far) |
| [docs/PICKING.md](docs/PICKING.md) | How to choose the next row from the map |
| [docs/GPU_BASELINE.md](docs/GPU_BASELINE.md) | Player GPU/OS/DLL requirements |
| [reasarch/GRAPICS-RESEARCH.md](reasarch/GRAPICS-RESEARCH.md) | Personal paraphrased learning notes — **unverified, not a technical reference**. Do independent research before implementing a graphics technique from it. |
| [docs/superpowers/specs/](docs/superpowers/specs/) | Design specs, one per shipped/in-progress feature |
