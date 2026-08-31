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

Eight documents, eight URLs: the hub, the newest full report, and one per
metric. Rotated older full reports stay as files only — `full` always shows the
newest.

## 2. The circular-link problem

The hub links to six metric pages; every metric page links back to the hub and
sideways to the other five. So a first publish cannot know all the URLs it
needs.

Resolve it in two phases, and only ever on a first run or a repair:

**Phase A — mint.** For every registry entry with an empty `url`, publish the
document with the nav rendered as **disabled placeholders** (plain `<span>`, not
`<a>`). Record each returned URL immediately.

**Phase B — link.** Every URL is now known. Re-publish all eight with real
`href`s.

After Phase B the registry is complete, so every later run is a single publish
per changed document. Only `/analizeMax-repair` should ever need Phase A again.

## 3. Who publishes what

| Command | Writes | Publishes |
|---------|--------|-----------|
| `/analizeMax` | full report, `LATEST.md`, `PLAN.md` | `full`, `hub`, **and re-publishes all six metric pages** — their grades and staleness just changed |
| `/analizeMax-metric <m>` | `metric-<m>.md` | that metric, **and `hub`** — its row's timestamp and freshness just changed |
| `/analizeMax-repair` | any missing file | whatever is missing or malformed, both phases |

**Always refresh the hub.** It is small, and a hub showing a grade the metric
page contradicts is worse than no hub. This is not optional.

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
things are fixed across all eight pages so navigation feels like one document:

**A. The nav strip** — top of every page, hub included:

- On the hub: the six metrics as chips, each linking to its page. A metric with
  no report yet still gets a chip and still links — its page exists and says it
  is empty.
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

### An empty metric page

Never a blank page. It still knows the grade — the hub has it. So:

1. Same nav strip and grade badge as any other metric page.
2. The grade and its one-sentence summary from the audit.
3. One line: no detailed report has been generated for this metric yet, and
   `/analizeMax-metric <key>` produces it.
4. Back to the scorecard.

A reader following a link from a shared scorecard lands somewhere that makes
sense and can get back.

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
