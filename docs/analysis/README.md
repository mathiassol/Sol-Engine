# Analysis

The audit lives on the management service, not in this directory. `/aim-audit`
measures the tree, grades six dimensions against a server-held rubric, and
submits one JSON document; the service validates it and its web views render it.
Nothing here is hand-written.

| Command | Does |
|---------|------|
| `/aim-audit` | The audit. Fetches the rubric, measures, researches externally, grades, submits. Nine validation rules are enforced server-side. Writes `PLAN.md`. Expensive — run it for the real picture, not during feature work |
| `/aim-fix` | Applies `PLAN.md`, verifying between phases |
| `tools/aim.ps1 audit show latest` | The newest audit, with staleness against `HEAD` |
| `tools/aim.ps1 audit diff` | Grade movement, and which findings **carried over**. A finding reported three times means the fix did not work or the diagnosis was wrong. Only safe **after** submitting |
| `tools/aim.ps1 decisions` | The questions an audit raised and would not answer |

Everything is reached through `pwsh -NoProfile -File tools/aim.ps1 <cmd>`. Start
with `tools/aim.ps1 doctor`.

## Files

| File | Lifecycle |
|------|-----------|
| `PLAN.md` | The phased remediation plan. Written by `/aim-audit`, applied by `/aim-fix`. **Overwritten every run.** Absent when the last plan has been applied and cleared |
| `DECISIONS.md` | Nine open decisions carried over from the last pre-service audit. **Temporary** — delete it once an audit re-raises them on the server. See the file's own header |
| `YYYY-MM-DD-HHMM-full.md` | A full report, kept as one readable offline snapshot. `/aim-audit` does not write these; the newest is from the last analizeMax run before the migration |

The plan is the only part of the loop the repo still owns end to end, because
the service has no plan endpoints by decision.

## Reading the codes

Every report and many commit messages use short codes. There are two systems and
they look alike, which is the whole problem.

**Findings** are a dimension letter plus a number, assigned in the order found:

| Letter | Dimension |
|--------|-----------|
| `S` | Stability |
| `A` | Architecture |
| `C` | Capabilities |
| `P` | Portability (cross-platform readiness) |
| `D` | Developer setup |
| `T` | AI tooling |

So `D3` is the third finding recorded against developer setup, and a commit
tagged `(analizeMax D3)` fixed it. The tag keeps that spelling for continuity —
96 commits already use it, and renaming it would orphan every one. The commit
subject always says what it did in words; the tag is a cross-reference, not the
explanation.

**Ceilings** are `G1`–`G6`. A ceiling caps a grade no matter how good the rest of
the dimension is, so a capped grade is a claim about one specific gap rather than
an overall impression:

| Code | Name | Fires when | Caps at |
|------|------|-----------|---------|
| `G1` | unmeasured | it would not build or run, so nothing could be measured | C+ |
| `G2` | doc-sourced | the evidence came from the project's own docs, not the tree | B− |
| `G3` | uncovered failure | a High-severity finding has no gate or check that would catch it | B+ |
| `G4` | no comparison | no external engine or library was compared against | A− |
| `G5` | unfixed critical | a Critical finding is still open | D+ |
| `G6` | better than reference | the extra bar for A+ — not a penalty | A |

`**G3** (finding D1)` in a grade table therefore reads: *capped at B+ because
developer-setup finding 1 is a serious gap that nothing would catch.*

One rule keeps this readable, enforced in the skill rather than here: no code is
ever written bare on its first appearance in a document.

## What CI checks here

**Links are not checked.** This directory is excluded from the `doc-links`
invariant: reports are dated snapshots, and an older one may legitimately
reference a file that has since moved. A generated file must never be able to
turn CI red for a stale link. `format-hygiene` skips it for the same reason.

There is no longer a check on the *set* of files here. The `analysis-set`
invariant existed to machine-check an artifact-URL registry — the audit used to
publish nine HTML pages to permanent URLs, and the registry was what kept those
URLs stable. The service renders those views now, so there is no registry, no
drift, and nothing for that check to verify. Pages published before the
migration keep serving their last version; they no longer update.
