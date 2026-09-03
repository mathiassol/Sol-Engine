# codemap — vendored third-party skill

Upstream: <https://github.com/beadnall/codemap> (MIT, see [LICENSE](LICENSE)).
Vendored at commit `21aeff868b492a70295e96190cea9ce2f218d10a`.

Installed as a **project** skill rather than a personal one so it travels with
the repo — same as every other skill under `.claude/skills/`.

## Why the files were copied instead of cloned

The upstream install instruction is `git clone ... <repo>/.claude/skills/codemap`,
which leaves a nested `.git` inside a tracked tree. Every other skill here is
committed as plain files, so this one is too.

## What was left behind, and why

| Dropped | Reason |
|---------|--------|
| `AGENTS.md`, `.github/copilot-instructions.md`, `.cursor/rules/codemap.mdc` | Entry points for Codex, Copilot and Cursor. This repo drives the skill through Claude Code, and `.cursor/` is gitignored here anyway |
| `docs/example.jpg` | A 192 KB screenshot of the upstream demo output. This is a public repo; the sample earns nothing here |
| upstream `README.md` | Its links point at the four files above, and `tools/check-invariants.ps1` checks that every markdown link in the tree resolves. This file replaces it |
| `.gitignore` | Covered by the root one |

`SKILL.md` itself is unmodified, and it only ever references
`assets/template.html`, `scripts/verify.py` and `scripts/render.py` — all of
which are here.

Line endings were converted from CRLF to LF to match `.gitattributes` and the
`format-hygiene` invariant.

## Updating

Re-clone upstream to a scratch directory, copy the six kept files over, convert
to LF, and re-run `pwsh -NoProfile -File tools/check-invariants.ps1`. Bump the
commit hash above.

## Rendering

`scripts/render.py` shells out to Chrome or Chromium for a headless screenshot;
`scripts/render.swift` is the macOS WebKit path and does nothing on Windows.
Neither is needed to produce a map — the output is a self-contained HTML file
that opens in a browser.
