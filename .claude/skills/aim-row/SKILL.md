---
name: aim-row
description: Take one roadmap row from research to shipped in a single unbroken run — resolve it against the server (status, what waits on it, and the decision-log entries that already constrain it), research the codebase and the web, ask everything in one mandatory question round ending in how to execute, then build it, gate it, and ship it via /aim-ship. Invoke as /aim-row renderer #16.
when_to_use: The user names a category and row number to implement — "/aim-row renderer #16", "do foundation 5", "build the transparency row". Not for auditing (that is /aim-audit), not for choosing (that is /aim-next), and not for closing out work already written (that is /aim-ship, which this invokes at the end).
argument-hint: "<category> #<row>   e.g. foundation #5, renderer #16"
effort: max
allowed-tools: Bash(git *) Bash(cmake *) Bash(find *) Bash(grep *) Bash(wc *) Bash(ls *) Bash(cat *) Bash(sed *) Bash(awk *) Bash(pwsh *) Bash(./build/bin/Debug/sandbox.exe *) Bash(./build/bin/Release/game.exe *) PowerShell Read Grep Glob Write Edit AskUserQuestion Agent Workflow Skill WebSearch WebFetch
---

Deliver one roadmap row end to end. Research it properly, ask everything you
need in one round, then run to completion without stopping again.

**The question round is the only interruption.** Before it, gather. After it,
build, gate and ship without coming back for approval. Reporting a genuine
blocker is not stopping — inventing a checkpoint is.

---

## Phase 1 — Resolve the target

Parse `<category> #<row>` and ask the server:

```bash
pwsh -NoProfile -File tools/aim.ps1 roadmap row renderer/16
```

The category is the word references are written with, lowercased —
`renderer`, `rhi`, `ui`, `foundation` — or the heading's first word if those
differ. Add `--full` for the complete decision entries.

One call returns everything Phase 1 used to assemble by hand: the row's status
and item text, its **Finish first** blockers, **what waits on it**, and the
**decision-log entries for its category — including the `Do not` lines.** Print
the row verbatim before doing anything.

Those Do-not lines are the point of asking. They exist because someone already
decided against something, and re-litigating one silently is how a decision log
stops being worth keeping. Read them before you design.

Then check three things, and carry any that fail **into the question round**
rather than deciding alone:

- **Status is `ready`.** Starting a Later or Far row breaks a stated
  non-negotiable, and Later rows name what must land first. If it is not ready,
  the first question is whether to proceed anyway, quoting its blockers.
- **Status is not `done`.** If it is, say so and stop — there is nothing to
  build. This is the one case where stopping immediately is correct.
- **The tree is clean enough.** `git status --porcelain`. Uncommitted work from
  another session means your commits will mix two features; ask.

If the server is down, read `docs/ENGINE_MAP.md` and `docs/ROADMAP.md` directly —
the repo is canonical — and say the row was resolved by hand.

## Phase 2 — Deep research

Both halves, always. Neither substitutes for the other.

### In the codebase

- **Where it lives.** Which package, which layer, and what that layer may depend
  on. A new package needs the `foo` / `foo-bar` split rule from
  `docs/packageRules.md`, and must not be scaffolded empty.
- **What it touches.** Name every file before writing any. For a render pass
  that is eight files across two packages, and
  `.claude/rules/renderer-boundaries.md` has the checklist — read it, because
  every missed step there fails *silently*.
- **The pattern to follow.** Find the two closest existing features and read how
  they did it. Matching the surrounding idiom matters more than any preference of
  yours.
- **The budget.** If it adds per-draw or per-frame GPU data, check it against the
  frame-ring and descriptor ceilings the renderer-boundaries rule documents.
- **The gate shape.** Find the nearest existing `run_*_gate` and read what it
  asserts. Yours must assert on values derived independently, not on "it did not
  crash".

### On the web

- **Primary sources first** — specifications, vendor documentation, the original
  paper. A technique you cannot point at a real source for is one you do not
  understand yet.
- **`reasarch/GRAPICS-RESEARCH.md` is not a reference.** Unverified personal
  notes. Fine as a pointer to *what* to research; verify independently before
  implementing from it.
- **What real engines do.** Godot, bgfx, The Forge, sokol, wgpu and the public
  Unity/Unreal docs are inspectable. Find where they disagree with each other —
  that disagreement is usually the real design decision.
- **Search for the failure mode.** Not "how do I do X" but "why does X break".
  Any API detail that differs between D3D12, Vulkan and Metal must be found
  *now*: the RHI has a second backend already, and a wrong assumption here is
  the expensive kind. `StartInstanceLocation` was exactly this — D3D excludes it
  from `SV_InstanceID`, Vulkan folds it into `gl_InstanceIndex`, and Metal splits
  it out again, so the base instance had to travel in pass constants instead.

Write the research down as a design spec before the question round:
`docs/superpowers/specs/YYYY-MM-DD-<topic>-design.md` with `Status: spec`,
following the existing specs' format — Sources, Not this, Decision, Gate, Out of
scope. `superpowers:brainstorming` is the right tool when the design is genuinely
open; skip it when the row is narrow and the codebase already forces the answer.

## Phase 3 — The question round

**Always happens.** Even when you think you know everything — the point is that
the user sees the plan before it becomes code, once, when redirecting is
cheapest.

Use **AskUserQuestion**: at most four questions, two to four options each, so
batch them.

- Up to **three substantive questions** about the actual work — scope
  boundaries, which of two designs the research left open, whether an adjacent
  gap gets fixed in the same pass, a name that will end up in a public header.
  Do not ask what the codebase already answers, and do not ask permission to
  follow the rules.
- Then, **always last, the execution mode.** Mandatory, and in the final call.

If more than three substantive questions are genuinely needed, make two calls —
the execution-mode question stays last in the last one.

### The execution-mode question

Header `Execution`. Three options, recommended one first and marked
`(Recommended)`. Pick the recommendation from the row's actual shape and say why
in the option's description:

| Recommend | When the row looks like |
|-----------|-------------------------|
| **Directly** | One coupled change. Most feature rows: a render pass threads four structs, a gate, a shader and a recorder that all have to agree, and splitting that across agents costs more in reconciliation than it saves. Default here. |
| **Subagents** | Three or more genuinely independent pieces with disjoint files — several loaders, several unrelated call sites, docs that can be written while code lands elsewhere. |
| **Workflow** | A real fan-out with per-item verification: migrating N call sites, generating N variants, sweeping a pattern across many files. Deterministic and resumable. |

Say the trade-off plainly before they pick: parallel modes buy wall-clock and
lose attribution — when the build breaks after four agents edited four files,
you no longer know which edit did it.

## Phase 4 — Build it

Whatever mode was chosen, three rules hold.

**Write the gate first and watch it fail.** This project's equivalent of a
failing test. A gate that has never been red proves nothing. Put the real
measurements in its message, including on failure. Set `.target` from
`shader_target_for(device)` on any new `ShaderCompileDesc` — a desc that never
sets it asks for DXIL, a Vulkan device rejects the blob, the setup function
returns early, and **every gate after it silently does not run**. That cost fifty
gates twice, with a green pass count both times.

**One builder at a time.** There is a single `build/` directory and concurrent
`cmake --build` invocations corrupt each other. In parallel modes agents edit
only; every build, gate run and invariant check happens serialized in this
session. Never delegate verification to the agent that made the change.

**Stay inside the row.** Something else will look wrong while you are in there.
Note it, finish the row, mention it at the end. Widening scope mid-feature is how
one row becomes three and none of them ship.

Then verify, and do not report success on anything you have not run:

```powershell
cmake --build build --config Debug
.\build\bin\Debug\sandbox.exe --gates
pwsh -NoProfile -File tools/check-invariants.ps1
```

If GPU code changed, run the gates again with `ENGINE_GPU_DEBUG=1` and confirm
**0 messages** — the count, not an impression. If build or install-layout code
changed, also run `game.exe --gates` in Release.

## Phase 5 — Ship it

Set the spec's `Status: implemented`, then **invoke `/aim-ship`** and follow it
exactly.

Do not reimplement its steps. It owns the map flip, the stale-blocker sweep, the
ROADMAP decision entry, the LOC recount, the invariant re-run immediately before
committing, the commit form, the push, and the re-import. One owner for that
procedure; this skill is its caller, not its copy.

## Phase 6 — Report

What shipped, which Later rows became Ready as a result, the gate line verbatim,
the new LOC total, anything you noticed and deliberately left alone, and the
row's dashboard URL.

## When this goes wrong

- **The row is not Ready.** Raised as the first question, not decided alone. If
  the user says go, go — and say in the ROADMAP entry that it was taken out of
  order and why.
- **The row turns out to be two rows.** Finish the half that stands alone, ship
  it, and add the remainder to ENGINE_MAP as a new row with a Finish first. Do
  not ship half a row as if it were whole.
- **The gate cannot be made to fail first** because the behaviour already works.
  Then the row is already done, or the gate asserts the wrong thing. Work out
  which — both are findings.
- **The gate goes red after implementation.** Fix it. That is the job, not a
  reason to stop. Report only if genuinely blocked, with the failing line.
- **The debug layer is not silent.** Every message is build-breaking. It was 314
  warnings once, and nobody knew because nothing counted them.
- **Research contradicts a Do-not line.** Stop and raise it in the question round
  with both sources. The decision log may be right, or may predate what you just
  found — but silently overriding it is not yours to do.
- **You want to skip the question round** because the row seems obvious. That is
  the round's whole purpose: obvious-seeming rows are where an unstated
  assumption costs a day.
- **You want to skip the web research** because the codebase makes the answer
  clear. The codebase tells you what this engine does, not whether it is right.
