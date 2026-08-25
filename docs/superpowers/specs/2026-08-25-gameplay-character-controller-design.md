# Character controller (Gameplay #4)

Date: 25 Aug 2026
Status: implemented

## Sources

- Unity `CharacterController`: kinematic capsule, `Move()`, step offset,
  slope limit. Not a dynamic rigid body.
- Physics #3 capsule (Jolt: `radius` + cylinder `half_height`, Y-up) and
  Physics #5 closest-hit raycast with `ignore`. No shapecast (still Physics #5
  Do not).

## Not this

- Input actions (Gameplay #2). Raw keys / pad.
- Follow / orbit / FPS cameras (Gameplay #3) — landed.
- Gamepad (Platform #3) — landed. Analog `Move()` uses stick magnitude.
- Shapecast, mesh colliders, Jolt, angular capsules.
- Burying Unity `Move()` inside `physics-cpu`.

## Decision

New `packages/gameplay` static lib (like `scene`, not a `gameplay-*` backend).
Depends on `physics` + `math` + `core`. Apps link it.

- Kinematic capsule, `set_position` each fixed tick, velocity zero so `step`
  does not double-move.
- `CharacterController::move(wish_dir, jump, dt)`: collide-and-slide with
  **raycasts** at four heights (feet, lower sphere, center, upper sphere).
  Horizontal slide on non-walkable hits. Step: raise by `step_offset`, move
  at least a radius forward, snap down only if the new rest is higher.
- Grounded: down-ray hit with `normal.y >= cos(slope_limit)`. Snap to
  `hit.y + half_height + radius`. Jump sets vertical speed only while
  grounded. Gravity from `IPhysics::gravity()`.
- `is_walkable_ground(normal, slope_limit_deg)` is public so the gate can
  prove slope without a tilted AABB (the CPU world is still translation-only).

Sandbox: static floor matching the checker quad. Husky 0 follows the
capsule. **Tab** toggles walk mode (WASD is camera-relative XZ, Space jumps).
Fly cam keeps WASD when walk mode is off; Q/E and look always work. Z/X still
nudges the capsule on world X.

## Gate

`Character gate: walk=yes jump=yes step=yes slope=yes grounded=yes (pass)`

1. Capsule on a static floor reports `grounded`, rest height
   `floor_top + half_height + radius`.
2. `move(+Z)` advances XZ and stays on the floor.
3. Jump leaves the ground (`y` up, `grounded` false) then lands.
4. A box shorter than `step_offset` is climbed; the slope helper rejects a
   60° normal at a 45° limit.
5. Prior physics gates still pass.

## Out of scope

Actions, shapecast, particles on land, 3D audio on
the capsule. (Cameras landed as Gameplay #3. Pad landed as Platform #3.)
