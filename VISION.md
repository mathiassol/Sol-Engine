# Vision — the finished product

**This file is the target.** Not the plan, not the backlog, not the current
state. It describes Sol Engine in a stable 1.0 release, and it exists so that
no session, human or agent, has to guess what this is being built toward.

It is the **single owner** of that question. [Scaffold.md](Scaffold.md) records
where the project started and is frozen. [Philosophy.md](Philosophy.md) is how
to build. [docs/ROADMAP.md](docs/ROADMAP.md) is why the tree looks the way it
does. [docs/ENGINE_MAP.md](docs/ENGINE_MAP.md) is what is left to do. None of
them restate the goal — they point here.

Read it before making an architectural call. It is deliberately short enough to
read every time.

---

## What it is

A general-purpose game engine in the Unity / Godot / Unreal category, written
in C++20, targeting a **full open world at high fidelity** — a big world, good
graphics, advanced systems. The reference game is an open-world action RPG at
the fidelity of a current commercial release.

Streaming is assumed, not deferred. Entity counts are in the hundreds of
thousands. A level is not a thing that loads whole.

## The wedge

Each of the big three gets something wrong, and the gap between them is the
opportunity:

- **Unreal** is powerful and locked. The capability is real; the price is that
  you work the way Unreal works.
- **Unity** is more open, and older than it can afford to be.
- **Godot** is genuinely open — the thing to learn from — but its feature set
  stays primitive and it is not trying to compete on large, high-quality games.

But "open like Godot, powerful like Unreal" is not the pitch. Text scene files
are table stakes; Godot already has them. The pitch is:

> **An engine an agent can drive as well as a human can.**

Nothing in the big three can claim it. Unreal's Python editor scripting is
bolted onto the editor, Godot has no external editor control at all, Unity's is
partial. A user lives inside the editor; everything the editor can do is also a
file and a command, so an agent has the same reach a person does.

That is the property to protect when a decision is close.

## What it is not

The list that prevents drift. Each of these is a deliberate exclusion, not an
oversight or a "later":

- **Not an editor-first product.** The editor is essential and never the only
  way in. Any capability reachable only through the UI is a bug.
- **Not a visual scripting language.** No Blueprint equivalent, no node graph
  for game logic. Lua is the scripting layer.
- **Not a closed workflow.** The engine does not know the editor exists. A
  third-party editor is a supported outcome, not a hypothetical.
- **Not an asset marketplace or launcher.** A player unzips a build and runs it.
- **Not consoles for 1.0.** Windows first; macOS and mainstream Linux are
  designed for and reached through `platform-*` packages.
- **Not an in-engine inspector.** Engine ≠ editor stays true; the editor is a
  separate application layered on open APIs.
- **Not a research renderer.** A graphics paper with no game that hurts without
  it is not on the path.

## The decisions that carry it

Settled in
[docs/superpowers/specs/2026-09-03-editor-architecture-design.md](docs/superpowers/specs/2026-09-03-editor-architecture-design.md),
which holds the reasoning. Summarised here because these are the ones most
likely to be quietly violated:

| | Decision |
|---|---|
| **D1** | Everything authored is plain text, and every editor operation is a command issuable without the editor |
| **D2** | Layer and shippability are two axes: every package is tagged `runtime` or `tools`, and there is no "engine layer that does not ship" |
| **D3** | The command layer is the spine. Editor UI, CLI, Lua and agent are all clients; none can bypass it |
| **D4** | Commands are editor-time only. The cooked runtime is a separate fast path |
| **D5** | Entity plus components, and the file format is deliberately not the runtime layout |
| **D6** | Lua, script-as-component, and nothing ticks by default |
| **D7** | The editor UI technology is a late, reversible decision |

## Where the tree contradicts this today

The honest gap, and the point of keeping it in this file: these are not
"missing features," they are **decisions already made the other way** that have
to be undone. The `vision-gap` invariant checks every row, so this table cannot
silently go stale — when a contradiction is resolved, the check fails until the
row is removed.

A number in that table is a **limit, not a hard wall** — the arrays can be made
bigger, and the instance and material caps have been, to import a scene the
engine was not built for. Raising one does not resolve its row: the
contradiction is that there is a fixed ceiling to name at all, not that the
ceiling is low. Nor is raising one free — each cap is load-bearing somewhere
else, and the cost has to be paid in the same commit.

| Contradiction | Lives in | Symbol |
|---|---|---|
| 3,072 instances per world | `packages/scene/include/engine/scene/world.hpp` | `kMaxInstances` |
| 128 materials per world | `packages/scene/include/engine/scene/world.hpp` | `kMaxMaterials` |
| 4 point lights | `packages/scene/include/engine/scene/world.hpp` | `kMaxPointLights` |
| 31-character entity names | `packages/scene/include/engine/scene/world.hpp` | `kMaxNameChars` |
| 256 rigid bodies | `packages/physics/include/engine/physics/physics.hpp` | `kMaxBodies` |
| Motion history keyed by instance index | `packages/renderer/include/engine/renderer/motion.hpp` | `kHistorySlots` |

Two further contradictions have no symbol to name, and so are not machine-checked:

- **Nothing can be despawned.** `scene::World` has no `remove_instance`,
  `destroy_instance` or `clear`. The only removal is replacing the whole world.
- **A scene is one file.** `.solscene` is monolithic, so two people editing one
  world is a merge conflict and an agent must rewrite the whole file to change
  one entity.

## How to tell whether we are drifting

Four questions. Any "no" is a drift signal worth stopping for:

1. **Can it be done from a file and a command, without the editor?** If a new
   capability is UI-only, D1 is broken.
2. **Does it survive 200,000 entities and streaming?** If it assumes the world
   is resident, it is a demo feature.
3. **Would a third party be able to build this on the open APIs?** If it needs
   private access, the boundary is in the wrong place.
4. **Is there a gate that would catch it breaking?** The verification culture is
   not separate from the vision; it is what makes a small team's engine
   trustworthy enough to be an option next to the big three.

## What changes this file

Only a deliberate decision about the product. Not a feature landing, not a
grade moving, not a row shipping — those are
[docs/ROADMAP.md](docs/ROADMAP.md) and
[docs/ENGINE_MAP.md](docs/ENGINE_MAP.md). If this file needs editing because
the tree went somewhere else, that is the drift it exists to catch: fix the
tree, or change the goal on purpose and say so here.
