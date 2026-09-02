---
name: aim-fix
description: Apply the remediation plan that /aim-audit wrote to docs/analysis/PLAN.md. Every task in it already passed the admission test, so none needs individual approval — this checks the plan is still describing the current tree, applies each phase, verifies between phases, and commits per phase. Runs directly by default; pass subagents or workflow only for a genuinely large plan.
when_to_use: The user has run /aim-audit and wants the plan applied. Trigger phrases include "apply the plan", "run the plan", "fix the audit findings", "execute the plan", "do the remediation". Does not audit or grade — that is /aim-audit — and never invents work the plan does not name.
disable-model-invocation: true
argument-hint: "[optional: subagents | workflow, or specific task IDs]"
allowed-tools: Bash(git *) Bash(cmake *) Bash(find *) Bash(grep *) Bash(wc *) Bash(ls *) Bash(cat *) Bash(sed *) Bash(awk *) Bash(pwsh *) Bash(./build/bin/Debug/sandbox.exe *) Bash(./build/bin/Release/game.exe *) PowerShell Read Grep Glob Write Edit Agent Workflow
---

Apply `docs/analysis/PLAN.md`. Every task in it passed the admission test when
it was written — reversible, verifiable, no behaviour change, no judgement call,
bounded — so **no task here needs individual approval.** Your job is to confirm
the plan still describes this tree, apply it, and prove each phase green.

The plan file is the one thing the management service does not hold: Phase 4 was
skipped by decision, so there are no plan endpoints and this stays local. That is
deliberate, not a gap.

---

## Step 1 — Check the plan still fits the tree

Read `docs/analysis/PLAN.md`.

- **Missing** — say so, offer `/aim-audit`, stop.
- **Empty plan** — the audit found nothing that passed admission. Say so and
  stop. **Do not go looking for work.** An empty plan is a real result.
- **Already applied** — if the frontmatter carries an `applied:` line, this plan
  has been run. Say which commit applied it and stop unless the user asks for a
  re-run.

**Staleness.** Compare the plan's `commit:` against `git rev-parse HEAD`.

```bash
git rev-list --count <plan-sha>..HEAD
git diff --name-only <plan-sha>..HEAD
```

Same commit → go. Different → say how many commits landed, and intersect their
files with the files the plan names. **Overlap means the plan may be describing
text that no longer exists**: name the affected tasks and recommend a fresh
`/aim-audit` rather than applying them blind. No overlap → proceed and say so.

**Working tree.** `git status --porcelain`. Dirty means every commit in this run
mixes your changes with someone else's; say what is uncommitted and ask before
proceeding.

Then state the shape of the run: how many phases, how many tasks, and the union
of files they touch. Do this before applying anything.

## Step 2 — Constraints, in every mode

Correctness, not preference.

- **One builder at a time.** There is a single `build/` directory and two
  concurrent `cmake --build` runs corrupt each other. Parallel workers **edit
  only**; every build, gate run and invariant check happens serialized, here.
- **Verify between phases, never only at the end.** Each phase ends green or the
  run stops. A phase is the unit designed to be independently verifiable, which
  is why it is also the unit that gets committed.
- **Commit per phase.** `fix(<area>): <phase name> (analizeMax <task ids>)`.
  The tag stays `analizeMax` on purpose: 96 commits already use it, and
  `docs/analysis/README.md` documents it as the way to look a finding code up.
  Renaming it would leave one convention with two spellings, which is worse than
  a name that outlived its tool.
- **Stop on red.** Do not continue to the next phase and do not widen scope to
  fix it. Report which task's change broke it and either revert that phase or
  hand it back.
- **Never touch anything the plan did not name.** A task's file list is the
  boundary. If a task turns out to need a file outside it, that task failed
  admission test 5 — stop it, say so, move on.

## Step 3 — Apply

**Directly, by default.** For each phase in order: state it and its task IDs,
apply each task following its **Do** exactly (the plan is precise on purpose),
then run the phase's **Verify** plus whatever the phase's blast radius earns:

```bash
pwsh -NoProfile -File tools/check-invariants.ps1
```

Add a build and the gates when the phase touched compiled source, content, or a
shader — and skip them when it touched only prose, saying that you did. A plan of
comment corrections does not need a nine-minute rebuild to prove it, and
pretending otherwise is how verification becomes theatre. If it touched GPU code,
run the gates again with `ENGINE_GPU_DEBUG=1` and confirm 0 messages.

Report per phase as you go, not saved up for the end.

**`subagents`** — only if the user passed it. Group tasks so no two concurrent
workers share a file; print the grouping before dispatching. Each agent's prompt
must stand alone (it cannot see this conversation): the task ID, its exact
**Files** and **Do**, the standing constraint *edit only these files, do not
build, do not commit, do not fix anything you notice outside your task*, and what
to return. **You** verify when a group returns. Never delegate verification to
the agent that made the change.

**`workflow`** — only if the user passed it, and only worth the machinery for a
large plan with per-task verification structure. Same constraints; the builder
is still this session.

The honest trade-off, worth saying if the user asks for a parallel mode on a
small plan: it buys wall-clock and loses attribution. When the build breaks after
six agents edited six files, you no longer know which edit did it. `/aim-audit`
plans are small by construction — only factual corrections pass admission — so
directly is usually right, which is why it is the default rather than a question.

## Step 4 — Close out

Stamp the plan so a later session can tell it ran. Add to the frontmatter:

```
applied: <date>
applied_commit: <the last phase commit sha>
```

Then push (standing instruction for `main`) and report:

- what landed per phase, with commit hashes;
- anything skipped, and why;
- **everything still open in `aim decisions`**, as questions the user can
  answer. That list is the real output of this step — the part the plan
  deliberately would not decide for them, and where the next roadmap row or
  design spec comes from. It lives on the server now rather than in a section of
  a file the next audit overwrites, so answering one is a real action:

  ```bash
  pwsh -NoProfile -File tools/aim.ps1 decisions answer <code> --note "what was decided" --ref <where it lives>
  ```

  If applying a phase happened to settle one, answer it here rather than leaving
  it open — that is the whole point of it having a status.

Delete nothing. The plan stays until the next `/aim-audit` overwrites it.

The audit's findings are already on the server, so there is nothing to publish
and no registry to update. Ask the server what the graph looks like now if a
phase touched `ENGINE_MAP.md`:

```bash
pwsh -NoProfile -File tools/aim.ps1 roadmap import
```

## When this goes wrong

- **A task's change is already present.** Someone did it by hand. Skip it, say
  so, do not re-apply. Normal, not an error.
- **A task's file no longer exists.** The plan is stale. Stop that task, finish
  the others, recommend a fresh `/aim-audit`.
- **Verification is red and the diff looks unrelated.** Check for a stale binary
  before believing it — rebuild and re-run. That has produced a confidently wrong
  conclusion here before.
- **A task turns out to need a judgement call.** It should not have passed
  admission test 4. Stop it, move it to Needs a decision, and say the audit's
  admission test let one through — that is worth knowing, because test 4 is
  documented as the one that gets rationalised away.
- **The user asks to also fix something not in the plan.** Fine, but as separate
  work with its own commit. Do not let it ride inside a phase commit; that
  destroys the revertibility the phases were built for.
