# Picking the next row

[ENGINE_MAP.md](ENGINE_MAP.md) is the list. This file is only how to **choose**
from it, and what to do before you start.

It deliberately holds no statuses and no row list. The map is the only copy of
those — a second copy here is how the previous version of this file ended up
sixty percent out of date.

---

## Before you start

- **One row at a time.** Finish it and ship it before picking another.
- **One gate.** A plain function in `packages/sandbox/src/main.cpp` asserting on
  real values, plus a visible sandbox check with `ENGINE_GPU_DEBUG=1` if it
  touches the GPU. See the gate protocol in [../CLAUDE.md](../CLAUDE.md).
- **Write the gate first and watch it fail.** A gate that has never been red
  proves nothing.
- **Do not scaffold an empty package.** Put new work behind `foo` / `foo-*`
  when you actually start it, per [packageRules.md](packageRules.md).
- **Stay inside the row.** Something else will look wrong while you are in
  there. Note it, finish the row, mention it at the end.

`/roadmap <category> #<id>` does all of this — research, one question round,
build, gate, ship — in a single run.

## Choosing among the Ready rows

There are more Ready rows than there used to be, so "pick a Ready row" is no
longer much of an instruction. In order:

1. **Does it unblock the most?** Prefer the row that the largest number of
   Later rows are waiting on. That is the only choice that makes *future*
   choices cheaper, and the map records it — search for a row's reference to see
   what waits on it:

   ```bash
   grep -n "Renderer #16" docs/ENGINE_MAP.md
   ```

   Every hit outside its own row is something blocked on it. A row with no hits
   unblocks nothing; roughly half of the Ready rows are in that bucket, and they
   are the ones to leave for a quiet afternoon.

2. **Is it the thing you keep noticing?** The table below maps a symptom to
   where it lives. A row you have wanted three times is worth more than a row
   that is merely well-placed.

3. **Can the sandbox demonstrate it?** A row you can see on screen or assert in
   a gate is a row you can prove. One you cannot is a row you will have to trust.

4. **At equal leverage, cheap and safe first.** Then an interrupted run still
   leaves the tree better off.

Each category heading in the map carries its own `N done · M ready` line, so
scanning for where the available work is takes one read.

## Where to look when something feels wrong

Symptoms to rows. This table says *where*, never *whether* — the map holds the
status, so this cannot go stale.

| You are feeling this | Look at |
|----------------------|---------|
| No text, no menu, no score, no dialogue | UI #2, then UI #6 layout |
| Lighting looks flat or wrong | Renderer #8 PBR, #9 IBL, #28 colour space |
| Only the sun and a few point lights | Renderer #30 spot lights, then #17 clustered |
| Shadows look like stamps at distance | Renderer #15 cascades |
| Cannot see through anything | Renderer #16 alpha |
| The frame is a black box | Renderer #32 debug views, Debug #5 graph dump |
| Characters do not move their limbs | Assets #10 skins, then Animation #1 |
| Too few physics bodies, or it asserts | Physics #8 capacity |
| Boxes and capsules are not enough | Physics #10 convex hulls |
| Art does not fit in graphics memory | Assets #5 BC7, Assets #15 LOD |
| Load hitches, or one core does everything | Jobs #2 pool, Assets #14 async |
| No ground to walk on beyond a plane | World #3 terrain |
| Nothing to point a gun at | AI #1 navmesh, then AI #2 steering |
| No hits, sparks or dust | VFX #1 particles |
| Sound does not come from anywhere | Audio #3 positional |
| A crash in a shipped build leaves nothing | Foundation #7 minidump, Build #7 log location |
| Cannot tell where memory went | Foundation #12 tracked allocators |
| It does not build anywhere but this machine | Build #13 Linux CI, Build #14 presets, Build #16 setup check |
| A player cannot install it | Build #8 installer, Build #15 release workflow |
| Editing the demo in C++ hurts | Change the C++. **Not** an in-engine inspector — see Editor #1 |

## What not to pick

- **A Later row.** Its Finish first names what has to land before it, or a wall
  that has not been hit. Starting it anyway means building on something that
  does not exist yet.
- **A Far row.** Valid engine work; wait until a game actually hurts without it.
- **An Editor row.** The engine is not the editor. Demo games are C++ in the
  sandbox for a long time yet.
- **A graphics paper.** If the map does not have it and nothing hurts without
  it, it is not the next row.

## When the map itself looks wrong

That is a finding, not a blocker. `map-dependencies` in
`tools/check-invariants.ps1` checks the graph on every run — every
`Category #N` reference resolves, no Later row has only Done blockers, and no
two rows block each other. If a row seems mis-statused and the checker is
green, the status is a judgement call and you can change it; say so in the
commit.

Row numbers are permanent ids and are not sequential. Row order inside a
category is implementation order. Both rules, and how **Finish first** works,
are documented at the top of [ENGINE_MAP.md](ENGINE_MAP.md).
