# Claude Code Workflow Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace Cursor's progress-board rule + external canvases with a Claude Code `CLAUDE.md` / rules / skill / hook setup, and bring the engine's uncommitted backlog into a clean, disciplined git history.

**Architecture:** Six new config artifacts under the repo root and `.claude/` (CLAUDE.md, two path-scoped rules, one skill, one hook + its settings.json registration), one `.gitignore` update, one filesystem deletion (`.cursor/`), and two git commits (an explicitly-scoped checkpoint of pre-existing work, then the new config as its own commit).

**Tech Stack:** Markdown + YAML frontmatter (Claude Code CLAUDE.md/rules/skills), Bash (hook script), JSON (`.claude/settings.json`), git.

**Spec:** [docs/superpowers/specs/2026-08-25-claude-code-workflow-migration-design.md](../specs/2026-08-25-claude-code-workflow-migration-design.md)

**Execution note:** runs directly on `main` per explicit user consent (the design itself is "trunk-based, direct commits to main, no branches") — no worktree isolation, since the task's own subject is git history on `main`.

---

## Task 1: Root `CLAUDE.md`

**Files:**
- Create: `CLAUDE.md`

- [ ] **Step 1: Write the file**

```markdown
# Sol Engine — Claude Code guide

General-purpose C++20 game engine (Unity/Godot/Unreal category), Windows/D3D12
today. Full design principles: [Philosophy.md](Philosophy.md) and
[Scaffold.md](Scaffold.md). Read those before making architectural calls this
file doesn't cover.

## Start here

- **What to work on**: [docs/ENGINE_MAP.md](docs/ENGINE_MAP.md) — pick one
  **Ready** row. Picking rules: [docs/TODO_LATER.md](docs/TODO_LATER.md).
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

## Non-negotiables

These outrank convenience every time, regardless of what a session's context
window still holds:

- Renderer never includes a graphics-API header (`d3d12.h` or equivalent) —
  only `rhi`. A new pass is `add_pass` in
  `packages/renderer/src/standard_frame.cpp`, never the sandbox.
- Dependencies only point downward. No circular dependencies.
- Engine ≠ editor. No inspector, hierarchy, or content-browser UI inside any
  engine package or the sandbox. An editor, if it ever exists, is a separate
  executable (see ENGINE_MAP.md category 17).
- One feature, one gate, one package. Don't scaffold an empty package with no
  implementation (see [docs/packageRules.md](docs/packageRules.md)).
- Don't skip a Ready row to add an unrelated graphics-paper feature.

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
| [docs/TODO_LATER.md](docs/TODO_LATER.md) | How to pick the next row from the map |
| [docs/GPU_BASELINE.md](docs/GPU_BASELINE.md) | Player GPU/OS/DLL requirements |
| [docs/WHATS_NEXT.md](docs/WHATS_NEXT.md) | Archived 2026 foundation list (superseded) |
| [reasarch/GRAPICS-RESEARCH.md](reasarch/GRAPICS-RESEARCH.md) | Personal graphics-paper notes |
| [docs/superpowers/specs/](docs/superpowers/specs/) | Design specs, one per shipped/in-progress feature |
```

- [ ] **Step 2: Verify line count is under the ~200-line adherence guideline**

Run: `wc -l CLAUDE.md`
Expected: a number under 200 (should land around 95-105).

- [ ] **Step 3: Verify every relative link target actually exists**

Run: `for f in Philosophy.md Scaffold.md README.md docs/ARCHITECTURE.md docs/FOUNDATION.md docs/packageRules.md docs/STABILITY_NORTH_STAR.md docs/ROADMAP.md docs/ENGINE_MAP.md docs/TODO_LATER.md docs/GPU_BASELINE.md docs/WHATS_NEXT.md reasarch/GRAPICS-RESEARCH.md; do [ -f "$f" ] && echo "ok: $f" || echo "MISSING: $f"; done`
Expected: every line prints `ok: ...`, no `MISSING` lines.

---

## Task 2: C++ conventions rule

**Files:**
- Create: `.claude/rules/cpp-conventions.md`

- [ ] **Step 1: Write the file**

```markdown
---
paths:
  - "packages/**/*.cpp"
  - "packages/**/*.hpp"
  - "packages/**/*.h"
---

# C++ conventions

From [Philosophy.md](../../Philosophy.md)'s Code section and
[docs/ARCHITECTURE.md](../../docs/ARCHITECTURE.md)'s conventions — applies to
every package.

- RAII everywhere. No manual cleanup that a destructor could do.
- Prefer composition over inheritance; prefer data over objects.
- Ownership is explicit: `std::unique_ptr` for modules, injected at startup.
  No hidden global state; pass context explicitly instead of reaching for a
  singleton.
- Minimize dynamic allocation; favor immutable data when practical.
- Headers live at `include/engine/<package>/<header>.hpp`. Implementation
  details live only in `src/`, never in a public header.
- Namespace `engine::`, with a sub-namespace per package.
- One factory function per implementation package (e.g. `create_platform()`,
  `create_rhi()`). Prefer forward declarations; include only what a header
  needs.
- A package with swappable backends splits into `foo` (interfaces/types
  only) and `foo-bar` (one implementation). `foo` must not depend on any
  `foo-bar`.
```

- [ ] **Step 2: Verify the frontmatter delimiters and glob list are well-formed**

Run: `head -5 .claude/rules/cpp-conventions.md`
Expected:
```
---
paths:
  - "packages/**/*.cpp"
  - "packages/**/*.hpp"
  - "packages/**/*.h"
```

---

## Task 3: Renderer/RHI boundaries rule

**Files:**
- Create: `.claude/rules/renderer-boundaries.md`

- [ ] **Step 1: Write the file**

```markdown
---
paths:
  - "packages/renderer/**"
  - "packages/rhi/**"
  - "packages/rhi-d3d12/**"
  - "packages/engine/**"
---

# Renderer / RHI boundaries

From [docs/ARCHITECTURE.md](../../docs/ARCHITECTURE.md) and
[docs/STABILITY_NORTH_STAR.md](../../docs/STABILITY_NORTH_STAR.md) — the
swap test this engine is built to pass.

- `renderer` never includes a graphics-API header (`d3d12.h` or equivalent).
  It only sees `rhi`'s interfaces. If a renderer file needs a D3D12 type,
  that's a sign the abstraction belongs in `rhi` instead.
- A new engine pass is `add_pass` inside
  `packages/renderer/src/standard_frame.cpp` (`setup_standard_frame`) — never
  registered from `sandbox/src/main.cpp`.
- Frustum culling and sun-bounds logic live in `renderer::extract_visible`.
  `renderer` never includes `scene` — the sandbox copies `scene::World` into
  `ExtractInstance` and hands the renderer only that snapshot.
- Every pass declares its reads/writes on the graph explicitly. Avoid pass
  side effects that aren't expressed as a graph dependency.
- One production GPU backend (`rhi-d3d12`) until a second is justified as its
  own package (e.g. `rhi-vulkan`) — grow the `rhi` interface now, implement a
  second backend only when actually needed.
```

- [ ] **Step 2: Verify the frontmatter parses as a distinct block**

Run: `sed -n '1,7p' .claude/rules/renderer-boundaries.md`
Expected: opening `---`, four `paths:` entries, closing `---`, blank line,
then the `# Renderer / RHI boundaries` heading.

---

## Task 4: `/ship-feature` skill

**Files:**
- Create: `.claude/skills/ship-feature/SKILL.md`

- [ ] **Step 1: Write the file**

```markdown
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
```

- [ ] **Step 2: Verify frontmatter fields are present and correctly named**

Run: `sed -n '1,5p' .claude/skills/ship-feature/SKILL.md`
Expected: `name: ship-feature`, `description: ...`,
`disable-model-invocation: true`, `allowed-tools: Bash(git add *) ...`,
each on their own line between the two `---` markers.

---

## Task 5: `SessionStart` map-sync hook + registration

**Files:**
- Create: `.claude/hooks/check-map-sync.sh`
- Create: `.claude/settings.json`

- [ ] **Step 1: Write the hook script**

```bash
#!/usr/bin/env bash
# SessionStart hook: nudge if packages/ has uncommitted work the map/roadmap
# doesn't reflect yet.
cd "$(git rev-parse --show-toplevel 2>/dev/null)" 2>/dev/null || exit 0

src_changed=$(git status --porcelain -- packages/ 2>/dev/null)
[ -z "$src_changed" ] && exit 0

docs_changed=$(git status --porcelain -- docs/ENGINE_MAP.md docs/ROADMAP.md 2>/dev/null)
[ -n "$docs_changed" ] && exit 0

echo "Heads up: packages/ has uncommitted changes from a previous session, but docs/ENGINE_MAP.md and docs/ROADMAP.md don't. If that work finished a Ready row, run /ship-feature before starting something new."
exit 0
```

- [ ] **Step 2: Make it executable and syntax-check it**

Run: `chmod +x .claude/hooks/check-map-sync.sh && bash -n .claude/hooks/check-map-sync.sh && echo SYNTAX_OK`
Expected: `SYNTAX_OK` with no other output (a syntax error would print to
stderr and `SYNTAX_OK` would not appear).

- [ ] **Step 3: Write the settings file**

```json
{
  "hooks": {
    "SessionStart": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "bash .claude/hooks/check-map-sync.sh"
          }
        ]
      }
    ]
  }
}
```

- [ ] **Step 4: Validate the JSON is well-formed**

Run (PowerShell): `Get-Content .claude/settings.json -Raw | ConvertFrom-Json | Out-Null; echo "JSON_OK"`
Expected: `JSON_OK` with no parse error printed above it.

- [ ] **Step 5: Manually exercise the hook's two branches**

Run: `bash .claude/hooks/check-map-sync.sh`
Expected right now (before Task 7's checkpoint commit lands): since
`packages/` currently has uncommitted changes and `docs/ENGINE_MAP.md` /
`docs/ROADMAP.md` are also currently uncommitted (modified, not committed),
this should print **nothing** — the "docs also changed" branch exits 0
early. This confirms the hook doesn't false-positive on the exact state this
repo is in right now.

---

## Task 6: Remove the Cursor rule folder

**Files:**
- Delete: `.cursor/` (entire directory)

- [ ] **Step 1: Confirm it is untracked before deleting**

Run: `git status --porcelain .cursor/`
Expected: a line starting with `??` (untracked) — e.g. `?? .cursor/`. If
this instead shows nothing or a tracked-file status, STOP and re-check the
spec's Context section before deleting anything.

- [ ] **Step 2: Delete it**

Run: `rm -rf .cursor`

- [ ] **Step 3: Verify it's gone**

Run: `git status --porcelain .cursor/ ; ls -la .cursor 2>&1`
Expected: no `.cursor` output from `git status`, and `ls` reports
`No such file or directory`.

---

## Task 7: Checkpoint commit (pre-existing backlog)

**Files:** none created — this stages and commits the pre-existing working
tree, explicitly excluding this migration's own new files. Runs **before**
Task 8's `.gitignore` edit, so the version of `.gitignore` captured here is
whatever was already modified before this migration started — not this
migration's own additions.

- [ ] **Step 1: Snapshot current status for reference**

Run: `git status --porcelain > "$CLAUDE_SCRATCHPAD/pre-checkpoint-status.txt" 2>/dev/null || git status --porcelain > /tmp/pre-checkpoint-status.txt; echo done`

(Use the session scratchpad directory if `$CLAUDE_SCRATCHPAD` isn't set,
substitute its actual path.)

- [ ] **Step 2: Stage every pre-existing path explicitly — never `git add -A`**

Run:
```bash
git add .gitignore CMakeLists.txt Philosophy.md README.md cmake/ docs/ packages/ run.bat cpm.json
```

This adds: all modified tracked files under `cmake/`, `docs/`, `packages/`
(including the 22 pre-existing `docs/superpowers/specs/*.md` files), plus
the top-level modified/tracked files, plus any currently-untracked files
already living under those same directories (e.g. the new `packages/*`
folders and `docs/ENGINE_MAP.md`, `docs/ROADMAP.md`,
`docs/STABILITY_NORTH_STAR.md`, `docs/GPU_BASELINE.md` seen as untracked in
`git status`), plus `.gitignore`'s pre-existing (pre-migration) edits.

- [ ] **Step 3: Verify `CLAUDE.md`, `.claude/`, and this migration's spec/plan are NOT staged**

Run: `git status --porcelain | grep -E "^(A|M).*(CLAUDE\.md|\.claude/|claude-code-workflow-migration)"`
Expected: **no output**. If anything prints, run
`git restore --staged <path>` for each match before continuing.

- [ ] **Step 4: Review the full staged diff stat**

Run: `git diff --cached --stat | tail -30`
Expected: a large diff touching `packages/`, `docs/`, and the root files
listed in Step 2 — no `.claude/`, no root `CLAUDE.md`, no
`docs/superpowers/plans/` or `docs/superpowers/specs/2026-08-25-claude-code-workflow-migration-design.md`
entry.

- [ ] **Step 5: Commit**

```bash
git commit -m "$(cat <<'EOF'
Checkpoint: engine through phase 14, physics, scene, build/ship pipeline

Squashes the working-tree state accumulated under Cursor before git
discipline started here. Everything ENGINE_MAP.md and ROADMAP.md mark Done
is in this commit: foundation through phase 14, physics (overlaps, bodies,
capsule, triggers, raycasts), scene (names, hierarchy, save/load, prefabs),
the assets cooker and pak pipeline, the Release game.exe build/ship path,
gamepad input, follow/orbit/FPS cameras, and the character controller.

See docs/ROADMAP.md for the real per-feature decision log (Why/Choice/Gate)
covering this work, and docs/superpowers/specs/ for the design specs behind
the most recent features. Individual commits from here forward are tracked
normally.
EOF
)"
```

- [ ] **Step 6: Verify the commit landed**

Run: `git log -1 --stat | head -20 && echo --- && git status --porcelain`
Expected: the checkpoint commit as `HEAD`, and `git status --porcelain` now
shows only the untracked migration files (`CLAUDE.md`, `.claude/`,
`docs/superpowers/plans/2026-08-25-claude-code-workflow-migration.md`, and
the spec file) — `.gitignore` should NOT appear (fully committed, no pending
edits yet since Task 8 hasn't run).

- [ ] **Step 7: Push**

```bash
git push
```

Expected: push succeeds against `origin/main` (fast-forward, since `main`
was already up to date with `origin/main` before this commit).

---

## Task 8: `.gitignore` additions (the migration's own change)

**Files:**
- Modify: `.gitignore`

Runs **after** Task 7's checkpoint commit, so this produces a clean,
migration-only diff instead of getting bundled into the checkpoint.

- [ ] **Step 1: Add `.cursor/` next to the existing `.cursor-tmp/` line**

Find this block:
```
# IDE / editor
.idea/
.vscode/
.cursor-tmp/
*.swp
*.swo
*~
```

Replace with:
```
# IDE / editor
.idea/
.vscode/
.cursor/
.cursor-tmp/
*.swp
*.swo
*~
```

- [ ] **Step 2: Add a Claude Code personal-files section**

Append after the "IDE / editor" block (before "# Local / secrets"):
```

# Claude Code — personal/local, never shared
CLAUDE.local.md
.claude/settings.local.json
```

- [ ] **Step 3: Verify the result**

Run: `grep -n "cursor\|CLAUDE.local\|settings.local" .gitignore`
Expected: four lines — `.cursor/`, `.cursor-tmp/`, `CLAUDE.local.md`,
`.claude/settings.local.json`.

- [ ] **Step 4: Confirm this is now the only pending change to the file**

Run: `git status --porcelain .gitignore`
Expected: `M .gitignore` (freshly modified relative to Task 7's commit,
containing only the 4 new lines).

---

## Task 9: Follow-up commit (the migration itself)

**Files:** none created — stages and commits everything from Tasks 1-5 and 8.

- [ ] **Step 1: Stage the migration files**

```bash
git add CLAUDE.md .claude/ .gitignore docs/superpowers/specs/2026-08-25-claude-code-workflow-migration-design.md docs/superpowers/plans/2026-08-25-claude-code-workflow-migration.md
```

- [ ] **Step 2: Review what's staged**

Run: `git status --porcelain`
Expected: every line starts with `A ` (added) or `M ` (modified — only for
`.gitignore`) — nothing untracked left over, nothing from `packages/`.

- [ ] **Step 3: Commit**

```bash
git commit -m "$(cat <<'EOF'
docs: add Claude Code workflow migration

Replaces the Cursor progress-board rule (and its two out-of-repo canvases)
with a CLAUDE.md, two path-scoped rules, a /ship-feature skill, and a
SessionStart map-sync hook. See docs/superpowers/specs/2026-08-25-claude-code-workflow-migration-design.md
for the full design.
EOF
)"
```

- [ ] **Step 4: Push**

```bash
git push
```

- [ ] **Step 5: Final verification**

Run: `git status && echo --- && git log --oneline -5 && echo --- && ls -la .cursor 2>&1`
Expected: working tree clean, the two new commits on top of the original two,
`origin/main` matches `main`, and `.cursor` reports
`No such file or directory`.

---

## Plan self-review notes

- **Spec coverage:** Decision §1→Task 1, §2→Tasks 2-3, §3→Task 4, §4→Task 5,
  §6→Task 6. §5 (git workflow) splits across Task 7 (checkpoint + push),
  Task 8 (the gitignore additions specifically), and Task 9 (follow-up
  commit + push). The spec's Verification section's `/context` and
  rule-loads-on-matching-file checks are session-behavior checks only
  observable by starting a fresh Claude Code session afterward — flagged to
  the user at handoff rather than scripted as a task step.
- **Sequencing fix (found on critical review before execution):** the first
  draft edited `.gitignore` (old Task 6) *before* the checkpoint commit (old
  Task 8), which would have bundled this migration's new gitignore lines
  into the "pre-existing backlog only" checkpoint commit and left the
  follow-up commit's `git add .gitignore` a silent no-op. Fixed by moving
  the gitignore edit to Task 8, after the checkpoint commit (Task 7) —
  see each task's "Files"/intro note for the ordering rationale.
- **Placeholder scan:** no TBD/TODO; every step has literal file content or
  a literal command with an expected output.
- **Type consistency:** the skill name (`ship-feature`) matches between
  Task 4's frontmatter and CLAUDE.md's "The loop" step 5 and Git workflow
  section. The hook script path (`.claude/hooks/check-map-sync.sh`) matches
  between Task 5 Step 1 and Step 3's `command` field.
