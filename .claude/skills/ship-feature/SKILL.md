---
name: ship-feature
description: Close out a finished Ready-row engine feature — update ENGINE_MAP.md and ROADMAP.md, recount the LOC audit, commit, and push. Use once a feature's gate passes and it's ready to mark Done.
disable-model-invocation: true
allowed-tools: Bash(git add *) Bash(git commit *) Bash(git push *) Bash(git status *) Bash(git diff *)
---

Close out one finished ENGINE_MAP.md Ready row. Run these steps in order.

## 1. Confirm the gate

Ask for (or re-run) `sandbox --gates` output. It must pass. If the feature
touched GPU code, confirm it was also run with `ENGINE_GPU_DEBUG=1` and the
D3D12 debug layer stayed silent. Do not continue past this step on a red or
unverified gate.

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

## 4. Review and stage

Run `git status`. Stage only the files that belong to this feature — the
implementation plus the two docs above. Do not sweep in unrelated changes.

## 5. Commit

Use `type(package): summary (Category #N)` — `feat` for a shipped Ready row,
`fix`/`refactor` when that fits better than `feat`. Example:

```bash
git commit -m "feat(ui): screen-space quads (UI #2)"
```

## 6. Push

```bash
git push
```

## 7. Report

Tell the user: what shipped, which Later rows just became Ready as a result,
and the new total LOC from step 3.
