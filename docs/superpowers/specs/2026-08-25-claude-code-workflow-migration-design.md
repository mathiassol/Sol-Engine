# Claude Code workflow migration (Cursor → Claude Code)

Date: 25 Aug 2026
Status: approved, not yet implemented

## Why

The project has been driven from Cursor. Cursor's only real involvement was one
"always apply" rule (`.cursor/rules/progress-board.mdc`) that told the agent to
keep two `.tsx` canvases — stored outside the repo, in Cursor's local app data —
mirroring `docs/ENGINE_MAP.md` (what to work on) and `docs/ROADMAP.md` (phase +
LOC board). Moving fully to Claude Code (CLI, desktop app, and IDE extensions —
same config everywhere) drops the canvas layer; the markdown was already the
declared source of truth. Separately, git discipline never existed: two commits
total, and everything the engine currently does (phases 0–14, physics, scene,
build/ship, gamepad, cameras, character controller) is sitting uncommitted.

## Context

- `docs/superpowers/specs/` already holds 22 design specs (20–25 Aug 2026)
  produced by the `superpowers` plugin's brainstorm → spec → plan flow,
  matching the most recently-shipped ENGINE_MAP/ROADMAP rows. This is already
  the de facto workflow and stays exactly as-is — this document is written in
  that same convention.
- Confirmed against current official docs (code.claude.com/docs/en, fetched
  25 Aug 2026): `CLAUDE.md` files, target under ~200 lines each; path-scoped
  `.claude/rules/*.md` with `paths:` frontmatter load only when Claude touches
  matching files; `.claude/skills/<name>/SKILL.md` frontmatter supports
  `disable-model-invocation` (user-only invocation) and `allowed-tools`
  (pre-approved tool patterns); hook mechanics are enforced regardless of what
  Claude decides, memory/rules are not. Desktop app, CLI, and IDE extensions
  read the identical files — no desktop-specific config exists.
- Git state at design time: `origin` → `github.com/mathiassol/Sol-Engine`,
  branch `main`, 2 commits ever ("Initial commit",
  "Initial engine scaffold with D3D12 forward pass"). `.gitignore` already
  covers `build/`, IDE files, and secrets reasonably well.

## Decision

### 1. Root `CLAUDE.md`

New file at repo root, target ~100–150 lines (under the ~200-line adherence
guideline). Sections, each linking to existing docs rather than duplicating
them:

- **Identity** — one line + links to `Philosophy.md` / `Scaffold.md`.
- **Start here** — pick from `docs/ENGINE_MAP.md`'s Ready rows; picking rules
  in `docs/TODO_LATER.md`; canonical plan/changelog is `docs/ROADMAP.md`.
- **The loop**, per Ready row: brainstorm → spec
  (`docs/superpowers/specs/`, via the existing `superpowers` skill) → plan
  (`superpowers:writing-plans`) → implement → gate (`sandbox --gates`,
  `ENGINE_GPU_DEBUG=1` for GPU-touching work) → `/ship-feature` to close out.
- **Non-negotiables** — condensed from `ENGINE_MAP.md`'s "Cross-cutting rules"
  and `packageRules.md`: renderer never includes D3D12/Vulkan headers,
  dependencies only downward, engine ≠ editor (no in-engine inspector), one
  gate per feature, don't scaffold an empty package with no implementation.
- **Build & gate commands** — condensed from `README.md`.
- **Git workflow** — see Decision §5 below; this section states the rules,
  not the rationale.
- **Docs map** — one table, every doc in the repo and its one-line purpose,
  so a cold session finds the right file without re-deriving structure.

### 2. Path-scoped rules (`.claude/rules/`)

Two files, using official `paths:` frontmatter so each loads only when Claude
actually reads/edits a matching file — not injected into every session:

- **`cpp-conventions.md`**
  ```yaml
  ---
  paths:
    - "packages/**/*.cpp"
    - "packages/**/*.hpp"
    - "packages/**/*.h"
  ---
  ```
  Body: `Philosophy.md`'s "Code" section distilled — RAII everywhere,
  composition over inheritance, data over objects, explicit ownership
  (`std::unique_ptr` for modules, injected at startup), no hidden global
  state, pass context explicitly, minimize dynamic allocation. Plus the
  header convention from `ARCHITECTURE.md`: `include/engine/<package>/<header>.hpp`,
  implementation details only in `src/`, one factory function per
  implementation package.

- **`renderer-boundaries.md`**
  ```yaml
  ---
  paths:
    - "packages/renderer/**"
    - "packages/rhi/**"
    - "packages/rhi-d3d12/**"
    - "packages/engine/**"
  ---
  ```
  Body: the swap-test rules from `ARCHITECTURE.md` / `STABILITY_NORTH_STAR.md`
  — renderer never includes a graphics-API header (`d3d12.h` or equivalent);
  a new engine pass is `add_pass` in `packages/renderer/src/standard_frame.cpp`,
  never the sandbox; frustum/extract logic lives in `renderer::extract_visible`;
  renderer never includes `scene`.

Not doing per-package `CLAUDE.md` files yet — the engine shares one
philosophy today; revisit only if a specific package earns its own
conventions (official guidance: nested `CLAUDE.md` when a package differs
from the rest, not by default).

### 3. `/ship-feature` skill (`.claude/skills/ship-feature/SKILL.md`)

Replaces the bookkeeping `progress-board.mdc` used to force after every map
change. Frontmatter:

```yaml
---
name: ship-feature
description: Close out a finished Ready-row engine feature — update ENGINE_MAP.md and ROADMAP.md, recount the LOC audit, commit, and push. Use once a feature's gate passes and it's ready to mark Done.
disable-model-invocation: true
allowed-tools: Bash(git add *) Bash(git commit *) Bash(git push *) Bash(git status *) Bash(git diff *)
---
```

`disable-model-invocation: true` so this only runs when explicitly typed —
never something Claude decides mid-task on its own, since it commits and
pushes.

Body (procedure Claude follows when invoked):

1. Confirm the gate passed — `sandbox --gates` output is clean;
   `ENGINE_GPU_DEBUG=1` was used and silent if GPU code changed.
2. Update `docs/ENGINE_MAP.md`: flip the row's Status to **Done**; flip every
   **Later** row whose only Finish-first was this row to **Ready**; add new
   rows if scope grew, in implementation order within the right category.
3. Update `docs/ROADMAP.md`: append a dated section in the file's existing
   format (`## <Category> #<N> — <name> (done)` with **Why**, **Choice**,
   **Gate (met)** — the literal gate string the code logs — and
   **Do not (still)**); recount lines under `packages/` for
   `*.cpp *.hpp *.h *.hlsl`, excluding `build/` and `third_party/`; update the
   Audit table; bump "Last updated" at the top of the file.
4. `git status`, review the diff, stage only what belongs to this feature.
5. Commit using the convention in Decision §5.
6. Push to `origin/main`.
7. Report what shipped, what's newly Ready, and the new LOC total.

### 4. `SessionStart` map-sync hook

Not a `Stop` hook — verified against current docs that a `Stop` hook's plain
stdout only reaches a debug log; making it visible requires
`permissionDecision: "block"`, which forces an extra turn every time it
fires. Given how a git diff looks mid-feature, that would fire on nearly
every turn while a feature is in progress — too disruptive for a soft
reminder. A `SessionStart` hook's plain stdout, by contrast, is injected as
context Claude can see and act on, once, at the start of the next session —
mechanically the right fit for "orient a fresh session," and it naturally
stops nagging as soon as the work is actually committed.

`.claude/hooks/check-map-sync.sh`:

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

Registered in `.claude/settings.json` (project-scoped, committed):

```json
{
  "hooks": {
    "SessionStart": [
      {
        "hooks": [
          { "type": "command", "command": "bash .claude/hooks/check-map-sync.sh" }
        ]
      }
    ]
  }
}
```

No stateful de-dup file — `SessionStart` only fires once per new session, so
the noise problem a `Stop` hook would have doesn't apply here. Deletable at
any time by removing the `hooks` block and the script.

### 5. Git & GitHub workflow

- **Backlog**: one checkpoint commit covering everything already in the
  working tree *before* this migration's own files exist — the engine
  through phase 14, physics, scene, build/ship, gamepad, cameras, character
  controller, and the 22 pre-existing `docs/superpowers/specs/` files.
  Staged explicitly by path (never a blanket `git add -A`/`git add .`), so it
  does not pick up the new `CLAUDE.md` / `.claude/` files or this spec — those
  land in the follow-up commit (Rollout §5). Commit message states plainly
  that it's a squashed pre-git-discipline checkpoint and points at
  `ROADMAP.md` for the real per-feature history. Pushed to `origin/main`.
- **Branching**: trunk-based, direct commits to `main`. No branches or PRs.
- **Commit message convention**: `type(package): summary (Category #N)` —
  the `(Category #N)` suffix only when the commit closes an ENGINE_MAP.md
  row. Types: `feat`, `fix`, `refactor`, `docs`, `build`, `chore`, `test`.
  Examples: `feat(ui): screen-space quads (UI #2)`,
  `fix(renderer): correct PCF bias`, `docs(roadmap): log TAA gate`.
- **Push**: auto-push after every commit to `main` — pre-authorized standing
  instruction in `CLAUDE.md`. Force-push, history rewrites (`reset --hard`,
  `rebase` on pushed history), and anything else destructive still always
  require asking first, regardless of this authorization.
- **`.gitignore` additions**: `.claude/settings.local.json`, `CLAUDE.local.md`
  (Claude Code's own personal/local-file convention), `.cursor/`.
  `docs/superpowers/` stays tracked — real design history, not scratch output.
- **CI**: none for now. Hosted GitHub Actions runners have no real GPU and
  the engine explicitly skips WARP/software adapters, so a hosted runner
  cannot run `--gates`. Revisit if this project ever grows a team or a
  release process; a self-hosted runner on real hardware would be the path
  then, not a hosted build-only workflow.

### 6. `.cursor/` decommission

Delete the local `.cursor/rules/progress-board.mdc` file and its parent
`.cursor/` folder. It is untracked (confirmed via `git status`), so this
touches no git history. The two canvas `.tsx` files it referenced live
outside the repo in Cursor's local app-data directory and are not touched —
they simply become unused.

## Not this (out of scope)

- No canvas or dashboard replacement — confirmed preference is markdown-only;
  `docs/ENGINE_MAP.md` / `docs/ROADMAP.md` are read directly.
- No `/engine-status` or "what's next" skill — same reason.
- No per-package `CLAUDE.md` files yet.
- No GitHub Actions CI yet.
- No feature branches or PR workflow.
- No change to `reasarch/GRAPICS-RESEARCH.md` or its filename/typos — the
  user's own personal notes, already flagged by them as unreviewed; not this
  document's concern.
- No change to the actual Cursor canvas `.tsx` files outside the repo.

## Verification

- `/context` in a fresh session lists the new `CLAUDE.md` under Memory files.
- Editing a file under `packages/renderer/` loads `renderer-boundaries.md`
  into context; editing a file under `packages/physics/` does not.
- `/ship-feature` is listed as user-invocable only (not something Claude
  reaches for unprompted) and its steps run correctly end to end on the next
  real feature.
- After a fresh `git clone`, `git log` shows the checkpoint commit followed
  by normal incremental commits; `git status` is clean; `origin/main`
  matches local `main`.
- `.cursor/` no longer exists on disk; `git status` shows no trace of it
  (it was never tracked).
- Starting a new session with uncommitted `packages/` changes and no
  matching docs changes shows the map-sync reminder; starting one with a
  clean tree or with docs already updated shows nothing.

## Rollout order

1. Write `CLAUDE.md`, both `.claude/rules/*.md` files, the `/ship-feature`
   skill, the `SessionStart` hook script, and `.claude/settings.json`.
2. Update `.gitignore`.
3. Delete the `.cursor/` folder.
4. Checkpoint commit: stage by explicit path (pre-existing `packages/`,
   `cmake/`, build files, docs, and the 22 pre-existing
   `docs/superpowers/specs/*.md` files) — explicitly *not* `git add -A`, and
   explicitly excluding `CLAUDE.md`, `.claude/`, and this spec file. Verify
   with `git status` that those three are still untracked before committing.
   Commit, then push.
5. Follow-up commit: stage `CLAUDE.md`, `.claude/` (rules, skill, hook,
   settings.json), the updated `.gitignore`, and this spec file. Commit as
   `docs: add Claude Code workflow migration`. Push.
