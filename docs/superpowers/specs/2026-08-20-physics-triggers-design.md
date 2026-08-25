# Physics triggers (Physics #4)

Date: 20 Aug 2026  
Status: implemented

## Sources

- Box2D `b2BodyDef.isSensor` + BeginContact / EndContact (no impulse).
- Jolt sensor bodies: contacts exist for overlap, the solver does not
  apply response.
- Unity `OnTriggerEnter` / `OnTriggerExit` — Stay is omitted here; a game
  can hold the pair set if it needs it.

## Not this

- `std::function` listeners (lifetime noise; the engine already polls
  overlaps).
- Stay events.
- Two sleeping statics overlapping (no active body walks the tree).
- Raycasts (Physics #5), CharacterController, Jolt.

## Decision

`BodyDesc.sensor` already skips sequential impulses. Physics #4 adds a
**previous-frame pair set** and an event snapshot after `IPhysics::step`.

A pair is recorded when:

- at least one body is a sensor
- both layer/mask tests pass (same as contacts)
- shapes overlap (same narrowphase as queries)
- at least one body is Dynamic or Kinematic (the walker)

`a.id < b.id` on every event so order is stable.

| Event | When |
|-------|------|
| Enter | pair in this step, not in the previous set |
| Exit | pair in the previous set, not in this step (includes destroy) |

`trigger_events(out)` copies the **last step’s** events (not a drain). The
next `step()` replaces the snapshot. Destroy only drops the pair from the
previous set; Exit appears on the following step.

Sensors still skip `collect_contacts`. Overlapping solids do not emit
trigger events.

## Gate

`Physics trigger gate: enter=yes stay=yes exit=yes mask=yes solid=yes (pass)`

1. Static sensor + overlapping dynamic: first step → one Enter; second
   step → zero events (`stay=yes` means Stay is silent).
2. Move the dynamic out: next step → one Exit.
3. Mask miss: no Enter.
4. Two non-sensor overlapping bodies: no trigger events.
5. Overlap / body / capsule gates still pass.

## Out of scope

Stay, listener objects, static-only pairs, rays, Jolt.
