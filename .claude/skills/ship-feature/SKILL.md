---
name: ship-feature
description: Close out a finished Ready-row engine feature — update ENGINE_MAP.md and ROADMAP.md, recount the LOC audit, commit, and push. Use once a feature's gate passes and it's ready to mark Done.
disable-model-invocation: true
allowed-tools: Bash(git add *) Bash(git commit *) Bash(git push *) Bash(git status *) Bash(git diff *) Bash(cmake *) Bash(.\build\bin\Debug\sandbox.exe *) Bash(.\build\bin\Release\game.exe *) Bash(find *) Bash(wc *) Read Edit Grep Glob
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

Do not continue past this step on a red gate.

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

- Recount lines under `packages/` for `*.cpp`, `*.hpp`, `*.h`, and `*.hlsl`,
  excluding `build/` and `third_party/`. On this repo that's:

  ```bash
  find packages -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.hlsl' \) -not -path '*/third_party/*' | xargs wc -l | tail -1
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

## 8. Report

Tell the user: what shipped, which Later rows just became Ready as a result,
and the new total LOC from step 3.

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
  so explicitly in the report. Those ceilings are coupled across packages and
  overflow is a hard abort, not a dropped frame.
