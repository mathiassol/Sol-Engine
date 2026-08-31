---
name: ship-feature
description: Close out a finished Ready-row engine feature — update ENGINE_MAP.md and ROADMAP.md, recount the LOC audit, commit, push, and refresh the published roadmap page. Use once a feature's gate passes and it's ready to mark Done.
disable-model-invocation: true
allowed-tools: Bash(git add *) Bash(git commit *) Bash(git push *) Bash(git status *) Bash(git diff *) Bash(cmake *) Bash(.\build\bin\Debug\sandbox.exe *) Bash(.\build\bin\Release\game.exe *) Bash(find *) Bash(wc *) Bash(ls *) PowerShell Read Write Edit Grep Glob Artifact
---

Close out one finished ENGINE_MAP.md Ready row. Run these steps in order.

## 1. Run the gate

**Run it yourself — do not accept a claim that it passed.**

```bash
cmake --build build --config Debug --target sandbox
.\build\bin\Debug\sandbox.exe --gates
```

Exit code must be `0`, and every `gate:` line in the output must end `(pass)`.
If the feature touched GPU code, run it again with `ENGINE_GPU_DEBUG=1` and
confirm the D3D12 debug layer stayed silent — any debug-layer message is
build-breaking, not a warning to skip. If build/install-layout code changed,
also run `.\build\bin\Release\game.exe --gates`.

Then run the invariant checks, which need no compiler and no GPU:

```bash
pwsh -NoProfile -File tools/check-invariants.ps1
```

Both must be green. Do not continue past this step on a red gate or a failed
invariant.

## 2. Update docs/ENGINE_MAP.md

- Flip the shipped row's Status from **Ready** to **Done**.
- Scan every **Later** row in the file. For each whose **Finish first**
  names only the row that just shipped, flip it to **Ready**.
- If real new scope surfaced while implementing, add rows for it in the
  right category, in implementation order, with a Status and (if not Ready)
  a Finish first.

## 3. Update docs/ROADMAP.md

- Append a new section using the file's existing format:

  ```markdown
  ## <Category> #<N> — <name> (done)

  **Why:** <the problem this closes>

  **Choice:** <the actual decision made, if there was a real fork in the road>

  **Gate (met):** <the literal gate string the code logs via --gates>

  **Do not (still):** <what this deliberately does not add yet>
  ```

- Recount lines under `packages/` for `*.cpp`, `*.hpp`, `*.h`, `*.hlsl` and `*.hlsli`,
  excluding `build/` and `third_party/`. On this repo that's:

  ```bash
  find packages -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.hlsl' -o -name '*.hlsli' \) -not -path '*/third_party/*' | xargs wc -l | tail -1
  ```

- Update the Audit section's total line count, file count, and any
  per-package figures it calls out, to match the recount.
- Bump "Last updated" at the top of the file to today's date.

## 4. Update the design spec and docs/ARCHITECTURE.md

- Set the feature's spec under `docs/superpowers/specs/` to
  `Status: implemented`. Skipping this is why specs go stale — nothing else
  ever revisits them.
- If the feature **added a package** or **changed what a package is
  responsible for**, update that row in the ARCHITECTURE.md Packages table,
  and the dependency graph if the links changed. If it added a render pass,
  update the standard-frame pass chain listed in the `renderer` row.
- If it added a cvar, add it to the shipped-knobs table in
  `docs/FOUNDATION.md`. If it added a `platform::Key`, update the key list
  there too.

## 5. Review and stage

Run `git status`. Stage only the files that belong to this feature — the
implementation plus the docs touched above. Name paths explicitly; never
`git add -A` or `git add .`.

## 5b. Re-run the invariants, last, on what you are about to commit

```bash
pwsh -NoProfile -File tools/check-invariants.ps1
```

Not a duplicate of step 1. The LOC audit in step 3 is a snapshot, and any edit
after it — including the doc edits in step 4 — makes it stale. Running the
checks *immediately before* the commit is the only way the number in ROADMAP.md
describes the tree being committed.

This is not hypothetical: it is exactly how commit `979c840` went out claiming
21,877 lines against a tree holding 21,881, and CI caught it on the next push.

## 6. Commit

Use `type(package): summary (Category #N)` — `feat` for a shipped Ready row,
`fix`/`refactor` when that fits better than `feat`. Example:

```bash
git commit -m "feat(ui): screen-space quads (UI #2)"
```

## 7. Push

```bash
git push
```

## 7b. Refresh the published roadmap page

You just flipped a row in `docs/ENGINE_MAP.md` and added an entry to
`docs/ROADMAP.md`. The published roadmap artifact is *generated from those two
files*, so it is now wrong — it still shows the row as Ready and has no
decision-log entry for it.

This step belongs here, not in whatever called this skill: step 2 is the only
place a row's status changes, so this is the only place that can reliably know
the page went stale. A `/ship-feature` run invoked directly from CLAUDE.md's
loop must refresh it just as much as one invoked by `/roadmap`.

Regenerate and re-publish per `.claude/skills/analizeMax/publishing.md` (see
**The roadmap page**). The essentials:

- Its permanent URL is `roadmap` in `docs/analysis/artifacts.json`. Pass it as
  `url`, with the registry's `favicon` and `title` unchanged, so the page anyone
  already has updates in place instead of a second one appearing.
- If that entry's `url` is empty the page has never been published: publish it,
  then **write the returned URL back into the registry immediately**. A URL that
  exists on claude.ai but not in the registry can never be updated again.
- Check the scorecard hub's nav still carries its **Roadmap** item with the new
  Ready count. If the count moved, re-publish the hub too.

Then confirm `analysis-set` still passes — it validates the registry, and this
step is the one that writes to it.

Skip only if `artifacts.json` does not exist at all, which means nothing has
ever been published; say so rather than silently doing nothing.

## 8. Report

Tell the user: what shipped, which Later rows just became Ready as a result,
the new total LOC from step 3, and the roadmap page URL.

## If this goes wrong

- **Gate is red** — stop. Do not update any doc, do not commit. Fix the gate
  or report the failing line; a half-shipped row is worse than an open one.
- **Recount is wildly off** the figure in ROADMAP.md — trust the command, not
  the file. The audit numbers have drifted before; that is expected and the
  recount is the fix.
- **`git push` is rejected** — someone pushed from the other machine. `git
  pull --rebase`, re-run the gate, then push. Never force-push (see CLAUDE.md).
- **The row turns out not to be Done** — flip it back to **Ready** in
  ENGINE_MAP.md and stop. Do not write a ROADMAP entry for it.
- **You changed the frame ring, an instance cap, or a descriptor limit** — say
  so explicitly in the report. Those ceilings are coupled across packages, and
  they no longer fail the same way as each other, so name which one moved:
  - **scene instance cap** — still a hard abort. `add_instance` asserts, and
    `ENGINE_ASSERT` is not compiled out in Release, so overflow kills the
    player build.
  - **frame constant ring** — recoverable. It logs once and drops draws, so
    overflow is a *silently missing object*, not a crash. That is the more
    dangerous of the two, because nothing reports it.
  - **shader descriptor heap** — recoverable. It clamps and logs.
- **The roadmap page did not refresh** (step 7b) — the published page now
  disagrees with `ENGINE_MAP.md`. Say so; not worth reverting a good commit
  over, but it must not go unmentioned.
