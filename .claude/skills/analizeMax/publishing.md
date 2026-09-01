# analizeMax — publishing contract

Shared by `/analizeMax`, `/analizeMax-metric` and `/analizeMax-repair`. Any
command that writes an analysis file follows this. Read it before publishing.

The goal: **one stable URL per document, forever.** Share the scorecard once and
every link inside it keeps working, through every later audit and every metric
re-run.

---

## 1. The registry is the source of truth

`docs/analysis/artifacts.json`. Never delete it; rotation must skip it.

```json
{
  "version": 1,
  "hub":  { "url": "", "favicon": "🧭", "title": "Sol Engine Scorecard" },
  "full": { "url": "", "favicon": "🔬", "title": "Sol Engine Full Audit" },
  "metrics": {
    "stability":    { "url": "", "favicon": "🛡️",  "title": "Sol Engine Stability" },
    "architecture": { "url": "", "favicon": "🏛️",  "title": "Sol Engine Architecture" },
    "capabilities": { "url": "", "favicon": "🎮", "title": "Sol Engine Capabilities" },
    "portability":  { "url": "", "favicon": "🌍", "title": "Sol Engine Portability" },
    "devex":        { "url": "", "favicon": "🧰", "title": "Sol Engine Developer Setup" },
    "ai-tooling":   { "url": "", "favicon": "🤖", "title": "Sol Engine AI Tooling" }
  }
}
```

**Rules that keep URLs stable:**

- A URL in the registry is **permanent**. Always pass it as `url` when
  publishing that document. Publishing without `url` mints a second artifact and
  orphans the one people already have.
- An empty `url` means "not published yet". Publish, then write the returned URL
  back to the registry **immediately**, before doing anything else. A URL that
  exists on claude.ai but not in this file is lost.
- `favicon` and `title` are **fixed at creation** and never change. Users find a
  tab by its icon; a changed favicon reads as a different page.
- Never renumber, rename, or repurpose a key. If a document is retired, leave
  its entry and mark it, so its URL is never handed to something else.

Nine documents, nine URLs: the hub, the newest full report, the roadmap page,
and one per metric. Rotated older full reports stay as files only — `full`
always shows the newest. A metric's URL does not exist until its report is first
generated, so fewer than nine being filled is a normal state, not a gap.

## 2. The circular-link problem

The hub links to whichever metric pages exist; every metric page links back to
the hub and sideways to the others. So a publish that mints more than one
document cannot know all the URLs it needs.

Resolve it in two phases, and only ever on a first run or a repair:

**Phase A — mint.** For every registry entry with an empty `url`, publish the
document with the nav rendered as **disabled placeholders** (plain `<span>`, not
`<a>`). Record each returned URL immediately.

**Phase B — link.** Every URL is now known. Re-publish everything that has one,
with real `href`s.

After Phase B the registry is complete, so every later run is a single publish
per changed document. Only `/analizeMax-repair` should ever need Phase A again.

## 3. Who publishes what

| Command | Writes | Publishes |
|---------|--------|-----------|
| `/analizeMax` | full report, `LATEST.md`, `PLAN.md` | `full` and `hub`. Metric pages only for the metrics its argument selected, and only by **invoking `/analizeMax-metric`** — never by writing one itself. |
| `/analizeMax-metric <m>` | `metric-<m>.md` | that metric, **and `hub`** — its row's freshness just changed |
| `/analizeMax-repair` | any malformed file | only what is broken |

**`/analizeMax` never writes a metric page itself.** Not to refresh a grade,
not to update a freshness label, not "while it is in there". A metric page costs
a designed page's worth of work, and the point of splitting `/analizeMax-metric`
out was to stop paying that for six pages nobody asked to read. An audit that
regenerates them all by default has quietly undone the split.

It may generate some **on request** — `/analizeMax core`, `/analizeMax max`, or
named metrics — but only by invoking `/analizeMax-metric`, once per metric. One
writer for that file, always. An audit that reimplements the metric report is
how the two documents start disagreeing about their own format.

It may **read** the metric files — it needs their `derived_from` to label the
hub. Reading is free; publishing is not.

**Always refresh the hub**, from every command that changes what the hub shows.
It is one small page and it is the thing people actually open.

### What the hub says about a metric it did not regenerate

This is the part that makes the rule safe. The hub carries the new grade; the
metric page still carries the old one. That is not a contradiction as long as
the hub says so, and each row has exactly three states:

| Registry `url` | Page's `derived_from` | Hub renders |
|----------------|-----------------------|-------------|
| empty | — | **Not analysed** — plain text, *not a link*. Nothing was published, so there is nothing to link to. Name `/analizeMax-metric <key>` as what produces it. |
| set | the newest full report | **Current** — a link, no caveat. |
| set | an older full report | **Superseded** — a link, labelled with the audit it came from, so a reader knows the detail is older than the grade beside it. |

An unlinked row is the honest rendering of "no page exists", and it costs
nothing. The hub's stylesheet already has a `dead` variant for exactly this.
Never link a row whose registry `url` is empty; a dead-end from a shared
scorecard is worse than a row that admits it has no detail yet.

## 4. Staleness is computed, never remembered

Every page shows its own freshness. Compute at publish time:

```bash
git rev-parse HEAD
git rev-list --count <the-doc's-commit>..HEAD
```

- **Hub** — the audit's commit, its timestamp, and how many commits have landed
  since. If more than zero, a banner at the top says so in one plain sentence.
- **Metric page** — which audit it derived from, plus whether that audit is
  still the newest. Three states, and the page must say which:
  - `current` — derived from the newest full report, which is at `HEAD`.
  - `behind` — derived from the newest full report, but commits have landed
    since. Findings may already be fixed.
  - `superseded` — a newer full report exists than the one this derived from.
    Re-run `/analizeMax-metric` for this one.
- **Never-run metric** — say so plainly and name the command that fixes it.

State it in words, not only a colour. A shared page is read by someone who does
not know the convention.

## 5. Page structure

Author **HTML**, not markdown — these pages exist to be navigated, and real
links are the point. The markdown in `docs/analysis/` stays the repo-side source
of truth; the artifact is its shareable view. Same content, no divergence.

Follow the `artifact-design` skill for the visual pass. Beyond that, three
things are fixed across every published page so navigation feels like one
document:

**A. The nav strip** — top of every page, hub included:

- On the hub: the six metrics as chips, plus a **Roadmap** chip. A metric whose
  registry `url` is empty still gets a chip, but as plain unlinked text — there
  is no page to link to, and a dead link from a shared scorecard is worse than a
  chip that admits it has no detail yet.
- On a metric page: the same six chips with the current one marked as current
  (not a link), plus a **← Scorecard** link at the strip's left, and a second
  one at the bottom of the page. Someone who has scrolled a long action list
  should not have to scroll back up.
- A chip shows the metric's grade badge, so the strip is itself a scorecard.

**B. The grade badge** — one component, identical everywhere: the letter, and a
band colour. Semantic colour only (F/D critical, C caution, B/A good) and never
the page's accent, so a grade never reads as decoration.

**C. The footer** — on every page: which document this is, its own timestamp,
the audit commit it came from, and links to the hub and the full report.

### The hub

The scorecard *is* the hub. Content comes from `LATEST.md`:

1. Staleness banner, if behind.
2. The two-sentence state of the engine.
3. **The scorecard table** — one row per metric: name, grade badge, the
   one-sentence summary, and a link to that metric's page. Each row also shows
   the metric page's freshness (`current` / `behind` / `superseded` /
   `not yet run`) with its date and time.
4. What this means · What games this can build today · The three things worth
   doing next.
5. A link to the full report page.

The table is the navigation. Every grade is a link.

### A metric page

Content from `metric-<key>.md`:

1. Nav strip, staleness line, grade badge.
2. The 1–2 page report.
3. The full ordered action list. Group it visually by the `Status` field —
   `in PLAN.md`, `plan-eligible`, `needs a decision` — because those are three
   different kinds of work and a flat list hides that. Keep the execution
   ordering inside each group.
4. Back to the scorecard.

### The placeholder file

A metric with no report yet still needs a file on disk, because the hub links to
a page and that page has to come from somewhere. Write it exactly like this —
both `/analizeMax` and `/analizeMax-repair` use this one definition:

```markdown
---
run: <now>
derived_from: <newest full report filename>
commit: <the audit's commit>
state: empty
---

# <Metric name>

**Grade: <grade from LATEST.md>** — <the one-sentence summary from LATEST.md>

No detailed report has been generated for this metric yet.

Run `/analizeMax-metric <key>` to produce the 1–2 page report and the full
ordered action list. It derives from the audit of <date> and takes seconds — no
build, no research.
```

`state: empty` is what lets every other command tell a placeholder from a real
report without parsing the body. A real report sets `state: report`, and
overwriting a placeholder with one is normal — that is the file becoming real.

Never write a placeholder when no audit has run: without a grade there is
nothing to put on it, and six blank pages are worse than none.

### An empty metric page

Never a blank page. It still knows the grade — the hub has it. So:

1. Same nav strip and grade badge as any other metric page.
2. The grade and its one-sentence summary from the audit.
3. One line: no detailed report has been generated for this metric yet, and
   `/analizeMax-metric <key>` produces it.
4. Back to the scorecard.

A reader following a link from a shared scorecard lands somewhere that makes
sense and can get back.

## 5b. The roadmap page

Owned by `/roadmap`, not by the audit — but it shares this registry and sits in
the same nav, so its layout is defined here with everything else.

**One artifact, `roadmap` in the registry.** All 21 ENGINE_MAP categories are
**tabs inside the single page**, not 21 separate artifacts. Twenty-one URLs to
mint, track and keep in sync — for what is fundamentally one table — would cost
more than it could ever return, and every one of them would go stale on any row
flip.

Generated from `docs/ENGINE_MAP.md` and `docs/ROADMAP.md`. Those two files are
the source of truth; this page never holds a fact they do not.

**Structure:**

1. **Nav strip**, same component as everywhere else, with **← Scorecard** back
   to the hub.
2. **A summary line** across all categories: how many Done, Ready, Later, Far.
   That is the honest shape of the project in one row.
3. **Category tabs** — 21 of them, client-side (no navigation, no page reload).
   Label each with its number and name as ENGINE_MAP writes it, and show its
   Ready count on the tab, so the tabs with available work are visible without
   clicking through. Default to the first category with a Ready row.
4. **Inside a tab**, the category's rows in map order: number, item text, a
   status chip, and Finish first where the map fills it. Status chips reuse the
   grade badge's semantic colours — Done, Ready, Later, Far — never the accent.
5. For a **Done** row that has a `docs/ROADMAP.md` decision-log entry, show its
   Why / Choice / Gate / Do-not, collapsed. That log is the most valuable thing
   in the repository and nothing else surfaces it.
6. **Footer** with the source commit and generation time, plus links to the hub.

Tabs must work without JavaScript succeeding: render every panel in the document
and let CSS/JS switch which is visible, rather than building panels on click. A
page that shows nothing when a script fails is worse than a long page.

**Hub integration.** The scorecard's nav strip carries a **Roadmap** item beside
the six metrics. It is not a seventh metric and takes no grade badge — give it
the Ready count instead. Any command that re-publishes the hub keeps that item;
dropping it silently orphans the roadmap page from the only link people share.

## 6. Verify before you finish

- Every `href` to another analysis page resolves to a registry URL, and no page
  links to a URL not in the registry.
- The registry has no empty `url` left.
- Every grade on the hub matches the grade on the metric page it links to. A
  mismatch means a page was published from a stale read — re-publish it.
- Every page has a route back to the hub.
- The favicon and title of each page match the registry.

## 7. A note on the registry in a public repo

`artifacts.json` holds artifact URLs, and this repository is public. Artifacts
are private until explicitly shared, so a URL alone grants nothing — but it is
still a URL in public. It is committed anyway, because the URLs must survive a
fresh clone and a second machine, and losing them orphans every artifact anyone
has already been given. If that trade is ever unwanted, gitignore the file and
accept that a fresh clone mints new URLs.
