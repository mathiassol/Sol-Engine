# Editor-centric architecture — identity, commands, and an engine an agent can drive

Date: 3 Sep 2026
Status: approved

**This is a parent spec.** It settles the cross-cutting decisions that the next
several years of work inherit, and it deliberately does not design any one
subsystem to implementation depth. Each of the eight steps in "Build order"
gets its own design spec and its own plan when it starts.

It exists because these were decisions held in one person's head. Four of them
had already leaked into the tree as their opposite — a 512-instance array with
index identity, no despawn, a monolithic scene file — so writing them down is
not documentation, it is the thing that stops the tree drifting further from
the target.

---

## The goal, and what it is not

The product goal is owned by [VISION.md](../../../VISION.md), which summarises
D1-D7 below and is the file to read first. Repeated here only far enough to
make this spec readable on its own.

A general-purpose engine in the Unity / Godot / Unreal category, targeting a
**full open world at high fidelity** — big world, good graphics, advanced
systems. Streaming is assumed, not deferred.

The positioning, stated as what each of the big three gets wrong:

- **Unreal** is powerful and locked. Capability is real; the price is that you
  work the way Unreal works.
- **Unity** is more open and older than it can afford to be.
- **Godot** is genuinely open — the thing to learn from — but its feature set
  stays primitive and it is not trying to compete on large, high-quality games.

Sol's wedge is **not** "open like Godot, powerful like Unreal." Text scene
files are table stakes; Godot already has them. The wedge is:

> **An engine an agent can drive as well as a human can.**

Nothing in the big three can claim that. Unreal's Python editor scripting is
bolted onto the editor, Godot has no external editor control at all, Unity's is
partial. This is the property every decision below is chosen to serve, and it
is also the property that suits this repo's existing verification culture.

**Non-goals for 1.0:** a visual scripting language, a node/material graph, an
asset marketplace, console platforms.

---

## Decisions

Each is stated as a decision because each was open, and each is expensive to
reverse.

### D1 — The editor is essential, and must never be the only way in

The editor is what makes an engine usable. It is also what locks users in when
it becomes the only path to a capability. So: **every authored thing is a plain
text file, and every editor operation is a command that can be issued without
the editor.**

This is not "we also ship a CLI." It is a structural constraint that the editor
cannot violate, enforced by D3.

### D2 — Layer and shippability are two axes, not one

The original sketch was a four-layer stack — renderer, runtime, engine, editor
— where `engine` held the development-only APIs. That does not work: game code
written against engine APIs needs those APIs at runtime, so an engine layer
that does not ship cannot hold the open API surface.

The two axes are:

- **Layer** — dependency direction. The tree already has this (five ranks) and
  already machine-checks it.
- **Runtime / tools** — whether a package ships in a packaged game. This is a
  *tag*, not a layer.

Godot does this with one codebase and `TOOLS_ENABLED` compiled out of export
templates. Unreal tags every module `Runtime` / `Editor` / `Developer`. Neither
has an "engine layer that does not ship."

**Decision:** every package gains a `runtime` or `tools` tag, and a new
invariant fails the build if a `runtime` package depends on a `tools` package.
Cheap, and it is the same class of rule as the existing layer check.

### D3 — The command layer is the spine

Every mutation of authored data goes through one command API. The editor UI,
the CLI, the Lua console, and an AI agent are all **clients** of it, and none
can bypass it.

If the editor mutated state directly and the CLI were a second path onto the
same state, they would diverge — not might, would. This is the only structure
that delivers D1.

What falls out at no extra cost:

| Capability | Why it is free |
|---|---|
| Undo / redo | It is the command log |
| Agent control | Same API, no second surface |
| Editor tests | Replay a log, diff the resulting files |
| Swappable editor UI | The UI is a thin client |
| Crash recovery | Replay the session log |
| Collaborative editing (later) | Commands are already serialisable and ordered |

### D4 — Commands are editor-time only

The command layer mutates **authored** data. The cooked runtime is a separate
fast path; gameplay mutates entities directly with no command indirection.

An agent can author, cook, and inspect. It cannot reach into a running game's
live state. A read-only live channel may be added later behind a debug build —
that stays compatible with this decision, whereas live mutation through the
command layer would put an indirection on the hot path at open-world entity
counts.

### D5 — Entity plus components; the file format is not the runtime layout

An entity is a **stable id plus a set of named components**. Not Godot's
node-as-behaviour, which conflates authoring with runtime and cannot reach the
target scale. Not archetype ECS as the authoring model, which has no clean text
representation and which this project's own docs already rejected as "a second
source of truth."

The load-bearing half of this decision: **on disk is not in memory.** The file
is per-entity text; the runtime packs components into dense per-type arrays
chunked by cell. Any single hot component type can move to archetype storage
later as an optimisation, without touching the file format or the command
surface. Godot conflates these two and cannot get out of it.

### D6 — Scripting is Lua, a script is a component, and nothing ticks by default

Game logic is Lua. A script is attached as a `[script]` component, so two
scripts on an entity is two components and needs no special rule.

**Nothing ticks by default.** One VM with 200,000 script instances calling
`_update` every frame is not viable, and that is a model problem rather than a
Lua problem — Unreal's answer is that most actors do not tick, work is
event-driven, and a significance manager allocates attention by distance. The
tick policy is therefore part of the component:

| `tick` | Behaviour |
|---|---|
| `"never"` (default) | Event-driven only. Most entities |
| `"always"` | Every frame. The few that must |
| `"significant"` | The significance system decides per frame against a budget |

The tick list is rebuilt when cells load or unload, never scanned per frame. So
200,000 entities existing costs nothing; only residency and significance cost.

### D7 — The editor UI technology is a late decision

Because the editor is a thin client of D3, the UI choice does not block
anything and is deliberately left open. Recorded so the reasoning is not
re-derived:

- **Qt Quick / QML with `QRhiWidget`** (Qt 6.7+) can share the engine's D3D12 or
  Vulkan device rather than blitting. Houdini, Maya, Substance and Nuke prove
  QML-era Qt scales to enormous tool complexity. Costs: LGPLv3-or-commercial,
  landing on the editor rather than the MIT engine, and a separate binary.
- **Tauri / web** is strongest for panels — timelines, graphs, inspectors — and
  weakest for the viewport: a native child window cannot composite engine UI
  over the 3D view with transparency, and shared-texture-to-canvas costs a copy
  and latency every frame.
- **ImGui with ImGuizmo** gets working tooling in weeks and will never be
  best-in-class UI quality.

Worth knowing: game engines all draw their own editor UI (Unreal's Slate,
Unity, Godot's Control nodes, Blender). The tools that use Qt are DCC
applications, which have one viewport in a mostly-static layout. Engines need
play-in-editor, per-frame gizmos, and identical behaviour across platforms.

**The pragmatic path this design enables:** ship ImGui to get tooling now,
replace the shell later, because the engine never knows which client is
talking to it.

---

## Identity — three kinds

### `EntityId` — persistent

128-bit UUIDv7. It is the filename, and it is the only thing cross-references
store.

- **Why 128-bit:** parallel editing by several people and agents on branches
  must never collide on merge. A 64-bit random id needs a global uniqueness
  check to be safe, and no authority exists to run one across branches before a
  merge. 128 bits deletes the failure class permanently.
- **Why v7:** time-ordered, so directory listings and diffs sort by creation
  instead of scattering.
- **Why not the name:** a rename would move the file, which git records as
  delete-plus-add, and names are not unique. The human-readable name lives
  *inside* the file, so a rename is a one-line diff.
- **Accepted cost:** you navigate by editor or CLI, not by `ls`. Unreal made the
  same trade with one-file-per-actor, for the same reason.

### `EntityHandle` — runtime

`{ u32 index, u32 generation }`. A dense index into the owning cell's arrays,
with the generation bumped on slot reuse so a stale handle is detectable rather
than silently wrong.

This follows the pattern `physics-cpu` already uses — a handle carrying a
generation and a live flag per slot — so it is an existing in-tree convention
rather than a new invention.

### `EntityRef` — a cross-entity reference

Always holds the `EntityId`, plus a cached handle, and resolves to **three**
states:

| State | Meaning |
|---|---|
| `Live(handle)` | Target is resident; use the handle |
| `NotResident` | Target exists, its cell is not loaded — **normal** |
| `Dangling` | No entity with that id exists anywhere — **a content bug** |

Collapsing the last two into "null" is the specific mistake open-world engines
make, because it renders a real content error indistinguishable from ordinary
streaming. Telling them apart requires a **world-level id index** (`id → cell`)
built at cook time, so the question is answerable without loading the world.

---

## On-disk layout and grammar

```
project/world/overworld/
  world.world              # cell size, bounds, id index
  cells/0012_0004/
    0192f3a4-b5c6-7d8e-9f01-23456789abcd.entity
```

Cells are a **2D grid** with configurable size. Horizontal streaming is what
this genre needs; vertical partitioning is cost with no payoff here.

The grammar is a **TOML subset parsed in-tree**. Both halves are deliberate:
hand-written keeps the small-parser-with-positioned-errors discipline
`packages/scene/src/scene_file.cpp` already has and adds no dependency, while
being valid TOML means external validators, editor plugins and agents already
understand it for free.

```toml
format = "solentity/1"
name = "guard_captain"

[transform]  pos = [412.0, 18.5, -207.25]
             rot = [0.0, 0.7071, 0.0, 0.7071]
             scale = [1.0, 1.0, 1.0]

[mesh]       source = "res://char/guard.gltf"
[script]     source = "res://ai/guard.lua", tick = "significant"
[patrol]     route = "@0192f3a4-b5c6-7d8e-9f01-23456789ab21"
```

An `EntityRef` is written as a quoted string with an `@` prefix, so a reference
is distinguishable from an ordinary string without consulting the component's
field descriptors. The id's canonical text form is the dashed UUID, and it is
the same in a filename, a file body and a command — one form, no conversion.

Three fixes to warts in the current `.solscene` format, one of which that file
complains about itself:

| Current | New | Why |
|---|---|---|
| Mesh by `fnv1a64` hash | Asset by path | The existing file's own comment calls the hash "stable across runs, but not readable" |
| Transform as 16 floats | pos / rot / scale | A rotation edit should not rewrite sixteen numbers |
| `material 0`, positional light index | Named keys | Positional references cannot be edited safely by anything |

**Text is source, not runtime.** The cooker produces one binary pack per cell; a
shipped game parses no text. The existing `SOLC` / `SOLP` cooker extends to
this rather than being replaced. Binary data — meshes, textures — is never
inlined, only referenced.

---

## Component storage and the tick loop

A component type registers a **name, a POD struct, and field descriptors**.

That last part promotes Foundation #14 from Later to a prerequisite, and the
reason is worth stating: field descriptors make text serialisation, the command
layer, and the editor's inspector **one mechanism** instead of three
hand-written ones that drift apart.

Per cell, per type: a **dense array** plus a **sparse set** (entity index →
component index). Deliberately not archetype storage:

- Single-type iteration is linear over the dense array — the common case, and
  cache-optimal.
- Multi-type iteration walks the smallest array and probes the others' sparse
  sets.
- **Add and remove are swap-remove, O(1)**, so runtime structural change stays
  cheap. This is the concrete reason not to start with archetypes, where adding
  a component moves the entity between chunks.
- The **cell is the allocation unit.** Load fills the arrays; unload drops them
  whole. No cross-cell fragmentation.

---

## Command surface and log

A command is an **intent**, not a diff: `create_entity`, `destroy_entity`,
`add_component`, `remove_component`, `set_field`, `set_ref`, `move_to_cell`.

**Addressing is by id alone**, never cell-qualified — an entity can change
cells, so a path containing the cell is fragile:

```
set @0192f3a4-b5c6-7d8e-9f01-23456789abcd transform.pos [412.0, 18.5, -207.25]
add @0192f3a4-... component patrol route=@0192f3a4-...-4c21
```

Names resolve for convenience (`set @guard_captain ...`, erroring on
ambiguity), but **the log stores resolved ids, never names** — otherwise a
replay after a rename silently edits a different entity.

**One grammar, three uses.** The text form of a command is the log form is the
CLI form. A line copied out of a log can be pasted into the CLI. The editor UI
calls the same commands through an in-process C++ API that builds the same
structs.

**Transactions are the unit, not commands.** A gizmo drag emits hundreds of
`set_field` calls that coalesce into one named transaction on release. Without
this, undo replays three hundred mouse positions.

Two files share the grammar and are not the same thing:

| | Committed | Purpose |
|---|---|---|
| **Session log** | No | Undo, crash recovery, auditing what an agent did |
| **Command script** (`.cmd`) | Yes | A deliberate authored migration or batch edit |

Committing the session log would create two histories that can disagree. Git
already versions the `.entity` files, and those are the truth.

**The discipline this costs**, stated plainly because it is the rule that will
be tempting to break: a command must be a pure function of `(project state,
args)` with no hidden UI state. That forbids "apply the current gizmo mode" —
the editor must resolve its own state into explicit arguments before issuing.
That rule is the entire reason replay, undo and agent control work.

---

## Undo

**Hybrid, chosen per command**, rather than requiring `invert()` on every
command:

- `set_field` stores the old value — tiny and exact.
- Structural changes (`destroy_entity`, `remove_component`) store the removed
  data blob.

The stack holds transactions. Redo re-executes forward. Saving marks a point in
the stack rather than clearing it, so "modified since save" is displayable and
undo still crosses the save boundary.

**The detail that silently loses data if missed:** the undo stack references
authored data, so **a cell with unsaved edits cannot be evicted by streaming.**
Edit an entity, walk away, let the cell unload, press undo — without this rule
the edit is gone and nothing reports it. Dirty cells are pinned; the pin
releases on save.

---

## The streaming boundary — two models, one direction of flow

The editor holds an **authored document** separate from the **runtime world**:

```
commands ──► document ──delta──► runtime world ──► renderer
             (text-backed,       (handles, dense
              id-keyed)           arrays, residency)
```

Flow is **one-way, always**. Commands mutate the document; the document pushes
deltas so the viewport updates live. The runtime never writes back — if it
could, editor state and file state would diverge, which is the exact failure D3
exists to prevent. A gizmo *reads* runtime transforms to draw itself and
*writes* through a command.

Editor residency uses the same streaming system as the game, plus two pin
sources: cells with unsaved edits, and cells the user has explicitly opened.

---

## Package impact

| Package | Tag | Holds |
|---|---|---|
| `reflect` | runtime | Field descriptors (Foundation #14) |
| `world` | runtime | Ids, handles, component registry, dense and sparse storage |
| `worldstream` | runtime | Cell residency, async load, significance |
| `script-lua` | runtime | Lua VM, bindings, the script component |
| `document` | tools | Authored model, text parse and write, id and name indices |
| `commands` | tools | Command structs, dispatch, log, undo |
| `editor` | tools / app | The editor application |

Plus one contract change: `platform` and `rhi` must accept a **foreign native
surface** — a window handle the editor owns — because `ISwapchain` currently
assumes it created its own window. Note that this, not renderer coupling, is
what makes embedding hard: `renderer` already links only `rhi`, `core` and
`math`, and `engine` does not depend on `scene`. The renderer is already
separated.

---

## What this invalidates

- **Category 7 (Scene) is largely rewritten**, not extended. The flat
  512-instance list with index identity is replaced.
- **Open decision A3/C1 is resolved** by this spec: instances become handles.
  The vision requires it, so it is no longer a judgement call.
- **Foundation #14 (field descriptors) promotes** from Later to prerequisite.
- **Assets #14 (async loading) and Jobs #2 (thread pool) promote** — streaming
  cannot exist without them.
- **Editor #9 (undo/redo) moves from Far to near-first**, because it is the
  command log rather than a feature layered on one.
- **The renderer's 512-instance, 16-material and 4-point-light caps must go.**
- **Gameplay #7 (scripting VM) stops being Far** and becomes a sub-project.
- **Open decision T3** ("is a feature Done when it works on one of two
  backends?") gets more urgent, not less: this spec adds a second axis of
  partial completion.

---

## Build order

1. `reflect` — small, unblocks everything
2. `world` — ids, handles, components, and **removal**
3. `document` — the authored model and the text format
4. `commands` — dispatch, log, undo
5. Foreign surface in `platform` and `rhi`
6. `worldstream` — cells, async load, significance (needs the jobs pool)
7. `script-lua`
8. `editor` application

**Steps 1–4 are entirely headless.** The whole spine is buildable and gateable
with CPU gates and no GPU — which is exactly what CI can run today, given that
50 of 88 gates cannot execute on a hosted runner. This ordering adds
substantial capability to the half of the suite CI actually covers.

---

## Deliberately not settled here

Each of these is a real decision, deferred with a reason rather than
overlooked:

| Question | Why it waits |
|---|---|
| Editor UI technology | D7 — the command spine makes it a late, reversible choice |
| LuaJIT (5.1 semantics) vs Lua 5.4 | Belongs to the `script-lua` spec, where the benchmark can decide it |
| Whether C++ gameplay stays first-class alongside Lua | Changes whether a C++ hot-reload story is needed; belongs with `script-lua` |
| Cell size, and the significance budget's shape | Needs a real world to measure against |
| Whether the editor is one process or two | Follows the UI choice |
| Asset database and import pipeline | A separate parent spec; this one assumes assets are referenced by path |

---

## Do not

- **Do not let anything mutate authored data except a command.** The moment the
  editor writes the document directly, D1 and every free capability in D3 is
  gone, and nothing will report it.
- **Do not store names in the command log.** Resolve to ids at issue time.
- **Do not let the runtime world write back to the document.** One direction.
- **Do not evict a dirty cell.** Pin it until saved.
- **Do not collapse `NotResident` and `Dangling`.** Normal streaming and a
  content bug must stay distinguishable.
- **Do not make the authoring format match the runtime layout.** Keeping them
  separate is what leaves archetype storage available later as an optimisation
  instead of a rewrite.
- **Do not tick by default.** The default is `"never"`, and it is load-bearing
  at this scale.
- **Do not add a monolithic scene file back for convenience.** Per-entity files
  are what make merges conflict-free and agent edits small; a "just for the
  test scene" exception becomes the format everyone uses.
- **Do not read a green gate suite as a working system.** That sentence is in
  RHI #24's and #25's do-not lists, and it cost fifty silent gates twice.
