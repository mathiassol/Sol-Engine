#!/usr/bin/env bash
# SessionStart hook: nudge if packages/ has uncommitted *source* work the
# map/roadmap doesn't reflect yet.
#
# Only tracked-file modifications count. Untracked files are ignored on
# purpose: a stray crash dump or a scratch file is not a finished feature, and
# a nag that is wrong once is a nag that gets ignored forever.
cd "$(git rev-parse --show-toplevel 2>/dev/null)" 2>/dev/null || exit 0

# Porcelain XY status: either column may be a space (' M' unstaged, 'M ' staged,
# 'MM' both, 'A ' added...). '??' untracked never matches, which is the point.
src_changed=$(git status --porcelain -- packages/ 2>/dev/null \
    | grep -E '^[MARDU ][MARDU ] .*\.(cpp|hpp|h|hlsl|hlsli)$')
[ -z "$src_changed" ] && exit 0

docs_changed=$(git status --porcelain -- docs/ENGINE_MAP.md docs/ROADMAP.md 2>/dev/null)
[ -n "$docs_changed" ] && exit 0

echo "Heads up: packages/ has uncommitted source changes from a previous session, but docs/ENGINE_MAP.md and docs/ROADMAP.md don't. If that work finished a Ready row, run /ship-feature before starting something new."
exit 0
