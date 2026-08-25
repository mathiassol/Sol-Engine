# Physics capsule (Physics #3)

Date: 20 Aug 2026
Status: implemented

## Sources

- Jolt `CapsuleShape`: cylinder half-height + radius, caps at
  `(0, ±halfHeight, 0)`, Y-up. Half-height **excludes** the hemispheres.
  `halfHeight == 0` is a sphere.
- Christer Ericson, *Real-Time Collision Detection*: capsule = segment ⨁
  sphere. Contacts from closest points (segment–point, segment–segment,
  segment–AABB).

## Not this

- Unity’s “height includes caps” (easy to make height < 2r).
- Faking a character with a tall AABB (catches on edges; that’s why capsules
  exist).
- Full CharacterController (`Move`, step offset, slope limit) — Gameplay #4
  (landed).
- Angular / tilted capsules — rotation still frozen.

## Decision

Add `ShapeType::Capsule` to the existing SI solver.

- `ShapeDesc.radius` — cap/cylinder radius
- `ShapeDesc.half_height` — half of the **cylinder** (Jolt)
- Axis is world +Y
- Tight AABB: `{r, half_height + r, r}`
- `overlap_capsule` for queries (same pattern as `overlap_sphere`)

Contacts:

| Pair | Method |
|------|--------|
| Capsule–sphere | Closest point on segment to center |
| Capsule–capsule | Closest points of two segments (Ericson 5.1.9) |
| Capsule–AABB | Iterate closest point on segment ↔ AABB clamp |

Then the existing sphere contact (including interior min-face) at those points.

## Gate

`Physics capsule gate: overlap=yes rest=yes floor=yes not_aabb=yes (pass)`

1. Query capsule overlaps a known static AABB; a too-small capsule misses.
2. Dynamic capsule settles on a static floor at
   `y ≈ floor_top + half_height + radius` (not `half_extents.y`).
   That last check proves we did not rest as a box of height `2*half_height`.

## Out of scope

Character `Move()`, sliding, stairs, triggers, rays, Jolt.
(Gameplay #4 landed `Move` / step / slope. Triggers, rays, and
CharacterController are separate rows.)
