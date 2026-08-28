# Physics rigid bodies (Physics #2)

Date: 20 Aug 2026  
Status: implemented

## Sources (use these)

- Erin Catto, GDC 2005 *Iterative Dynamics with Temporal Coherence* (PGS)
- Erin Catto, GDC 2009 *Modeling and Solving Constraints* (sequential impulses)
- Erin Catto, Box2D Solver2D notes (2024): PGS + NGS is the minimum viable
  rigid-body solver; **clamp accumulated impulse**, not the incremental one
- Jolt architecture: Static / Kinematic / Dynamic; gravity then velocity
  constraints then integrate then position correction; Y-up

## Sources (do not follow)

- Penalty springs / “push out with k * penetration” as the solver
- Explicit Euler (`x += v dt; v += a dt`) as the integrator
- Verlet / XPBD as the first rigid-body path (different domain)
- Random GitHub “physics engines”, beginner Unity bounce tutorials
- Full 3D angular on AABB (the box would stop being an AABB)

## Decision

Keep `physics` / `physics-cpu`. Add a **translation-only** rigid body on top
of the existing overlap BVH.

| Choice | Why |
|--------|-----|
| Semi-implicit (symplectic) Euler | Catto step 1+3: `v += g dt`, solve, `x += v dt`. Stable enough for games. |
| Sequential impulses, 8 velocity iters | PGS on contact normals. One-way: accumulated λ ≥ 0. |
| Split / NGS position, 3 iters | Penetration correction on **positions**, not Baumgarte in the velocity loop (less fake energy). |
| Frozen rotation | AABB and sphere stay axis-aligned. Angular + OBB is a later shape, not #2. |
| `MotionType` Static / Dynamic / Kinematic | Same three Jolt/Unity types. Default **Static** so overlap-only bodies do not fall. |
| Fixed step via `Engine::fixed_update` | The loop already has `fixed_delta`. Variable `update()` dt is not a solver step. |
| No Jolt | Still Physics #7. |

Friction: Coulomb tangent, clamped to μ λ_n (Catto). Needed so rest on a
floor is not a lottery. Restitution mixed with `min(eA, eB)`; bounce
threshold so objects sleep-ish instead of jitter.

Sensors still skip response (#4).

## Step order

1. Dynamic: `v += gravity * dt`, linear damping
2. Broadphase pairs from the existing tree (dynamic/kinematic vs others)
3. Narrowphase contacts (AABB–AABB min-axis, sphere–sphere, sphere–AABB)
4. Velocity SI (normal + friction)
5. `x += v * dt`, refit tree
6. Refresh penetration, NGS position correction, refit

`IPhysics::step(f32 dt)` replaces `tick()`. Engine calls it once per consumed
fixed step **after** `on_fixed_update` (game can apply velocity first).

## API additions

- `enum class MotionType { Static, Dynamic, Kinematic }`
- `BodyDesc`: `motion`, `mass`, `restitution`, `friction`
- `set_gravity` / gravity default `{0, -9.81, 0}`
- `set_linear_velocity`, `linear_velocity`, `position`
- `step(f32 dt)`

Static and mass≤0 → `inv_mass = 0`. Kinematic: no gravity, infinite mass,
still integrates velocity (moving world).

## Gate

Keep the overlap gate. Add:

`Physics body gate: gravity=yes rest=yes floor=yes sphere=yes box=yes (pass)`

1. Isolated dynamic falls ~½ g t² in 1 s (symplectic, ±0.4 m).
2. Static floor does not move.
3. Dynamic sphere and AABB settle on the floor (y ≈ top + half-extent,
   |vy| small) after 3 s, restitution 0.
4. Overlap gate still passes.

## Out of scope

Angular velocity, OBB, capsules, sleeping islands, CCD, warm-start across
frames, joints, Jolt, trigger enter/exit, raycasts.
