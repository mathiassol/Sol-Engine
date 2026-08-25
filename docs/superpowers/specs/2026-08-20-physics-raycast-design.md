# Physics raycasts (Physics #5)

Date: 20 Aug 2026  
Status: implemented

## Sources

- Erin Catto / Box2D: ray vs dynamic AABB tree, shrink `tmax` on closer
  hits. Fat leaves are broadphase only.
- Kay–Kajiya slab test for AABB.
- Christer Ericson, *Real-Time Collision Detection* 5.3: ray–sphere,
  ray–cylinder. Capsule = finite Y cylinder + two spheres (same
  parameterization as Physics #3).

## Not this

- All-hits / `RaycastAll`.
- Swept shapes (shapecast).
- Mesh / OBB rays.
- Inspector picking UI (editor is a separate app).
- Guns as gameplay (this is the query; Gameplay binds it later).
- Jolt.

## Decision

Closest hit along `origin + normalize(direction) * t`, `t ∈ [0, max_distance]`.

```
bool raycast(origin, direction, max_distance, mask, hit, ignore = {})
```

- `(body.layer & mask) != 0`
- `ignore` skips one body (the shooter)
- Direction is normalized internally; zero direction or `max_distance <= 0`
  misses
- Origin **inside** a shape does not hit that shape (Unity)
- Sensors are still shapes; mask them out if a gun should ignore volumes
- `hit.fraction` is `t / max_distance` in `[0, 1]`
- `hit.normal` points **outward** from the shape

Tree walk: ray vs node fat AABB, then tight shape. Same BVH as overlaps.

## Gate

`Physics raycast gate: aabb=yes sphere=yes capsule=yes closest=yes mask=yes miss=yes (pass)`

1. Ray along +X hits a unit AABB at the known min-x face, outward normal −X.
2. Same ray vs a sphere: hit at `center − radius` along the axis.
3. Same ray vs a Y-up capsule: hit at `center − radius` on the cylinder.
4. Two AABBs on the ray: the nearer body.
5. Layer/mask miss; a ray the other way misses.

Overlap / body / capsule / trigger gates still pass.

## Out of scope

All-hits, shapecast, mesh colliders, picking UI, Jolt.
