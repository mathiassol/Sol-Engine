---
name: aim-ship
description: Close out a finished Ready row — run the gate and the invariants, flip the row in ENGINE_MAP.md, let the server tell you exactly which Later rows just became Ready, append the ROADMAP decision entry, recount the LOC audit, then commit and push. The roadmap mirror is re-imported and round-tripped as a safety net, so a hand-edit the parser reads differently is caught before the commit rather than after.
disable-model-invocation: true
allowed-tools: Bash(git add *) Bash(git commit *) Bash(git push *) Bash(git status *) Bash(git diff *) Bash(git log *) Bash(cmake *) Bash(pwsh *) Bash(find *) Bash(wc *) Bash(ls *) Bash(cat *) Bash(./build/bin/Debug/sandbox.exe *) Bash(./build/bin/Release/game.exe *) PowerShell Read Write Edit Grep Glob
---

Close out one finished ENGINE_MAP.md Ready row. Steps in order.

## 1. Run the gate

**Run it yourself — do not accept a claim that it passed.**

```bash
cmake --build build --config Debug --target sandbox
.\build\bin\Debug\sandbox.exe --gates
```

Exit code must be `0` and every `gate:` line must end `(pass)`. If the feature
touched GPU code, run it again with `ENGINE_GPU_DEBUG=1` and confirm the debug
layer stayed silent — any message is build-breaking, not a warning to skip. If
build or install-layout code changed, also run
`.\build\bin\Release\game.exe --gates`.

```bash
pwsh -NoProfile -File tools/check-invariants.ps1
```

Both green. **Do not continue past this step on a red gate or a failed
invariant.**

## 2. Flip the row, then ask the server what else changed

Edit `docs/ENGINE_MAP.md`: set the shipped row's Status from **Ready** to
**Done**. If real new scope surfaced while implementing, add rows for it in the
right category, in implementation order, with a Status and — if not Ready — a
**Finish first**.

Then push the file into the mirror and let the graph answer the follow-up
question:

```bash
pwsh -NoProfile -File tools/aim.ps1 roadmap import
pwsh -NoProfile -File tools/aim.ps1 roadmap check
```

`check` lists every **stale blocker** — a Later row whose named blockers are now
all Done. Those are exactly the rows to flip to Ready, and this replaces reading
all 285 rows by hand. It also reports cycles and dangling references, so a
**Finish first** you just typed that names a row which does not exist is caught
here.

Flip each stale-blocker row to Ready, then re-run `import` and `check` until it
reports the graph sound. A row can unblock another that unblocks a third, so one
pass is not always enough.

```bash
pwsh -NoProfile -File tools/aim.ps1 roadmap export --check
```

This regenerates `ENGINE_MAP.md` from the mirror and compares byte-for-byte. It
must say the file matches. A mismatch means the parser reads your edit
differently than you meant it — a Status the table does not recognise, a
malformed row, a broken column. Fix the file, not the check: this is the net that
catches a hand-edit before it becomes a commit.

## 3. Update docs/ROADMAP.md

Append a section in the file's existing format:

```markdown
## <Category> #<N> — <name> (done)

**Why:** <the problem this closes>

**Choice:** <the actual decision made, if there was a real fork in the road>

**Gate (met):** <the literal gate string the code logs via --gates>

**Do not (still):** <what this deliberately does not add yet>
```

The **Do not** lines are load-bearing — `aim roadmap row` prints them back, and
`/aim-audit` reads them before calling something a mistake. A vague one there
costs a later session real time.

Then recount lines under `packages/`:

```bash
find packages -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.hlsl' -o -name '*.hlsli' \) -not -path '*/third_party/*' | xargs wc -l | tail -1
```

Update the Audit section's total line count, file count, and any per-package
figures it calls out. Bump **Last updated** to today's date.

## 4. Update the spec and ARCHITECTURE.md

- Set the feature's spec under `docs/superpowers/specs/` to
  `Status: implemented`. Skipping this is why specs go stale — nothing else ever
  revisits them, and `/aim-next` reads `Status: spec` as available work.
- If the feature **added a package** or **changed what a package is responsible
  for**, update that row in the ARCHITECTURE.md Packages table, and the
  dependency graph if the edges changed. If it added a render pass, update the
  standard-frame pass chain in the `renderer` row.
- If it added a cvar, add it to the shipped-knobs table in
  `docs/FOUNDATION.md`. If it added a `platform::Key`, update the key list there
  too.

## 5. Review and stage

Run `git status`. Stage only what belongs to this feature — the implementation
plus the docs touched above. **Name paths explicitly; never `git add -A` or
`git add .`.**

## 5b. Re-run the invariants, last, on what you are about to commit

```bash
pwsh -NoProfile -File tools/check-invariants.ps1
```

Not a duplicate of step 1. The LOC recount in step 3 is a snapshot, and every
edit after it — including the doc edits in step 4 — makes it stale. Running the
checks *immediately before* the commit is the only way the number in ROADMAP.md
describes the tree being committed.

Not hypothetical: commit `979c840` went out claiming 21,877 lines against a tree
holding 21,881, and CI caught it on the next push.

## 6. Commit and push

`type(package): summary (Category #N)` — `feat` for a shipped Ready row,
`fix`/`refactor` when that fits better.

```bash
git commit -m "feat(ui): screen-space quads (UI #2)"
git push
```

Pushing after a commit to `main` is pre-authorized standing instruction — do not
ask.

## 7. Re-import the committed state

```bash
pwsh -NoProfile -File tools/aim.ps1 roadmap import
```

Once more, after the commit. The mirror stores the `commit_sha` it imported at,
which is what makes the dashboard's staleness honest — an import from a dirty
tree records a sha whose contents do not match. There is no page to republish
and no URL registry to maintain: the dashboard reads the mirror, so this one
call is the whole refresh.

## 8. Report

What shipped, which Later rows became Ready as a result (from step 2's `check`,
with the count), the new total LOC, and the row's dashboard URL.

## If this goes wrong

- **Gate is red** — stop. No doc edits, no commit. A half-shipped row is worse
  than an open one.
- **`export --check` reports a mismatch** — the file is not in canonical form.
  Read the diff it reports; do not commit and do not silently accept it. This is
  the check earning its place.
- **`roadmap check` reports a cycle** — two rows now block each other, so neither
  ever becomes Ready. Usually a **Finish first** written in the wrong direction.
  Fix it before committing.
- **Recount is wildly off** the figure in ROADMAP.md — trust the command, not the
  file. The audit numbers have drifted before; the recount is the fix.
- **`git push` is rejected** — someone pushed from the other machine.
  `git pull --rebase`, re-run the gate, then push. Never force-push.
- **The row turns out not to be Done** — flip it back to Ready, re-import, and
  stop. Do not write a ROADMAP entry for it.
- **The server is down** — commit and push anyway; the repo is canonical and a
  backlog that depends on a running server is a liability. Then run
  `aim roadmap import` when it is back, and say in the report that the mirror is
  behind. You lose step 2's stale-blocker list, so scan the Later rows by hand
  and say that you did.
- **You changed the frame ring, an instance cap, or a descriptor limit** — say so
  explicitly in the report. Those ceilings are coupled across packages and no
  longer fail the same way as each other, so name which one moved:
  - **scene instance cap** — a hard abort. `add_instance` asserts, and
    `ENGINE_ASSERT` is not compiled out in Release, so overflow kills the player
    build.
  - **frame constant ring** — recoverable. It logs once and drops draws, so
    overflow is a *silently missing object*, not a crash. The more dangerous of
    the two, because nothing reports it.
  - **shader descriptor heap** — recoverable. It clamps and logs.
