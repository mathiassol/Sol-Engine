---
name: analizeMax-execute
description: Execute the remediation plan that /analizeMax generated in docs/analysis/PLAN.md. Asks whether to run it normally, fan out to subagents, or drive it as a workflow, then applies the phases with verification between each. Use when the user says "execute the analizeMax plan", "run the plan", "apply the analizeMax fixes", or asks to action the audit's plan.
when_to_use: The user has run /analizeMax and now wants the plan applied. Trigger phrases include "execute analizeMax plan", "execute the plan", "run the analizeMax plan", "apply the fixes", "do the plan". Do not use to generate a plan — that is /analizeMax.
argument-hint: [optional: normal | subagent | workflow, or specific task IDs]
allowed-tools: Bash(git *) Bash(cmake *) Bash(find *) Bash(grep *) Bash(ls *) Bash(sed *) Bash(awk *) Bash(rm *) Bash(./build/bin/Debug/sandbox.exe *) Bash(./build/bin/Release/game.exe *) PowerShell Read Grep Glob Write Edit AskUserQuestion Agent Workflow
---

Apply `docs/analysis/PLAN.md`. Every task in it already passed the admission
test when it was written — reversible, verifiable, no behaviour change, no
judgement call, bounded — so none of them needs individual approval. What does
need a decision is *how* to run them.

---

## Step 1 — Load and check the plan

Read `docs/analysis/PLAN.md`. Then:

**If it does not exist** — say so and offer to run `/analizeMax`. Stop.

**If it is empty** — the last audit found nothing that passed the admission
test. Say so and stop. Do not go looking for work.

**Staleness check.** Compare the plan's `commit:` frontmatter against
`git rev-parse HEAD`.

- Same commit → proceed.
- Different → say how many commits have landed since
  (`git rev-list --count <plan-sha>..HEAD`) and check whether any of them touch
  files the plan names. If they do, the plan may be describing a tree that no
  longer exists: tell the user which tasks are affected and recommend
  re-running `/analizeMax` rather than executing stale tasks.

**Working tree check.** `git status --porcelain`. If it is dirty, say what is
uncommitted and ask whether to proceed — executing on top of unrelated
in-flight work makes every commit in this run ambiguous.

Then state the shape of the run before asking anything: how many phases, how
many tasks, which files they touch in total.

## Step 2 — Ask how to run it

Use **AskUserQuestion** with exactly these three options. Put the recommended
one first and mark it `(Recommended)`.

Pick the recommendation from the plan's actual shape, and say why in the
option's description:

| Recommend | When |
|-----------|------|
| **Normal** | ≤ 6 tasks, or most tasks touch overlapping files so they serialise anyway, or any task is subtle enough that you want to see it land. |
| **Subagent** | ≥ 3 tasks with genuinely disjoint file sets, and the edits are mechanical enough to delegate. Keeps the main context clean for the verification you still do yourself. |
| **Workflow** | ≥ 8 tasks, or you want a resumable run with deterministic control flow and per-task verification structure. |

Header: `Execution`. The three options:

1. **Normal** — "I apply each phase in order, in this session, verifying
   between phases. Slowest, but every change is attributable to one task."
2. **Subagents** — "One agent per task, parallel across disjoint file groups.
   Faster. Attribution is weaker if verification goes red."
3. **Workflow** — "A scripted fan-out with structured per-task verification.
   Most machinery; best for large plans and resumable across interruptions."

If the user passed a mode as an argument, skip the question and use it.

**Say the honest trade-off before they choose.** Parallel modes buy speed and
lose attribution: when a build breaks after six agents edited six files, you no
longer know which edit did it. On a small plan that trade is a bad deal.

## Step 3 — Constraints that apply to every mode

These are correctness constraints, not preferences. Modes 2 and 3 break the
repository without them.

**One builder at a time.** There is a single `build/` directory. Two concurrent
`cmake --build` invocations corrupt each other's intermediate state. Parallel
agents do **edit-only** work; every build, gate run and invariant check happens
serialized, in the orchestrating session, between phases.

**Disjoint file sets or no parallelism.** Before fanning out, group tasks so no
two concurrent workers touch the same file. If two tasks share a file they go
in the same group and run sequentially inside it. Do not reach for git
worktrees here — an isolated worktree needs its own build directory and its own
several-minute build, which costs far more than the serialization saves.

**Verify between phases, never only at the end.** Each phase ends green or the
run stops.

**Commit per phase**, not per task and not once at the end. A phase is the unit
that was designed to be independently verifiable, so it is the unit that should
be independently revertible. Message form:
`fix(<area>): <phase name> (analizeMax <task ids>)`.

**Stop on red.** If a phase fails verification: do not continue to the next
phase, do not fix it by widening scope. Report which task's change broke it,
what the failure was, and either revert that phase or hand it back to the user.

**Never touch anything outside the plan.** A task's file list is the boundary.
If applying a task turns out to require editing a file the plan did not name,
that task failed admission test 5 — stop it, report it, move on.

---

## Mode 1 — Normal

For each phase, in order:

1. State the phase and its task IDs.
2. Apply each task. Follow its **Do** exactly; the plan is precise on purpose.
3. Run the phase's **Verify** command, plus:
   ```powershell
   cmake --build build --config Debug 2>&1 | Select-Object -Last 3
   .\build\bin\Debug\sandbox.exe --gates
   pwsh -NoProfile -File tools/check-invariants.ps1
   ```
   Skip the build only if the phase touched no compiled source or content.
4. Green → commit the phase. Red → stop per the rule above.

Report per phase as you go rather than saving it all for the end.

## Mode 2 — Subagents

The user selected this, which is the authorization to use the Agent tool.

1. **Group tasks by disjoint file sets.** Print the grouping before dispatching
   so the user can see what will run together.
2. Dispatch one agent per task within a group, all groups' agents in a single
   message so they run concurrently. Sequential groups, parallel within.
3. Each agent's prompt must be self-contained — it cannot see this conversation
   or the plan file's context. Include:
   - the task ID, its exact **Files** and **Do** text,
   - the standing constraint: *edit only the listed files; do not build, do not
     run gates, do not commit, do not fix anything you notice outside your
     task*,
   - what to return: the diff it made, or a one-line reason it made none.
4. When a group returns, **you** verify — build, gates, invariants — in this
   session. Never delegate verification to the agent that made the change.
5. Green → commit the phase, next group. Red → `git diff` to find which task's
   file broke it, report, stop.

Agents can and do report success on work they did not do. Read the diff.

## Mode 3 — Workflow

The user selected this, which is the authorization to use the Workflow tool.

Pass the plan's tasks in as `args` rather than embedding them in the script, so
the same script serves every run. Keep verification out of the workflow and in
this session — the one-builder rule still applies, and a workflow agent running
`cmake --build` concurrently with another is exactly the failure it prevents.

```js
export const meta = {
  name: 'analizemax-plan',
  description: 'Apply one /analizeMax remediation task per agent, grouped by disjoint files',
  phases: [
    { title: 'Apply', detail: 'one agent per task; groups never share a file' },
    { title: 'Report', detail: 'collect diffs for serialized verification' },
  ],
}

const TASK = {
  type: 'object',
  required: ['id', 'changed', 'summary'],
  properties: {
    id:      { type: 'string' },
    changed: { type: 'array', items: { type: 'string' } },
    summary: { type: 'string' },
    skipped: { type: 'string' },
  },
}

// args: [{ phase, group, tasks: [{ id, files, do }] }, ...]
// Groups within a phase are already disjoint — the caller guarantees it.
const results = []
for (const g of args) {
  phase(g.phase)
  const done = await parallel(g.tasks.map(t => () =>
    agent(
      `Task ${t.id} from the /analizeMax remediation plan.\n\n` +
      `Files you may edit (and NO others): ${t.files.join(', ')}\n\n` +
      `Change to make:\n${t.do}\n\n` +
      `Rules: edit only those files. Do NOT run cmake, do NOT run gates, ` +
      `do NOT commit, do NOT fix anything you notice outside this task. ` +
      `If the change is already present, make none and say so in "skipped".`,
      { label: `apply:${t.id}`, phase: g.phase, schema: TASK }
    )
  ))
  results.push(...done.filter(Boolean))
  log(`${g.phase}/${g.group}: ${done.filter(Boolean).length}/${g.tasks.length} applied`)
}
return { results }
```

When it returns, verify in this session exactly as Mode 1 does, phase by phase,
and commit per phase. A workflow that reports success is reporting that its
agents returned — not that the tree still builds.

---

## Step 4 — Close out

Push (standing instruction in CLAUDE.md for commits on `main`), then report:

- what landed, per phase, with commit hashes;
- anything skipped, and why;
- everything still sitting in **Needs a decision**, as questions the user can
  answer — that list is the actual output of this step, because it is the part
  the plan deliberately would not decide for them.

Then delete nothing. `PLAN.md` stays until the next `/analizeMax` overwrites it,
so there is a record of what was applied against which commit.

## When this goes wrong

- **A task's change is already present.** Someone did it by hand. Skip it, say
  so, do not re-apply. This is normal and not an error.
- **A task's file no longer exists.** The plan is stale. Stop that task, finish
  the others, recommend a fresh `/analizeMax`.
- **Verification is red and the diff looks unrelated.** Check for a stale
  binary before believing the failure — rebuild, re-run. That has produced a
  confidently wrong conclusion in this repository before.
- **A task turns out to need a judgement call.** It should not have passed
  admission test 4. Stop it, move it to Needs a decision, and say the audit's
  admission test let one through — that is worth knowing.
- **The user asks to also fix something not in the plan.** That is fine, but do
  it as separate work with its own commit. Do not let it ride along inside a
  phase commit; it destroys the revertibility the phases were built for.
