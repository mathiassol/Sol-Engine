---
name: analizeMax-metric
description: Take one metric from the newest /analizeMax full report — stability, architecture, capabilities, portability, developer setup, or AI tooling — and derive a focused 1-2 page report plus a complete ordered action list for it. Pure derivation from the existing report: no build, no gates, no web search, no reading source. Use when the user asks to zoom in on one metric, wants the action list for a dimension, or says something like "give me the stability report" or "what should we do about architecture".
when_to_use: The user wants one dimension of the last audit expanded into something they can work from. Trigger phrases include "analizeMax metric", "give me the <metric> report", "focus on <metric>", "action list for <metric>", "what should we do about <metric>". Do not use to audit — that is /analizeMax. Do not use to apply fixes — that is /analizeMax-execute.
argument-hint: stability | architecture | capabilities | portability | devex | ai-tooling
allowed-tools: Bash(git *) Bash(ls *) Read Grep Glob Write Edit
---

Expand one metric from the last audit into a working document: a short report
and the full ordered list of what to do about it.

This command **derives**. It does not investigate.

---

## The no-discovery rule

Everything in the output traces back to the newest `docs/analysis/*-full.md`.
That is the entire input. The rule exists so this command stays cheap and
instant — an audit costs a long session, and re-auditing one metric to answer
"what should I do about stability" is the wrong trade.

**Do not:**

- build, run `--gates`, or run `check-invariants.ps1`
- search the web or fetch anything
- read engine source, config, or docs to form new claims
- add a finding the full report does not contain
- **re-grade the metric** — quote the grade the report assigned, never
  recompute it. Re-deriving a grade while looking at the old one is exactly the
  anchoring failure `/analizeMax` is built to avoid, and here there is no fresh
  measurement to anchor against instead.

**If the report does not cover something**, write *"not covered by the audit"*
and move on. A derived document that quietly invents its gaps is worse than one
with holes, because the holes are the signal to run a fresh audit.

The only files you may open: the full report, `PLAN.md`, `LATEST.md`, and
`git log`/`git rev-parse` for the staleness check.

## Step 1 — Resolve the metric

Accept a friendly name, a key, or a near-miss:

| Metric | Key | Also accepts |
|--------|-----|--------------|
| Stability | `S` | stability, crashes, robustness |
| Architecture | `A` | architecture, layering, coupling |
| Capabilities | `C` | capabilities, features, what games |
| Portability | `P` | portability, cross-platform, linux, macos |
| Developer setup | `D` | devex, setup, dx, tooling-human, docs |
| AI tooling | `T` | ai-tooling, ai, claude, skills |

If no argument was given, list the six with their grades from the newest report
and ask which one. Ask in plain text — do not use AskUserQuestion, there are six
options and it takes at most four.

## Step 2 — Load the source and check it

```bash
ls -1 docs/analysis/*-full.md | sort | tail -1
```

That file is the source. It runs to tens of thousands of words, so navigate by
anchor rather than reading linearly. The layout is stable:

| Anchor | Holds |
|--------|-------|
| `## 2. Grades` | The grade table (Key / Dimension / Band / Grade / Ceilings fired), then **Why each modifier** — which names the single criterion a `+` missed — then **Falsification**, as blockquotes reading `> One band higher if …` and `> One band lower if …`, six of each. |
| `## 3. Per dimension` | `### <KEY> — <Name> — <Band> — <Grade>`, one per metric. |
| `#### <ID> — <title> — **<Severity>**` | The findings, inside their dimension. Severity in the heading is what `G5` was traced through, so read it — a `**Critical**` is why a dimension is capped. |
| `## 4. Cross-cutting` | Findings that belong to more than one metric. Always check this; a shared finding missing from your list makes it look complete when it is not. |
| `## 5. Remedies` | Costed fixes for surviving findings. Your action list's **Do** and **Cost** fields come from here. |
| `## 6. Calibration notes` | The counter-arguments to the highest and lowest grades. Useful for the report's *what would move it* section. |
| `## 7. Appendix: reproduction` | Commands and their literal output. Cite from here for the **Proof** field instead of inventing a command. |

Read: the metric's own `###` section in full, its rows in the grade table, its
two falsification blockquotes, its `Why each modifier` bullet, everything in
Cross-cutting that names it, and its entries in Remedies and the Appendix.

**If no full report exists** — say so and offer `/analizeMax`. Stop. Do not
audit as a substitute.

**Staleness.** Compare the report's commit against `git rev-parse HEAD`:

```bash
git rev-list --count <report-sha>..HEAD
```

Because there is no discovery here, staleness cannot be detected by
measurement — only by that number. So it goes in the output header, always,
even when it is zero. If commits have landed since, say so at the top of the
report in one sentence: this is derived from a snapshot, and some of it may
already be fixed.

**Cross-reference `PLAN.md`.** Any action already sitting in the plan gets
marked, so this list never sends the user to redo work `/analizeMax-execute`
would have done.

## Step 3 — Write the output

One file: `docs/analysis/metric-<key>.md`, lowercase key
(`metric-stability.md`, `metric-ai-tooling.md`). **Overwrite it.** One file per
metric, so running this for stability does not clobber the architecture one.

No rotation — this is derived, and the full reports are the history.

### Part 1: the report

**600–1200 words, hard cap 1400.** One to two pages. Count them.

Written for the person about to do the work, not for a stakeholder — so it can
be technical where `LATEST.md` cannot. But every action must be actionable
without opening the full report. If a reader has to go read the source
document, this file failed.

Cover, in this order:

1. **Where this metric stands** — the grade, in a sentence, and what it means
   concretely. Not the band's definition; what it *feels like*.
2. **What is holding it down** — the ceilings that fired and the findings that
   caused them. This is the core of the document. Be specific: name files,
   numbers, and the failure each one produces.
3. **What is genuinely good here** — from the report's own credit section.
   Skipping this produces a document that reads like the metric is worthless,
   which mis-sets the work.
4. **What would move it** — quote the report's two falsification sentences and
   say which actions below correspond to them. This is the link between the
   grade and the list: it tells the user which work actually changes the number
   and which merely tidies.
5. **What the audit did not look at** — the honest edge of the source document.

### Part 2: the action list

**No length limit.** Everything the report implies for this metric, ordered.

**Ordering** — execution order, not severity order. A severity-sorted list is
useless to work from because item 1 often depends on item 6.

1. **Dependencies first.** If B needs A done, A comes first, regardless of
   which matters more.
2. **Then leverage** — what de-risks or unblocks the most per unit of work.
3. **At equal leverage, cheap and safe before expensive and risky**, so
   stopping halfway still leaves the tree better off.

State the ordering rationale in one line before the list. If an item is out of
severity order, that is the point — say why.

Each action carries:

```markdown
### <n>. <imperative title>
- **From:** <finding ID in the full report>
- **Why:** <one sentence — the failure this removes>
- **Do:** <precise enough to act on without the full report>
- **Proof:** <what shows it worked; "none available" is a valid and useful answer>
- **Cost:** <trivial | hours | days> — <one clause on what dominates the cost>
- **Status:** `plan-eligible` | `in PLAN.md` | `needs a decision — <the question>`
- **Moves the grade:** yes, toward <band> | no, hygiene only
```

The **Status** field is the one that earns its keep. It splits the list three
ways: what `/analizeMax-execute` will already do, what could be added to the
plan, and what is genuinely blocked on the user deciding something. Use the
same admission test `/analizeMax` uses — reversible, verifiable, no behaviour
change, no judgement call, bounded — and mark the failing test for anything
that needs a decision.

**Moves the grade** matters too, and it is where most action lists lie. Most
items will be `no, hygiene only`. Say so. A list where every item claims to
move the grade tells the user nothing about where to spend a limited afternoon.

End with a **one-line count**: how many actions, how many plan-eligible, how
many blocked on a decision, and how many actually move the grade.

## Step 4 — Close out

Report in chat: the grade, the number of actions in each Status bucket, the
first three actions by the ordering above, and the file path.

Do not paste the list into chat — it is a file, and it may be long.

Do not commit, and do not start doing the actions. This command produces a
document. Applying it is `/analizeMax-execute` (for plan-eligible items) or
ordinary work.

Do not publish an artifact. `LATEST.md` is the artifact-worthy summary; this is
a working file for one metric.

## When this goes wrong

- **No full report exists.** Offer `/analizeMax`. Do not substitute an audit —
  the whole point of this command is that it is cheap because it does not.
- **The report's section for this metric is thin.** Say so plainly and keep the
  output short. A three-action list from a thin section is an honest result; a
  twenty-action list padded out of it is fabrication.
- **The report is many commits stale.** Still produce the document, with the
  staleness sentence at the top, and recommend a fresh `/analizeMax` if the
  metric's findings touch files that have changed.
- **You want to check whether a finding is still true.** That is discovery.
  You cannot, and the staleness note is how the output handles it instead.
- **An action seems obvious but is not in the report.** Leave it out. If it
  genuinely belongs, the audit missed it, and that is worth saying in
  *"what the audit did not look at"* — it is not worth inventing a finding to
  cover.
- **Two metrics overlap on one finding** (common between Architecture and
  Portability, or Developer setup and AI tooling). Include it in both, and say
  in each that it is shared, so neither list reads as complete-and-independent
  when it is not.
