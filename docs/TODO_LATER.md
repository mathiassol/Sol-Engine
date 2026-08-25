# Engine map (how to pick)

The full categorized list (implementation order inside each category) lives in
[ENGINE_MAP.md](ENGINE_MAP.md). Live numbered sequence: [ROADMAP.md](ROADMAP.md).

This file is only the **picking rules**.

---

## Rules

- Do not scaffold an empty package with no impl. Put new work behind
  `foo` / `foo-*` when you start it. See [packageRules.md](packageRules.md).
- One item, one gate. `--gates` and/or a visible sandbox check with
  `ENGINE_GPU_DEBUG=1`.
- **Ready** rows on the map are fair next picks. `--gates` / the sandbox is
  an allowed consumer. **Later** waits on a named map row that is not Done
  (or a real scale wall). When a Ready row is marked Done, Later rows whose
  Finish first is that row become Ready. **Far** waits until a game actually
  hurts without it.
- Renderer never includes a graphics API. Dependencies only downward.
- The engine and the editor are separate. Do not pick Editor rows as engine
  work. Demo games stay C++ in the sandbox / `game.exe` for a long time.

---

## Triggers (when a Ready row becomes the job)

| You feel this | Start this (see ENGINE_MAP) |
|---------------|------------------------------|
| Albedo-only lighting looks wrong | Renderer: PBR BRDF (**Done**) |
| Environment looks flat (no reflections) | Renderer: IBL (**Done**) |
| No visible sky / backdrop | World: Sky (**Done**) |
| Highlights have no glow / fireflies | Renderer: Bloom (**Done**) |
| Aliasing on edges | Renderer: cheap AA (**Done** — SMAA/FXAA) + Karis TAA optional on F5, default Off (**Done**) |
| Ghosting / wants TAA | Renderer: TAA (**Done** — Karis HDR, exclusive with SMAA) |
| First sound | Audio: interface + one impl (**Done** — XAudio2, Space beep) |
| Jump / overlap / gun | Physics is **Done**. Character controller (Gameplay #4) is **Done**. Follow camera (Gameplay #3) is **Done**. |
| Wants to walk on the floor | Gameplay: character controller (**Done** — Tab/Start walk, Space/A jump, Enter/Y follow/orbit/FPS) |
| Wants a controller | Platform: gamepad (**Done** — XInput, analog walk; no pad required in `--gates`) |
| First HUD / pause text | UI: screen-space quads + font atlas (**Ready**) |
| Wants Jump/Fire as named actions | Gameplay: input actions (**Ready**) |
| Character should deform | Assets: glTF skins (**Ready**), then Animation #1 |
| Land dust / muzzle flash | VFX: CPU particles (**Ready**) |
| Walk away from a sound | Audio: 3D positional (**Ready**) |
| Editing instances in the demo is painful | Change the C++ scene. Scene names if logging hurts. **Not** an in-engine inspector. |
| You want a player to run a game, not the sandbox | Build: `game` target + Release (**Done**) |
| Sun shadows are stamps | Renderer: PCF (**Done**). If **contacts** still look like stamps: PCSS |
| You need a second GPU API | RHI: `rhi-vulkan` (compute + cubes already real) |
| Two systems hitch (DXC already has a worker) | Jobs: small pool |

---

## Stale notes

Shadow mapping, tonemap, and a flat `scene` package are **done** (phases 6–8).
Do not treat old “deferred forever” tables as live. The live map is
[ENGINE_MAP.md](ENGINE_MAP.md).
