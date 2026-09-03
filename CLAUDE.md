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

- **What this is being built toward**: [VISION.md](VISION.md) — the finished
  product, what it is *not*, and the seven decisions that carry it. Read it
  before an architectural call. It is the single owner of the goal; nothing
  else restates it.
- **What to work on**: [docs/ENGINE_MAP.md](docs/ENGINE_MAP.md) — pick one
  **Ready** row. Picking rules: [docs/PICKING.md](docs/PICKING.md).
  Not sure what to pick? `/aim-next` reads git state, the gates and invariants,
  and asks the service for leverage, audit staleness and the graph verdict, then
  offers three directions with the evidence for each. It suggests; it does not
  start.
- **Canonical plan / changelog**: [docs/ROADMAP.md](docs/ROADMAP.md) — every
  shipped feature has a dated Why/Choice/Gate/Do-not entry there.

Those two files are canonical and stay canonical: if the service is down, commit
anyway and re-import later. The service renders views of them, but the file wins
any disagreement.

## The management service

A separate service at `../AI-Mangment` owns the data and the presentation of
project management: it holds the audit rubric, validates an audit against it,
mirrors the roadmap, computes the dependency graph, and keeps the decision list.
Five skills call it. There is no page for an agent to author and no derived fact
for a session to recompute by hand.

| Skill | Does |
|-------|------|
| `/aim-next` | Three directions with evidence. Suggests; never starts |
| `/aim-row` | One roadmap row, research to shipped, in a single run |
| `/aim-ship` | Close out a row. Names which Later rows just became Ready |
| `/aim-audit` | Audit the whole engine, grade six dimensions, submit |
| `/aim-fix` | Apply the plan `/aim-audit` wrote |

Everything is reached through one wrapper — `pwsh -NoProfile -File tools/aim.ps1
<cmd>` — which resolves `aim` from PATH, npm's global directory, or the sibling
checkout via node. Skills run in a non-interactive shell where npm's shim
directory is not on PATH, so calling `aim` bare is what fails. Start with
`tools/aim.ps1 doctor`.

Two commands worth knowing outside the skills, because they answer questions the
repo cannot:

- `tools/aim.ps1 audit diff` — what changed since the previous audit: grade
  movement, and which findings **carried over**. A finding reported three times
  means the fix did not work or the diagnosis was wrong. Only safe to read
  *after* submitting an audit, which is why it is a separate command.
- `tools/aim.ps1 decisions` — the questions an audit raised and deliberately
  would not answer, because answering one changes behaviour, an API, or what a
  word in the backlog means. Answer one with
  `decisions answer <code> --note "..." --ref <where>`.

## The loop, per Ready row

1. Pick **one** Ready row. Don't start a Later or Far row (see ENGINE_MAP.md's
   Status table for what that means).
2. Brainstorm and write a design spec with the `superpowers:brainstorming`
   skill → `docs/superpowers/specs/YYYY-MM-DD-<topic>-design.md`.
3. Plan with `superpowers:writing-plans`, then implement.
4. Gate it: `.\build\bin\Debug\sandbox.exe --gates` must pass. If GPU code
   changed, also run with `ENGINE_GPU_DEBUG=1` and confirm the debug layer is
   silent.
5. Run `/aim-ship` to close it out (updates the map/roadmap, commits, pushes).

`/aim-row <category> #<row>` does all five in one unbroken run: deep research
(codebase **and** web), one mandatory question round that ends by asking
whether to execute directly, across subagents, or as a workflow, then build,
gate and `/aim-ship`. The question round is its only interruption.

Separately, `/aim-audit` audits the whole engine — code and non-code — and
grades six dimensions against an absolute standard. It is expensive and
deliberate: run it to get the real picture, not during feature work. It writes a
phased plan to `docs/analysis/PLAN.md` that `/aim-fix` applies; only fixes that
need no approval reach that plan, and judgement calls become decisions on the
service instead. That one file stays local because the service has no plan
endpoints by decision.

Reports use two code systems: a dimension letter plus a number is a **finding**
(`D3` = developer setup, third finding), and `G1`–`G6` are **ceilings** that cap
a grade. Both are tabulated in [docs/analysis/README.md](docs/analysis/README.md),
which is also where a `(analizeMax D3)` commit tag can be looked up — the tag
keeps that spelling because 96 commits already use it.

## Non-negotiables

These outrank convenience every time, regardless of what a session's context
window still holds:

- Renderer never includes a graphics-API header (`d3d12.h` or equivalent) —
  only `rhi`. A new pass is registered with `add_pass` in
  `packages/renderer/src/standard_frame.cpp`, never from the sandbox — but the
  pass's shader and pipeline *are* app-owned, so a working pass spans two
  packages. The checklist and its file count are owned by
  [.claude/rules/renderer-boundaries.md](.claude/rules/renderer-boundaries.md)
  — read it rather than trusting a number here. For scale: the last renderer
  feature to ship, Renderer #16, touched 23 files across four commits.
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

Machine-checks the non-negotiables above plus doc-level drift, and prints one
line per check with the count it verified. **The script is the list** — it names
every check it ran, so do not re-enumerate them here or anywhere else; a
hand-kept copy drifts, and this paragraph used to be one. No compiler or GPU
needed, which is why this is what CI runs: `--gates` cannot run on a hosted
runner, because the D3D12 backend skips software adapters. Run it alongside the
gates before shipping.

Four checks are worth knowing the reasoning behind, because each encodes a
failure that was silent:

- **shader-target** — every default-constructed `ShaderCompileDesc` must name
  its `.target` (a copy inherits one and is exempt). `target` defaults to
  `Dxil`, so a desc that never sets it asks for the D3D backend's bytecode
  wherever it is used; a Vulkan device then rejects the blob at pipeline
  creation, the setup function returns early, and every gate after it in that
  function silently does not run. That cost fifty gates twice, with a green pass
  count both times — nothing fails, the gates are just absent, which no total
  can show. Set it from the device: `shader_target_for(device)`.
- **gate-registry** — every gate under `packages/sandbox/src/gates/` must be
  declared in `gates.hpp` and classified `Cpu` or `Gpu` in `kGates`, so a gate
  cannot exist and run in no sequence.
- **skill-frontmatter** — a skill's `description` and `when_to_use` must be
  quoted if they contain a ` #`. In YAML a space-then-hash starts a comment, so
  `Invoke as /aim-row renderer #16.` silently parsed as `Invoke as /aim-row
  renderer` — and those two fields are what the model reads to decide whether a
  skill applies, so the value was cut without the file looking wrong.
- **map-dependencies** — reads ENGINE_MAP.md as a graph: every `Category #N` in
  a **Finish first** must resolve, a Later row whose named blockers are all Done
  must be flipped to Ready, and no two rows may block each other — a loop means
  neither ever becomes Ready.

### What a gate is

There is no test framework. A gate is a plain function in
`packages/sandbox/src/gates/gates_<domain>.cpp`, declared in
`gates/gates.hpp`, and called from the sequence in `main.cpp`. Domains are
`core`, `platform`, `rhi`, `assets`, `scene`, `physics`, `renderer` — pick the
one the gate is *about*, not the one it happens to allocate from. Helpers only
that file needs are `static`; anything `main.cpp` also uses goes in
`sandbox_common.hpp`. Every gate lived in `main.cpp` until Sep 2026 — 72 of them by then, which had
made that one file 26% of the engine (analizeMax A4).

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

## Docs discipline

The markdown in this tree is within the same order of magnitude as the C++, and
that is why the rules below exist: at this size docs stop being free, and a
wrong doc costs more than a missing one because it is *acted on*.

**One owner per fact.** Every fact has exactly one document that states it.
Everything else links. When you catch yourself writing a number, a list or a
file path that already appears somewhere else, link instead — a second copy is
a future contradiction with a coin-flip deciding which half a reader believes.
The known owners:

| Fact | Owner |
|------|-------|
| What the finished product is, and is not | `VISION.md` — the only copy |
| Row status, blockers, what is Ready | `docs/ENGINE_MAP.md` — the only copy |
| Why a shipped feature is the way it is | `docs/ROADMAP.md` |
| Package list, layers, dependency graph | `docs/ARCHITECTURE.md` |
| The render-pass checklist and its file count | `.claude/rules/renderer-boundaries.md` |
| Which invariants exist | `tools/check-invariants.ps1` — it prints its own list |
| Gate count, package count, option list | the tree. Do not restate them in prose |
| Audit grades, findings, decisions | the management service |

**Prefer deleting to appending.** A doc that has grown a section per incident
is a doc nobody reads to the end. If a paragraph explains something the script
or the code now enforces, the enforcement is the documentation — cut the
paragraph to a sentence pointing at it.

**Two checks enforce the machine-checkable half**, and they are the only reason
the rest is credible:

- **doc-claims** — every package directory is a row in ARCHITECTURE.md's
  package table, every `option(ENGINE_*)` appears in both CMake-option tables,
  and every implementation package is named on its interface's row. Each of
  those three was wrong simultaneously when `rhi-vulkan` shipped.
- **doc-skill-refs** — every backticked `` `/<name>` `` in a live doc resolves
  to a real skill. Prose naming a past run is history and stays; a backticked
  command is a present-tense claim that you can run it. `docs/superpowers/` is
  excluded, because specs and plans are dated archives allowed to age.

What they do **not** check is prose: a count inside a sentence, or a claim about
what the engine can do. That is deliberate — verifying every copy of a fact is
the wrong fix, and having one owner is the right one. So when a check cannot
catch it, the answer is to move the fact, not to add a check.

**Dated documents are frozen.** Anything under `docs/superpowers/specs/`,
`docs/superpowers/plans/` or `docs/analysis/*-full.md` is a snapshot of what was
true when it was written. Do not update them to match the present; they are
evidence, and a rewritten spec is a lost record of what was actually decided.

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
| `finishing-a-development-branch`, `using-git-worktrees` | Skip. Trunk-based: commit to `main`, push. There is no branch to finish and no PR to open. `/aim-ship` is the closeout. |
| `requesting-code-review`, `code-review:code-review` | Skip the PR-shaped parts. `--gates` plus `tools/check-invariants.ps1` is the review gate; `/aim-audit` is the audit. |
| `brainstorming`, `writing-plans`, `executing-plans` | Use as written — steps 2 and 3 of the loop above call for them by name. |
| `systematic-debugging`, `verification-before-completion` | Use as written. Nothing here conflicts. |

Where a skill and this file disagree, this file wins — that is the documented
precedence, not a judgement call.

One skill here is third-party: `codemap` is vendored from
[beadnall/codemap](https://github.com/beadnall/codemap) (MIT) at a pinned
commit. It draws the package graph as an explorable isometric HTML page and is
read-only — it never edits the tree, so it is safe to run mid-feature. Ask for
"a codemap of this repo" or "diagram the architecture". Provenance, what was
left out of the vendored copy and how to update it:
[.claude/skills/codemap/README.md](.claude/skills/codemap/README.md).

## Docs map

| File | Purpose |
|------|---------|
| [README.md](README.md) | Setup, build, controls |
| [VISION.md](VISION.md) | **The finished product.** The goal, the non-goals, D1-D7 |
| [Philosophy.md](Philosophy.md) | Design principles |
| [Scaffold.md](Scaffold.md) | Original project scaffold statement |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Package graph, conventions |
| [docs/FOUNDATION.md](docs/FOUNDATION.md) | Core/math/platform API reference |
| [docs/packageRules.md](docs/packageRules.md) | Rules every package follows |
| [docs/STABILITY_NORTH_STAR.md](docs/STABILITY_NORTH_STAR.md) | Why stability-first; industry research |
| [docs/ROADMAP.md](docs/ROADMAP.md) | Canonical phase sequence + decision log |
| [docs/ENGINE_MAP.md](docs/ENGINE_MAP.md) | Canonical backlog (Done/Ready/Later/Far) |
| [docs/PICKING.md](docs/PICKING.md) | How to choose the next row from the map |
| [docs/analysis/README.md](docs/analysis/README.md) | The audit: codes, ceilings, what lives where |
| [docs/GPU_BASELINE.md](docs/GPU_BASELINE.md) | Player GPU/OS/DLL requirements |
| [reasarch/GRAPICS-RESEARCH.md](reasarch/GRAPICS-RESEARCH.md) | Personal paraphrased learning notes — **unverified, not a technical reference**. Do independent research before implementing a graphics technique from it. |
| [docs/superpowers/specs/](docs/superpowers/specs/) | Design specs, one per shipped/in-progress feature |
