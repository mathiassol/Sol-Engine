# Game cameras (Gameplay #3)

Date: 25 Aug 2026
Status: implemented

## Sources

- Unity third-person boom vs orbit (Cinemachine body) vs FPS (eye on the
  pawn). Same yaw/pitch convention as the sandbox fly cam: yaw 0 looks +Z,
  pitch up is positive.
- Gameplay #4 kinematic capsule as the follow target.

## Not this

- Input actions (Gameplay #2). Raw keys / mouse / pad.
- Gamepad (Platform #3) — landed (`add_look_velocity` for right stick).
- Camera collision / occlusion (no shapecast).
- Cinemachine-style blends, mouse-wheel zoom (no wheel on `IInput`).
- Replacing the fly cam. It stays the debug look.

## Decision

`GameCamera` in `packages/gameplay` (with the character, not in `sandbox`).

Three modes, observably different:

| Mode | Position | Look |
|------|----------|------|
| Follow | Boom in XZ behind yaw, **fixed height** | Chest point; pitch tilts look only |
| Orbit | Sphere around the chest (`\|eye − look\| ≈ distance`) | Always the chest; pitch **moves** the eye |
| FPS | Capsule center + `eye_height` | Yaw/pitch forward; not a boom |

`update(target)` snaps (no spring). Sensitivity matches fly-cam RMB
(`0.003`). Sandbox: **Tab** walk uses the game camera; **Enter** cycles
follow → orbit → FPS. Leaving walk copies yaw/pitch/eye back onto the fly
cam.

## Gate

`Camera gate: follow=yes orbit=yes fps=yes (pass)`

1. Follow: yaw 0 places the eye at `-Z` and above the target. Pitch does
   **not** change eye height. `view` puts the target in front (view-space
   `z < 0`).
2. Orbit: `|eye − look| ≈ distance` at pitch 0 and pitch `0.8`; the higher
   pitch raises the eye.
3. FPS: eye is on the target (plus `eye_height`), not on the follow boom.
   A point along forward is in front.

Character gate still passes.

## Out of scope

Actions, camera collision, splitscreen, mouse wheel. (Pad landed as Platform #3.)
