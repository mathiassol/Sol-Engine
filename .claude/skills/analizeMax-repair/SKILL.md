---
name: analizeMax-repair
description: Repair drift in the analizeMax analysis set — validate every file's format, create a missing metric file on disk, restore a registry entry that lost its URL, and re-publish whatever is actually broken so the shared scorecard navigates properly. Use when a link is broken, the registry is out of sync, or the user asks to fix the analysis pages. Not a required step after an audit, and it never publishes a metric page that has never been analysed.
when_to_use: Trigger phrases include "analizeMax repair", "fix the analysis pages", "the scorecard link is broken", "the registry is out of sync", "rebuild the analysis artifacts". Does not audit, does not re-grade, and does not generate metric pages — only /analizeMax-metric does that.
argument-hint: [optional: --check to report without changing anything]
allowed-tools: Bash(git *) Bash(ls *) Bash(cat *) Read Grep Glob Write Edit Artifact
---

Bring `docs/analysis/` into a known-good state: every document present, every
format valid, every artifact published at its permanent URL, every link
resolving in both directions.

Read `.claude/skills/analizeMax/publishing.md` first. It is the contract; this
command enforces it.

This command **never audits and never grades.** It moves nothing but structure.
If a grade is missing it is because no audit has run — say so, do not invent one.

With `--check`, report every problem and change nothing.

---

## Step 1 — Take stock

```bash
ls -1 docs/analysis/
git rev-parse HEAD
ls -1 docs/analysis/*-full.md 2>/dev/null | sort | tail -1
```

Build a table of what should exist against what does:

| Expected | Source of truth |
|----------|-----------------|
| `artifacts.json` | the registry — create from the contract's template if absent |
| `LATEST.md` | the hub's content. Only `/analizeMax` writes it |
| `PLAN.md` | written by `/analizeMax` |
| at least one `*-full.md` | written by `/analizeMax` |
| six `metric-<key>.md` | `/analizeMax-metric`. A missing one gets a placeholder *file* from here, never a published page |

The six keys are exactly: `stability`, `architecture`, `capabilities`,
`portability`, `devex`, `ai-tooling`.

**If no full report exists at all**, there is nothing to build a scorecard from.
Say so, recommend `/analizeMax`, and stop. Do not create empty pages with no
grades on them — a scorecard with six blanks is worse than no scorecard.

## Step 2 — Validate what is there

Check, and record each problem rather than fixing as you go:

**The registry** — parses as JSON; has `hub`, `full`, and all six metric keys;
every entry has `url`, `favicon`, `title`; no duplicate URLs (two documents
sharing a URL means one has been silently overwriting the other).

**`LATEST.md`** — has frontmatter with `run` and `commit`; has a six-row grade
table; every one of the six metrics appears with a grade.

**Each `metric-<key>.md`** — has frontmatter with `run`, `derived_from` (the
full report it came from), and `commit`; has both a report section and an action
list, or is a valid placeholder.

**Grade agreement** — the grade for each metric in `LATEST.md` matches the grade
in that metric's own file. A mismatch means one was written against a different
audit; the newer `derived_from` wins, and the older file needs regenerating —
report which.

**Orphaned files** — a `metric-<something>.md` whose key is not one of the six.
Report it; do not delete it.

## Step 3 — Create missing metric *files*

For each of the six with no file on disk, write the placeholder defined in the
publishing contract (`### The placeholder file`). One definition, shared — do
not restate or vary it here.

**Do not publish a page for a metric that has never been analysed.** The file on
disk is cheap and the invariant wants it; a published page is a designed page,
and only `/analizeMax-metric` makes those. A metric with an empty registry `url`
is rendered by the hub as plain unlinked text saying it has not been analysed,
which is the honest state and costs nothing.

So: mint a metric URL only when its page exists and its registry entry has lost
the URL. Never mint one to fill a gap.

## Step 4 — Publish

Follow the contract's two-phase rule. It exists because the hub needs the metric
URLs and the metrics need the hub URL, so a first publish cannot know everything
it needs.

**Phase A — mint.** For every registry entry whose `url` is empty: publish with
the nav rendered as disabled `<span>`s rather than links. **Write each returned
URL into `artifacts.json` immediately** — before publishing the next one. A URL
that exists on claude.ai but not in the registry is lost, and the artifact it
points at can never be updated again.

**Phase B — link.** Every URL now known: re-publish everything that has one,
with real `href`s, correct grade badges, and correct staleness. Pass each document's registry `url`
so it updates in place; pass its registry `favicon` and `title` unchanged.

If a registry URL turns out to be unreachable, try `action: "read"` on it first.
If it genuinely cannot be read, mint a replacement, record it, and **say clearly
in the report that the old URL is orphaned** — anyone holding the old link needs
the new one.

Skip Phase A entirely when the registry is already complete; that is the normal
case and repair is then a single re-publish pass.

## Step 5 — Verify

Do not report success on the fact that publishes returned. Check:

- `artifacts.json` has no empty `url` and no duplicates.
- Every registry entry with a URL resolves to a real page — `action: "list"`
  shows them. An entry with an empty `url` is fine and expected: a metric gets
  its URL the first time its report is generated.
- Spot-check by reading the hub back: its six links are the six registry metric
  URLs, in order.
- Every grade on the hub equals the grade on the page it links to.
- Every metric page has a route back to the hub.

## Step 6 — Report

Say plainly:

- what was missing and is now created;
- what was malformed and is now fixed;
- which URLs were minted and which already existed;
- **any orphaned URL**, loudly — that is the one failure a reader cannot
  discover on their own;
- anything that needs a command you are not allowed to run (`/analizeMax` for a
  missing audit, `/analizeMax-metric <key>` for a placeholder the user wants
  filled);
- the hub URL, last, as the thing to share.

Then remind the user that `artifacts.json` should be committed — the URLs must
survive a fresh clone and their second machine, and losing them orphans every
link already shared.

Do not commit. Do not run an audit. Do not fill in a placeholder's content.

## When this goes wrong

- **No audit has ever run.** Stop at step 1. A scorecard with no grades is not
  worth publishing.
- **`LATEST.md` is missing but full reports exist.** The hub's content comes
  from `LATEST.md`, and only `/analizeMax` writes it. Report the gap and
  recommend a re-run; do not reconstruct it from the full report, that is
  re-grading by hand.
- **A grade disagrees between the hub and a metric page.** The page with the
  older `derived_from` is stale. Fix the *link and staleness*, mark that page
  `superseded`, and tell the user which `/analizeMax-metric` to re-run. Never
  edit a grade to make two files agree — that hides the staleness instead of
  showing it.
- **Two registry entries share a URL.** One has been overwriting the other, so
  one document's history is already gone. Mint a fresh URL for the newer entry,
  keep the old URL on whichever document was published there first, and say
  which one lost its history.
- **A publish is refused or fails.** Record it, finish the others, and report
  exactly which document is unpublished. A partially published set with an
  honest report beats a retry loop.
- **The user asks you to also update the content while you are in there.** That
  is `/analizeMax` or `/analizeMax-metric`. This command fixes structure; mixing
  the two makes it impossible to tell whether a changed grade came from an audit
  or from repair.
