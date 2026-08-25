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
