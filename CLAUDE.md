# Sol Engine — Claude Code guide

General-purpose C++20 game engine (Unity/Godot/Unreal category), Windows today,
with **two GPU backends**: D3D12 is the shipped player backend, and `rhi-vulkan`
passes the whole gate suite and renders a live frame. A contract change costs two
implementations and two gate runs — see
[.claude/rules/renderer-boundaries.md](.claude/rules/renderer-boundaries.md).

Full design principles: [Philosophy.md](Philosophy.md) and
[Scaffold.md](Scaffold.md). Read those before making architectural calls this
file doesn't cover.

## Start here

- **What to work on**: [docs/ENGINE_MAP.md](docs/ENGINE_MAP.md) — pick one
  **Ready** row. Picking rules: [docs/PICKING.md](docs/PICKING.md).
  Not sure what to pick? `/aim-next` reads git state, the gates and invariants,
  and asks the service for leverage, audit staleness and the graph verdict, then
  offers three directions with the evidence for each. It suggests; it does not
  start. (`/whatnow` is the older equivalent that derives all of that by hand.)
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
that `/analizeMax-execute` applies. Only fixes that need no approval reach that
plan; judgement calls stay on a separate list. The `/aim-audit` + `/aim-fix`
pair below is the current path for both halves of that.

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

## The `aim` skills — prefer these

A separate service at `../AI-Mangment` now owns the data and the presentation of
all of this: it holds the rubric, validates an audit against it, mirrors the
roadmap, and computes the graph. **Prefer the `aim-*` skills.** They do the same
jobs with the derived facts computed instead of re-derived, and with no page for
an agent to author:

| Prefer | Over | What moved |
|--------|------|-----------|
| `/aim-next` | `/whatnow` | Leverage, audit staleness and the graph verdict come from one call, not a `grep` loop that once reported two false dependency loops |
| `/aim-row` | `/roadmap` | The row, its blockers, what waits on it and its Do-not lines come from the server |
| `/aim-ship` | `/ship-feature` | The server names which Later rows just became Ready, and round-trips the map to catch a hand-edit the parser reads differently. No artifact to republish |
| `/aim-audit` | `/analizeMax` | The rubric is fetched, nine validation rules are enforced server-side, and no HTML is written |
| `/aim-fix` | `/analizeMax-execute` | Runs the plan directly instead of always asking which of three modes to use — the plan is small by construction, so the question had one answer. Modes are still there as an argument |

Everything is reached through one wrapper — `pwsh -NoProfile -File tools/aim.ps1
<cmd>` — which resolves `aim` from PATH, npm's global directory, or the sibling
checkout via node. Skills run in a non-interactive shell where npm's shim
directory is not on PATH, so calling `aim` bare is what fails. Start with
`tools/aim.ps1 doctor`.

`/aim-fix` completes the set: it applies the `docs/analysis/PLAN.md` that
`/aim-audit` writes. That one file stays local because the service has no plan
endpoints by decision, so the plan is the only part of the loop the repo still
owns end to end.

Two commands worth knowing outside the skills, because they answer questions the
repo cannot:

- `tools/aim.ps1 audit diff` — what changed since the previous audit: grade
  movement, and which findings **carried over**. A finding reported three times
  means the fix did not work or the diagnosis was wrong. Only safe to read
  *after* submitting an audit, which is why it is a separate command.
- `tools/aim.ps1 decisions` — the questions an audit raised and deliberately
  would not answer, because answering one changes behaviour, an API, or what a
  word in the backlog means. Answer one with
  `decisions answer <code> --note "..." --ref <where>`. These used to live only
  in PLAN.md, which every audit overwrote.

The older skills still work and are not deprecated — the whole `analizeMax`
family is intact. **One sharp edge:** `/analizeMax-metric` derives from the
newest `docs/analysis/*-full.md`, and `/aim-audit` writes no such file. Run it
after an `/aim-audit` and it will silently describe the tree the last
`/analizeMax` measured, not the current one. For per-metric detail from a new
audit, read the report view on the dashboard; to regenerate a metric *document*,
run `/analizeMax` first so there is a report to derive from.

The repo also stays canonical for the roadmap file, the gates and the rules: if
the service is down, commit anyway and re-import later.

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

Use `pwsh`, not `powershell`. Windows PowerShell 5.1's execution policy is
`Restricted` by default and refuses to run the script; installing PowerShell 7
is what sets `RemoteSigned`. Under 5.1 the command needs
`-ExecutionPolicy Bypass`. The script itself is compatible with both, and CI
runs it under both.

Machine-checks the non-negotiables above plus doc-level drift. Eighteen checks:
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
this hand-tuned tree, and **gate-registry** — every gate defined under
`packages/sandbox/src/gates/` being declared in `gates.hpp` and classified `Cpu`
or `Gpu` in `kGates`, so a gate cannot exist and run in no sequence, and
**rhi-vocabulary** — no `D3D12`/`DXGI`/`SRV`/`UAV`/register-space terms in the
public `rhi` headers outside the binding contract that exists to name them, and
**shader-target** — every default-constructed `ShaderCompileDesc` naming its
`.target` (a copy inherits one and is exempt). `target` defaults to `Dxil`, so a
desc that never sets it asks for the D3D backend's bytecode wherever it is used;
a Vulkan device then rejects the blob at pipeline creation, the setup function
returns early, and every gate after it in that function silently does not run.
That cost fifty gates twice, with a green pass count both times — nothing fails,
the gates are just absent, which no total can show. Set it from the device:
`shader_target_for(device)`.
And **skill-frontmatter** — every `.claude/skills/*/SKILL.md` opening with closed
YAML frontmatter, whose `name` matches its directory, and whose `description` and
`when_to_use` are quoted if they contain a ` #`. In YAML a space-then-hash starts
a comment, so `Invoke as /aim-row renderer #16.` silently parsed as `Invoke as
/aim-row renderer` — and those two fields are what the model reads to decide
whether a skill applies, so the value was cut without the file looking wrong.
That map check reads
ENGINE_MAP.md as a graph: every `Category #N` in a **Finish first** must
resolve, a Later row whose named blockers are all Done must be flipped to
Ready, and no two rows may block each other — a loop means neither ever
becomes Ready. No compiler or GPU needed — this is
what CI runs, since `--gates` cannot run on a hosted runner (the D3D12 backend
skips software adapters). Run it alongside the gates before shipping.

### What a gate is

There is no test framework. A gate is a plain function in
`packages/sandbox/src/gates/gates_<domain>.cpp`, declared in
`gates/gates.hpp`, and called from the sequence in `main.cpp`. Domains are
`core`, `platform`, `rhi`, `assets`, `scene`, `physics`, `renderer` — pick the
one the gate is *about*, not the one it happens to allocate from. Helpers only
that file needs are `static`; anything `main.cpp` also uses goes in
`sandbox_common.hpp`. All 72 lived in `main.cpp` until Sep 2026, which made it
26% of the engine (analizeMax A4).

The shape is unchanged:

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
