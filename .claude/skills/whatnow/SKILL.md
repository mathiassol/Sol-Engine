---
name: whatnow
description: Work out what to do next on this engine and suggest three genuinely different directions — not one row, but areas, with the evidence for each. Reads git state, the gates and invariants, CI, the backlog's dependency graph, and the newest audit. Cheap and fast; it suggests and does not start work. Use when the user asks what to work on, what is next, or where to spend today.
when_to_use: The user is deciding what to do and wants a read on the whole system. Trigger phrases include "what now", "what should I work on", "what's next", "where should I spend today", "give me options", "I don't know what to do". Not an audit — that is /analizeMax. Not a single row — that is /roadmap.
argument-hint: [optional: an area to bias toward, e.g. rendering, tooling, stability]
disallowed-tools: Write Edit NotebookEdit Artifact
allowed-tools: Bash(git *) Bash(gh *) Bash(ls *) Bash(cat *) Bash(grep *) Bash(sed *) Bash(awk *) Bash(wc *) Bash(find *) Bash(./build/bin/Debug/sandbox.exe *) PowerShell Read Grep Glob
---

Read the system, then propose **three different directions** the user could take
right now, with the evidence for each.

**This skill suggests. It does not start work.** No edits, no commits, no
publishing — the tool grants enforce that. If the user picks one, they will say
so, and then the right tool is `/roadmap` or ordinary work.

Keep it cheap: **do not build, do not search the web.** `/analizeMax` is the
expensive one that measures everything. This one answers in a minute from what
is already on disk.

---

## Step 1 — Gather, in this order

Cheapest and highest-signal first. Stop early only if something is on fire.

**1. Unfinished work — this outranks everything.**

```bash
git status --porcelain
git log --oneline -8
git log origin/main..HEAD --oneline
git stash list
```

Uncommitted source, unpushed commits, a stash — the strongest possible answer to
"what now" is *finish what you started*. Also note what the last few commits
touched: momentum is real, and a warm area is cheaper to work in than a cold one.

**2. Is anything actually broken?**

```powershell
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

**3. The backlog, as a graph.**

Read `docs/ENGINE_MAP.md`. Per-category `N done · M ready` lines say where work
is available. Then find the leverage — which Ready rows the most Later rows wait
on:

```bash
grep -c "Renderer #16" docs/ENGINE_MAP.md
```

One `grep` per candidate row; every hit outside its own row is something blocked
on it. Roughly half the Ready rows unblock nothing, so this is what separates a
row that makes future work cheaper from a row that is merely available.
`docs/PICKING.md` holds the choosing rules and a symptom-to-area table.

**4. The last audit, and how stale it is.**

`docs/analysis/LATEST.md` — six grades and a one-line reason each. Compare its
`commit:` against `HEAD`; if many commits have landed, weight it down and say so.
`docs/analysis/PLAN.md` — any unexecuted tasks are pre-approved work sitting
ready, which is a cheap and honest suggestion.

**5. Designed but not built.**

```bash
grep -l "^Status: spec" docs/superpowers/specs/*.md
```

A spec at `Status: spec` is a decision already made and not yet implemented —
often the cheapest real work available.

**6. What the tree itself says.**

Bounded, not exhaustive: `TODO`/`FIXME`/`HACK` under `packages/`, and the
Do-not lines in `docs/ROADMAP.md` for the areas you are about to suggest — those
record decisions already taken, and suggesting against one without knowing is
how you waste the user's afternoon.

If an argument was given, bias toward that area — but still say plainly if
something outside it is more urgent.

## Step 2 — Decide what is actually urgent

This is the part that goes wrong. Given permission to look for critical
problems, the honest count is almost always zero, and the tempting count is
forty.

### The test

Something is urgent **only if you can point at it happening.** One of:

- **Red** — the build fails, a gate is red, CI is red, or the debug layer is
  talking. Not "might"; is.
- **Wrong** — it is shipping incorrect output or losing work, *silently*. The
  colour-space defect was this: pictures looked plausible and the maths was
  wrong, and nothing would have caught it.
- **Compounding** — every day of delay measurably raises the cost, and you can
  say why in one sentence. A formatter config that disagrees with the tree gets
  worse with every file added.
- **Choking** — it blocks more work than anything else, by a count you can show.

### The cap

**At most one item per run may be called urgent. Usually none is.**

If two things feel urgent, you have not finished deciding: rank them and name
the first. "Nothing is urgent, here are three good directions" is the normal,
correct answer and should be said without hedging or apology.

### Never urgency

- **Absence is not urgency.** A missing feature is a roadmap row, however large
  the hole. No text rendering is not an emergency; it is UI #2.
- **A grade is not urgency.** C+ on a dimension describes the tree; it does not
  mean anything is on fire.
- **A Later or Far row is never urgent.** By definition something else comes
  first.
- **"Could become a problem" is not urgency.** Neither is "best practice",
  "industry standard", or "technical debt" with no named cost.
- **Tell:** if you have written "should" three times in one item, you left
  urgency behind and are describing a preference. Move it to an ordinary
  suggestion.

## Step 3 — Propose three directions

Areas, not single rows. "Work the renderer's lighting cluster — spot lights,
emissive, debug views" is a direction. "Do Renderer #30" is an instruction, and
the user asked what to *consider*.

**The three must differ in kind.** Pick from at least two of these, ideally
three — three flavours of the same thing is not three options:

| Kind | What it is |
|------|-----------|
| **Build** | Roadmap rows: one category, or a cluster across two that share a foundation |
| **Strengthen** | Make what exists harder to break: a ceiling, a silent failure, a missing gate |
| **Smooth** | Reduce friction: setup, CI, docs, AI tooling, repository hygiene |
| **Decide** | An open judgement call that is blocking future work, where the deliverable is the decision |

Each direction gets, in a few lines:

- **A name** for the area, as the user would say it.
- **Why now** — the evidence, from Step 1. Numbers where you have them.
- **What it looks like** — two or three example rows or actions, explicitly as
  illustrations of the shape, not a prescription.
- **Rough size** — an afternoon, a few days, a week. Say if you are guessing.
- **What it unblocks**, when it unblocks something. Skip the line when it does not
  rather than inventing a benefit.

## Step 4 — Say it briefly

In chat, not a file. Around 350 words, and no more than 500.

1. **One line on the state.** What is green, what is red, how stale the audit
   is. If something is urgent, it goes here and it goes first.
2. **The three directions.**
3. **One line on what not to do now** — and mean it. A stale audit, a row whose
   blocker has not landed, a second thing started before the first is finished.
   This line is often the most useful one in the answer.

No table of everything you read. No grades unless a grade is the evidence for a
suggestion. The user asked what to do, not for a report.

## When this goes wrong

- **Everything is green and the backlog is wide open.** That is a good day, not
  a failure of the skill. Lead with leverage: the rows that unblock the most.
- **You want a fourth suggestion.** Three is the point. A fourth means you have
  not ranked.
- **The three all came out as "build".** Re-read Step 3 — you skipped the
  strengthen and smooth kinds, and one of them is usually where the cheapest
  real win is.
- **Uncommitted work is in the tree.** Lead with it. Do not suggest starting
  something new over unfinished work without saying that is what you are doing.
- **The binary is stale, so you did not run the gates.** Say so in the state
  line. Do not present unverified green as green.
- **The audit is many commits old.** Its grades still describe the tree it ran
  on; weight them down, say how far behind it is, and consider suggesting a
  fresh `/analizeMax` as one of the three if enough has changed.
- **You are about to write "critical".** Re-read the test in Step 2. If it does
  not pass, the word is wrong and the item is ordinary work.
