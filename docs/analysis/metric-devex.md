---
run: 2026-09-01 08:05 UTC
derived_from: 2026-08-31-1411-full.md
commit: 2a92289a6e8e79e11486c9e73301b138212a3e8c
state: report
---

# Developer setup — **C+**

> **Derived from a snapshot 20 commits old, and this metric moved more than any
> other in that window.** All seven findings have commits against them: D1 in
> `b4878f8`, D2 in `6c3f36c`+`8e83fdf`, D3/D4/D5 in `247939d`, D6 in `a875c6d`,
> D7 in `9931b34`. This document derives from the audit and cannot re-measure,
> so read the findings below as *what the audit found* and the action list as a
> record of what was done about it. The grade is quoted, not recomputed — see
> **What would move it** for why it may not have moved despite the work.

## Where this metric stands

**C+ — Working, top of band, with ceiling G3 fired on D1.**

The audit ran every command in `README.md` and all of them worked first try:
both builds clean, 71 gates passing in Debug and Release, the GPU debug layer
silent, 13 of 13 invariants green. Prerequisites are listed *with reasons* — the
Windows SDK entry explains that it supplies `dxcompiler.dll` and `dxil.dll` and
that configure emits a `FATAL_ERROR` without it, which the audit confirmed is
really thrown from `packages/game/CMakeLists.txt`.

So this is not a broken setup. What C+ describes is a setup that works and then
lays one trap for the next person who touches it. The band's own test —
*"a professional could inherit this and would not rewrite it; weaknesses are
known, bounded, and written down"* — fails on exactly one count, and D1 fails all
three parts of it.

## What is holding it down

**D1 (High) is the whole story, and the only reason a ceiling fired.**
Measured with clang-format 20.1.8 over 142 non-vendored files: **109 files
nonconforming, 2,883 violation sites.** Meanwhile `.editorconfig:15` asserted
the opposite — *"C++ formatting proper is owned by .clang-format … Keep the two
in agreement."* `clang-format` appeared in no CI job, no README instruction and
no rule; that `.editorconfig` comment was its only mention anywhere.

The failure it produces is concrete: a contributor opens the repo in Visual
Studio or VS Code with format-on-save enabled — a common default — and their
first save reformats three-quarters of the tree, burying whatever they came to
change. And the violations are overwhelmingly the author's *deliberate* column
alignment, which LLVM style undoes:

```
packages/core/src/log.cpp:12:25: error: code should be clang-formatted
    case LogLevel::Info:  return "INFO";
```

So the config did not merely go unenforced — it contradicted the house style it
claimed to own. G3 fired because a High finding had no gate, test or check
covering it.

The other six are all Medium or Low and none of them fired a ceiling:

- **D2 (Medium)** — 872 KB of dead content tracked in a public repo *and*
  installed into the player layout: `cartoon_husky.obj` (872,148 B),
  `cartoon_husky.mtl`, `triangle.hlsl`, all with zero references across 36
  content path strings, the cooker's five-asset list, every CMake `DEPENDS` and
  every markdown file. `copy_directory` shipped them next to both binaries on
  every build. That 872 KB was 15% of the repository's entire tracked size.
- **D3 (Medium)** — `ARCHITECTURE.md` said *"`rhi-d3d12` links `rhi`, and
  nothing else"* and drew the same edge, while CMake declares
  `PUBLIC_DEPS engine::rhi engine::math` and `device_d3d12.cpp:2098` really calls
  `math::build_rgba8_mip_chain`. The one wrong edge in a 20-package hand-drawn
  graph, and the `dependency-direction` invariant cannot catch it: `math` is
  Layer 0, so the edge is legally downward.
- **D4 (Low)** — `pwsh` was required by the documented invariant command and
  absent from the prerequisites; PowerShell 7 is not part of a base Windows
  install. The dependency is also avoidable — the script runs unchanged under
  the built-in 5.1.
- **D5 (Low)** — the last three numbered section comments in
  `check-invariants.ps1` disagreed with execution order (11th read `13.`, 12th
  read `11.`, 13th read `12.`). Checks 1–10 were correct.
- **D6 (Low)** — `run.bat` sat at the repo root referenced by no documentation.
- **D7 (Low)** — 22 hand-maintained numbers in `ENGINE_MAP.md` (the header
  totals plus 21 per-category subtotals) had no check. The audit recounted all
  22 and they were **correct** — the finding is the absent guard, not a wrong
  number.

## What is genuinely good here

Skipping this would mis-set the work, because most of this dimension is strong:

- **13 machine checks needing no compiler and no GPU**, including a cycle
  detector over the *backlog's* dependency graph, a CMake conditional-link
  checker, and an LOC-audit reconciliation.
- **CI comments that explain themselves and are true.** Every one the audit
  checked held up against the code — including the reason `--gates` is absent
  (the D3D12 backend skips `DXGI_ADAPTER_FLAG_SOFTWARE` adapters, so a hosted
  runner has none), which is an honest limitation rather than an excuse.
- **Doc accuracy where it was measured** — 22 markdown files with every link
  resolving, 281 map rows with correct totals and subtotals.
- **83 MB of third-party papers deliberately excluded** from a public MIT repo,
  with the copyright reasoning written into `.gitignore` itself.

Against references: Godot and bgfx both enforce formatting in CI, which is the
practice D1 sat outside. But `check-invariants.ps1` does something neither
reference does — validate project *documentation* against the tree — and the
audit named that an advantage rather than a quirk.

## What would move it

The audit's own two sentences:

> One band higher if the tree conformed to the `.clang-format` it ships.
> 109 of 142 files do not, across 2,883 violation sites, and `.editorconfig`
> asserts the two are "in agreement".

> One band lower if the documented setup path did not actually work. It does —
> every command in README.md was executed for this audit and every one
> succeeded on the first try.

Only **action 7 (D1)** corresponds to the upward sentence. Everything else on
the list below is hygiene: real, worth doing, and incapable of changing the
number.

**And there is a catch worth stating plainly.** The upward sentence asks for
*the tree to conform to the config*. The work actually done went the other way —
the config was relabelled descriptive, the false `.editorconfig` claim deleted,
and a "never bulk-reformat" rule recorded, after measuring that no clang-format
configuration can describe this tree (the best of six tuned settings still left
108 of 141 files and 2,012 sites diverging). That resolves the *finding* — the
trap is gone and the false claim with it — but it does not satisfy the *criterion
as written*. Whether C+ moves is therefore a question only a fresh
`/analizeMax` can answer, and this document deliberately does not guess.

## What the audit did not look at

- **No measurement of a genuinely clean machine.** Every command was run on the
  author's configured box. D4 was inferred from PowerShell 7 not shipping with
  Windows, not from a fresh install failing.
- **No timing.** Nothing on how long a cold configure or full build takes, which
  is a large part of how setup actually feels.
- **CI was read, not exercised** — `ci.yml` and its comments were verified
  against the code, but no run's logs were inspected.
- **No editor or IDE integration beyond the config files.** `.vscode`/`.idea`
  are gitignored, so whether IntelliSense and launch configs work from a clone
  was out of scope by construction.

---

## Action list

**Ordering.** This list is retrospective: every item already has a commit, so it
is ordered as the work actually ran — cheapest and safest first (docs and
comments, then file deletions, then a new check), with the two judgement calls
last because they needed a decision before anything could be done. That is also
the order a fresh reader should verify them in.

**A note on `Status`.** The command's vocabulary is
`plan-eligible` / `in PLAN.md` / `needs a decision`, none of which fits work
already shipped. Marking these as pending would send you to redo them, which is
the exact failure the field exists to prevent — so a fourth value, `done in
<sha>`, is used and the original classification is given alongside it.

### 1. Correct both `rhi-d3d12` dependency statements in ARCHITECTURE.md
- **From:** D3
- **Why:** The dependency graph a reader trusts stated the opposite of what CMake declares, on the one edge its surrounding prose specifically emphasised.
- **Do:** In the ASCII block, `rhi-d3d12 → rhi` becomes `rhi-d3d12 → rhi, math`. In the prose two paragraphs below, "`rhi-d3d12` links `rhi`, and nothing else" becomes a statement that it links `rhi` and `math`, the latter only for `build_rgba8_mip_chain`. Leave the preceding sentence about `rhi` itself — it is correct.
- **Proof:** `grep -n "rhi-d3d12" docs/ARCHITECTURE.md` shows `math` in both places; all 13 invariants still pass.
- **Cost:** trivial — two edits, no build needed.
- **Status:** `done in 247939d` (was `in PLAN.md`, Phase 1)
- **Moves the grade:** no, hygiene only

### 2. Add PowerShell 7 to README's Requirements list
- **From:** D4
- **Why:** A documented command used a tool the prerequisites never named, so a new contributor hits `pwsh : not recognized`.
- **Do:** Add a Requirements bullet for PowerShell 7 (`pwsh`), in the same style as the others — name, link, and why. Record that the script also runs unchanged under the built-in Windows PowerShell 5.1, so the dependency is avoidable.
- **Proof:** `powershell -NoProfile -File tools/check-invariants.ps1` exits 0 with all 13 checks — the audit verified this, so the claim is measured rather than assumed.
- **Cost:** trivial.
- **Status:** `done in 247939d` (was `in PLAN.md`, Phase 1)
- **Moves the grade:** no, hygiene only

### 3. Renumber the three misnumbered invariant section comments
- **From:** D5
- **Why:** A reader navigating `check-invariants.ps1` by its section numbers is misdirected for the last three checks.
- **Do:** `map-dependencies` (11th in output) becomes `# ── 11.`, `analysis-set` (12th) becomes `12.`, `conditional-target-links` (13th) becomes `13.`. Change no code and no prose after the number.
- **Proof:** `grep -n "^# ── [0-9]*\." tools/check-invariants.ps1` lists 1–13 ascending, matching the order of `grep -n "^Add-Result"`.
- **Cost:** trivial. Watch line endings: `.gitattributes` pins `*.ps1` to CRLF, and editing with `sed` strips the CR from touched lines.
- **Status:** `done in 247939d` (was `in PLAN.md`, Phase 1)
- **Moves the grade:** no, hygiene only

### 4. Delete the three unreferenced content files
- **From:** D2
- **Why:** 872 KB of dead weight tracked in a public repo and shipped into the player layout on every build.
- **Do:** `git rm packages/sandbox/content/meshes/cartoon_husky.obj`, `…/cartoon_husky.mtl`, `packages/sandbox/content/shaders/triangle.hlsl`. No CMake edit is needed — nothing references them.
- **Proof:** rebuild Debug and Release `game`, then `ls build/bin/*/content/meshes/` lists only `cartoon_husky.bin`, `cartoon_husky.gltf`, `cube.obj`. Gates unchanged, and `glTF gate` still reports `verts=7132 indices=37506`. Note `copy_directory` does not delete removed files, so clear the stale copies from `build/bin/*/content` first or the check is meaningless.
- **Cost:** trivial to do; the trap is that **`docs/ROADMAP.md:50` must be updated too** — `triangle.hlsl` counted toward the LOC audit, so `roadmap-audit` goes red otherwise. The audit's task list did not name that file, which is a defect in the plan rather than in the work.
- **Status:** `done in 6c3f36c` + `8e83fdf` (was `in PLAN.md`, Phase 2)
- **Moves the grade:** no, hygiene only

### 5. Reconcile ENGINE_MAP's 22 hand-written counts against a recount
- **From:** D7
- **Why:** 22 numbers nothing checked. They were all correct when measured, which is precisely when to add the guard — `roadmap-audit` exists because the ROADMAP's LOC figures went stale twice.
- **Do:** Extend the existing `map-dependencies` block, which already parses every row and already computes `$ready` for its summary. Tally done/ready/rows per category while parsing; capture the header line and each category's subtotal; report every mismatch naming the category and both figures. A missing header or subtotal line is itself a violation. Match separators as `\D+` rather than a literal middle dot so the check does not depend on file decoding.
- **Proof:** passes on the real tree reporting the verified totals; then change `76 Done` to `75 Done` and confirm it fails naming that mismatch, and revert. Also confirm it still runs under Windows PowerShell 5.1.
- **Cost:** hours — dominated by writing and control-testing ~30 lines of PowerShell, not by the parsing.
- **Status:** `done in 9931b34` (was `in PLAN.md`, Phase 4)
- **Moves the grade:** no, hygiene only — it closes the one CI gap the audit found, but D7 is Low and fired no ceiling.

### 6. Document `run.bat`
- **From:** D6
- **Why:** An interactive launcher at the repo root that no documentation mentioned, so nobody would find it.
- **Do:** Add it to README's run section: what it prompts for (GPU debug, extra arguments), that it checks the binary exists and says how to build it, and that it prints the exit code.
- **Proof:** `doc-links` still resolves; 13/13 invariants.
- **Cost:** trivial.
- **Status:** `done in a875c6d` (was `needs a decision` — the audit judged "document it or delete it" a taste call that failed admission test 4; the decision taken was to document)
- **Moves the grade:** no, hygiene only

### 7. Settle the formatter
- **From:** D1 — the only High finding, the only ceiling (G3), and the only item that bears on the grade
- **Why:** The shipped config contradicted the house style it claimed to own, and `.editorconfig` asserted they agreed. Format-on-save rewrote three-quarters of the tree.
- **Do:** The audit offered three options — reformat the tree and accept a 2,883-site diff; relax the config to permit the existing alignment; or delete `.clang-format` and the `.editorconfig` claim. It required a CI job either way. **What was actually done** was a fourth path, chosen after measuring that no configuration can describe this tree: tune the config to the two settings that measurably help (`BreakBeforeBinaryOperators: NonAssignment`, `AlignAfterOpenBracket: DontAlign`, taking 2,885 sites → 2,012), label it **descriptive and not enforced**, delete the false `.editorconfig` claim, and record "never bulk-reformat" in `.claude/rules/cpp-conventions.md` where a session reads it. **No CI job**, deliberately — enforcing a config that cannot describe the tree is the trap, not the fix.
- **Proof:** the config parses (`clang-format --style=file --dump-config`); the tree measures at exactly the 108/141 files and 2,012 sites its header documents; `.editorconfig` no longer claims agreement. What is **not** proven is that the grade moves — see below.
- **Cost:** hours — dominated by measuring six candidate settings across 141 files, not by the edit.
- **Status:** `done in b4878f8` (was `needs a decision` — three defensible answers, failing admission test 4)
- **Moves the grade:** **unknown, and this is the honest answer.** The finding is resolved: the trap is gone and the false claim with it. But the audit's upward criterion was *"the tree conformed to the `.clang-format` it ships"*, and the chosen fix made the config describe the code rather than the reverse. A fresh `/analizeMax` is the only thing that can settle whether C+ moves; this document quotes grades and does not recompute them.

---

**7 actions · 5 were plan-eligible and in `PLAN.md` · 2 needed a decision · 1 bears on the grade (action 7) · all 7 now have commits.** Nothing on this list is outstanding. The next real question for this metric is not on it: whether the D1 resolution satisfies the band, which needs a fresh audit rather than a derived document.
