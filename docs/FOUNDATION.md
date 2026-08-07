# Foundation

What the foundation layer provides and how to use it. Built to match [WHATS_NEXT.md](WHATS_NEXT.md) foundation gates.

## Core

### Time — `engine::Clock`, `engine::FrameTimer`, `engine::FrameContext`

- `Clock` — monotonic seconds, `tick()` returns delta.
- `FrameTimer` — per-frame `begin_frame()`, fixed-step accumulator via `consume_fixed_step()`.
- `FrameContext` — passed to engine callbacks; carries `frame_index`, `delta`, `fixed_delta`, `alpha`, `frame_slot`.

Delta is clamped (`max_delta` default 0.25s) to avoid spiral-of-death.

### Memory — `engine::Arena`

Linear bump allocator. Reset once per frame via `Engine::frame_arena()`. No individual free — for per-frame temporaries only.

### Diagnostics

- `log(level, channel, message)` — channels: `General`, `Platform`, `Render`, `Assets`
- `ENGINE_PROFILE_SCOPE("name")` — RAII scope; no-op profiler by default, swap via `set_profiler()`

## Math

Column-major `Mat4`, right-handed Y-up:

- `Mat4::translate`, `scale`, `look_at`, `perspective`
- `Mat4::inverse_affine` — view matrix inversion
- `operator*(Mat4, Mat4)`
- `Vec3::cross`, `constants.hpp` for `radians()` / `degrees()`

## Platform

### Input

`InputState` tracks `keys_down`, `keys_pressed`, `keys_released` (same for mouse).  
`IInput::key_pressed(Key::Escape)` etc. are convenience helpers.

### Window

Events: `Close`, `Resize`, `Focus`, `Unfocus`.  
`dpi_scale()` returns monitor DPI / 96.

### Filesystem (Win32 impl)

`read_file`, `write_file`, `exists`, `resolve` via `std::filesystem`.

## Engine loop

Phases per frame (in order):

1. `begin_frame` — time + arena reset
2. `poll_events`
3. `input->update()`
4. `fixed_update` — drains fixed-step accumulator
5. `update` — variable timestep callback
6. `render` — render graph (when RHI connected)

Register callbacks:

```cpp
engine::EngineCallbacks cb{};
cb.on_update = [](const engine::FrameContext& frame) { /* ... */ };
app.set_callbacks(cb);
```

Escape closes the window via engine default handling.
