# Physics overlaps (Physics #1)

Date: 20 Aug 2026  
Status: approved — implement this spec

## Problem

The engine has no collision world. `scene::World` is drawables. Mesh AABBs
exist for frustum extract and F4, not for gameplay queries.

Physics #1 is **overlap tests (AABB / sphere)**. Later rows (bodies, capsule,
triggers, raycasts, Jolt) must not require a new query API.

## Decision

Same split as audio:

| Package | Role |
|---------|------|
| `physics` | Interface + shape/query types only |
| `physics-cpu` | One CPU impl: dynamic AABB tree + primitive narrowphase |

Do **not** vendor Jolt/PhysX/Bullet. That is Physics #7, behind the same
`IPhysics`.

Do **not** store colliders on `scene::Instance`. The sandbox copies AABBs into
physics, the same way it copies instances into extract. `user_data` on a body
is the optional glue (scene index).

## Architecture

```
query AABB/sphere → layer mask → dynamic AABB tree (broad) → shape vs shape (narrow) → BodyHandle[]
```

This is the Unity / Godot / Unreal query path. The tree is Erin Catto’s
dynamic BVH (GDC 2019 / Box2D): index nodes, fat leaf AABBs, SAH insert,
remove + reinsert when a tight AABB leaves its fat leaf.

One tree is enough at sandbox scale. Static vs dynamic layers are an internal
later change; the interface never mentions the tree.

## Public API (`engine::physics`)

- `BodyHandle` — id + generation (invalid = zeros).
- `ShapeType` — `Aabb`, `Sphere`. Capsule is a later enum value.
- `ShapeDesc` — AABB: `half_extents`; sphere: `radius`. Position is the center.
- `BodyDesc` — shape, position, `layer` (bit), `mask` (stored for #2 pairs;
  volume queries filter `body.layer & query_mask`), `sensor` (stored for #4),
  `user_data`.
- `IPhysics`
  - `create_body` / `destroy_body`
  - `set_position`
  - `tick` — exists for later sim; #1 may refit immediately in `set_position`
  - `overlap_aabb(box, mask, out)` / `overlap_sphere(center, radius, mask, out)`
    — write up to `out.size()` hits, return count
  - `body_count` / `name` (`"cpu"`)

Queries include a body if `(body.layer & mask) != 0` and the **tight** shape
overlaps. Fat AABBs are broadphase only.

Capacity: 256 bodies. Full create returns an invalid handle.

## Implementation notes

- Nodes: indices + free list, no pointers.
- Fat margin: `0.15` world units.
- Narrowphase: AABB–AABB, sphere–sphere, sphere–AABB (closest point).
- Main thread only. No jobs.
- `math::Aabb` gains overlap / union / surface area / expand / closest point.
- Engine: `EngineModules.physics`, `Engine::physics()`, `tick()` after audio.
- Apps link `engine::physics-cpu` and define `ENGINE_HAS_PHYSICS_CPU`.

## Gate

`--gates` logs:

`Physics gate: aabb=yes sphere=yes mask=yes move=yes gen=yes backend=cpu (pass)`

Cases:

1. Two AABBs: overlapping pair found; separated pair not found.
2. Sphere overlaps one AABB and misses the other.
3. Layer 1 vs mask 2 returns 0; mask 1 returns 1.
4. `set_position` moves a body into the query volume; count changes.
5. Destroy + recreate: old handle does not hit; new generation does.
6. `name() == "cpu"`.

No GPU work. GPU debug stays on for the rest of the sandbox.

## Out of scope

Rigid body integration, contacts, gravity, capsules, enter/exit events,
raycasts, rotated boxes, mesh colliders, Jolt, job system, inspector.

## Expand

| Map | Reuse | Add |
|-----|--------|-----|
| #2 | Overlapping pairs from the tree | Velocity, contacts, fixed-step integrate |
| #3 | `ShapeType` + queries | Capsule; static floor body |
| #4 | `sensor` bit | Previous-frame pair set |
| #5 | Tree walk | Ray vs node, then vs shape |
| #7 | `IPhysics` | `physics-jolt` |
