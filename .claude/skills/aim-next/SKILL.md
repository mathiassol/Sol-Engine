---
name: aim-next
description: Work out what to do next and suggest three genuinely different directions, with the evidence for each. One `aim whatnow` call supplies audit staleness, the open findings with their severity and whether any check covers them, the Ready rows ranked by how much they unblock, and the dependency graph's verdict; this skill adds the local state a server cannot see — uncommitted work, invariants, whether the gate binary is stale — and does the judgement. Cheap, fast, and it suggests without starting anything.
when_to_use: The user is deciding where to spend time and wants a read on the whole system. Trigger phrases include "what now", "what should I work on", "what's next", "where should I spend today", "give me options", "I don't know what to do". Not an audit — that is /aim-audit. Not a single row — that is /aim-row.
argument-hint: "[optional: an area to bias toward, e.g. rendering, tooling, stability]"
disallowed-tools: Write Edit NotebookEdit Artifact
allowed-tools: Bash(git *) Bash(gh *) Bash(ls *) Bash(cat *) Bash(grep *) Bash(sed *) Bash(awk *) Bash(wc *) Bash(find *) Bash(pwsh *) Bash(./build/bin/Debug/sandbox.exe *) PowerShell Read Grep Glob
---

Read the system, then propose **three different directions**, with evidence.

**This skill suggests. It does not start work.** No edits, no commits — the tool
grants enforce it. If the user picks one they will say so, and then the right
tool is `/aim-row` or ordinary work.

Keep it cheap: **do not build and do not search the web.** `/aim-audit` is the
expensive one. This answers in about a minute.

---

## Step 1 — Ask the server what it already knows

```bash
pwsh -NoProfile -File tools/aim.ps1 whatnow
```

One call returns everything that used to be reconstructed by hand every session,
and it is the reason this skill is short:

- **the newest audit**, its grades, and **how stale it is** against `HEAD` —
  computed from the audit's `commit_sha`, so it is never a guess
- **`open_findings`** — every finding from that audit with its `code`,
  `severity`, and **`covered_by_check`**. Read these. They are the audit's most
  actionable output and the cheapest evidence in the whole payload
- **the Ready rows ranked by `unblocks`** — the count of rows that
  *transitively* wait on each one, plus `ready_unblocking_nothing`, which is
  usually over half of them
- **the graph's verdict** — cycles, stale blockers, dangling refs, plus
  `available_but_blocked` (a Ready row that still names a blocker) and
  `later_without_reason` (a Later row naming neither a row nor a wall). Both are
  contradictions in the backlog rather than work, and worth one line if non-empty
- **`the_bar`** — the project's own standard. Judge against that, not against
  what is reasonable for its size

**A caution on `open_findings`: "open" means "present in the newest audit", not
"still true".** Nothing marks a finding fixed, so one you closed an hour after
the audit still appears here. Check anything you are about to lead with against
the tree before repeating it.

Take those numbers as given. Do not re-derive leverage with `grep`: the ad-hoc
version of that count read anti-dependency disclaimers as edges and reported two
false dependency loops, which were then investigated as real. The server walks
the actual graph and has negative-control tests.

If the command fails, say the server is down, then fall back to reading
`docs/ENGINE_MAP.md` directly — and say in your answer that the leverage numbers
are eyeballed rather than computed.

## Step 2 — Gather what the server cannot see

**1. Unfinished work. This outranks everything.**

```bash
git status --porcelain
git log --oneline -8
git log origin/main..HEAD --oneline
git stash list
```

Uncommitted source, unpushed commits, a stash — the strongest possible answer to
"what now" is *finish what you started*. Note what the last few commits touched:
momentum is real, and a warm area is cheaper to work in than a cold one.

**2. Is anything actually broken?**

```bash
pwsh -NoProfile -File tools/check-invariants.ps1
```

Four seconds, no compiler. Then the gates — **but only if the binary is not
stale.** Compare `build/bin/Debug/sandbox.exe` against the newest file under
`packages/`. If the binary is older, say you did not run them rather than
building; a stale binary has produced a confidently wrong answer here before.

```bash
./build/bin/Debug/sandbox.exe --gates
gh run list --limit 3
```

**3. Work that is already decided.**

```bash
grep -l "^Status: spec" docs/superpowers/specs/*.md
```

A spec at `Status: spec` is a decision made and not yet implemented — often the
cheapest real work available. Same for unexecuted tasks in
`docs/analysis/PLAN.md`: those are pre-approved.

**4. What the tree itself says.** Bounded, not exhaustive: `TODO`/`FIXME`/`HACK`
under `packages/`, and the **Do not** lines in `docs/ROADMAP.md` for any area you
are about to suggest. Those record decisions already taken, and suggesting
against one without knowing is how you waste the user's afternoon.
`aim roadmap row <cat>/<id>` prints a row's Do-not lines directly.

If an argument was given, bias toward that area — but still say plainly if
something outside it is more urgent.

## Step 3 — Decide what is actually urgent

This is the part that goes wrong. Given permission to look for critical
problems, the honest count is almost always zero and the tempting count is
forty.

**Something is urgent only if you can point at it happening.** One of:

- **Red** — the build fails, a gate is red, CI is red, or the debug layer is
  talking. Not "might"; is.
- **Wrong** — it ships incorrect output or loses work, *silently*. The
  colour-space defect was this: pictures looked plausible, the maths was wrong,
  and nothing would have caught it.
- **Compounding** — every day measurably raises the cost, and you can say why in
  one sentence.
- **Choking** — it blocks more work than anything else, **by the `unblocks`
  count from Step 1**, not by impression.

`open_findings` is evidence for this test, never a verdict on it. A `critical`
finding, or a `high` one with `covered_by_check: false`, is the strongest
candidate the payload offers — a serious problem that nothing would catch is
exactly the **Wrong** case. But confirm it against the tree first: the audit may
be many commits old, and the fix may have landed. A finding alone is not urgency.

**At most one item per run may be called urgent. Usually none is.** If two feel
urgent you have not finished deciding: rank them and name the first. "Nothing is
urgent, here are three good directions" is the normal, correct answer and should
be said without hedging.

**Never urgency:**

- **Absence is not urgency.** A missing feature is a roadmap row, however large
  the hole. No text rendering is not an emergency; it is UI #2.
- **A grade is not urgency.** C+ describes the tree; nothing is on fire.
- **A finding count is not urgency.** Twenty findings is a normal audit, not
  twenty fires. Severity and `covered_by_check` are what separate them.
- **A Later or Far row is never urgent.** By definition something comes first.
- **"Could become a problem"** is not urgency. Neither is "best practice" or
  "technical debt" with no named cost.
- **Tell:** three uses of "should" in one item means you left urgency behind and
  are describing a preference.

## Step 4 — Propose three directions

Areas, not single rows. "Work the renderer's lighting cluster — spot lights,
emissive, debug views" is a direction. "Do Renderer #30" is an instruction, and
the user asked what to *consider*.

**The three must differ in kind.** Use at least two of these, ideally three:

| Kind | What it is |
|------|-----------|
| **Build** | Roadmap rows: one category, or a cluster across two sharing a foundation |
| **Strengthen** | Make what exists harder to break: a ceiling, a silent failure, a missing gate. **The `high` findings with `covered_by_check: false` are this list** — you do not have to invent it |
| **Smooth** | Reduce friction: setup, CI, docs, AI tooling, repository hygiene |
| **Decide** | An open judgement call blocking future work, where the deliverable is the decision |

Each gets a few lines: **a name** as the user would say it; **why now** with the
numbers from Steps 1–2; **what it looks like** — two or three example rows,
explicitly as illustrations of the shape; **rough size** (an afternoon, a few
days, a week — say if you are guessing); and **what it unblocks**, using the real
count, skipping the line entirely when it unblocks nothing rather than inventing
a benefit.

## Step 5 — Say it briefly

In chat, not a file. Around 350 words, 500 at the outside.

1. **One line on the state** — what is green, what is red, how stale the audit
   is. Anything urgent goes here, first.
2. **The three directions.**
3. **One line on what not to do now**, and mean it. Often the most useful line
   in the answer.

No table of everything you read. No grades unless a grade is the evidence for a
suggestion.

## When this goes wrong

- **Everything is green and the backlog is wide open.** A good day, not a
  failure. Lead with leverage: the rows with the highest `unblocks`.
- **You want a fourth suggestion.** Three is the point. A fourth means you have
  not ranked.
- **The three all came out "build".** You skipped strengthen and smooth, and one
  of those is usually the cheapest real win.
- **Uncommitted work is in the tree.** Lead with it. Never suggest starting
  something new over unfinished work without saying that is what you are doing.
- **The binary is stale so you did not run the gates.** Say so in the state
  line. Unverified green is not green.
- **The audit is many commits behind.** Its grades describe the tree it ran on.
  Weight them down, say how far back it is, and consider offering a fresh
  `/aim-audit` as one of the three.
- **You are about to write "critical".** Re-read Step 3. If it does not pass the
  test, the word is wrong and the item is ordinary work.
