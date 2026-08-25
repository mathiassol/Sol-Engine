# Foundation

What the foundation layer provides and how to use it. Foundation gates in [ROADMAP.md](ROADMAP.md) phase 0–4 are done.

## Core

### Time — `engine::Clock`, `engine::FrameTimer`, `engine::FrameContext`

- `Clock` — monotonic seconds, `tick()` returns delta.
- `FrameTimer` — per-frame `begin_frame()`, fixed-step accumulator via `consume_fixed_step()`.
- `FrameContext` — passed to engine callbacks; carries `frame_index`, `delta`, `fixed_delta`, `alpha`, `cpu_frame_slot` (CPU timer ring). GPU in-flight index is `IDevice::frame_slot()`, filled after the device begins a frame.

Delta is clamped (`max_delta` default 0.25s) to avoid spiral-of-death.

### Memory — `engine::Arena`

Linear bump allocator. Reset once per frame via `Engine::frame_arena()`. No individual free — for per-frame temporaries only. `Arena::push<T>()` placement-news a `T` from the current bump pointer.

### Diagnostics

- `log(level, channel, message)` — channels: `General`, `Platform`, `Render`, `Assets`. Default logger is mutex-serialized stderr.
- `ENGINE_ASSERT` / `ENGINE_ASSERT_MSG` — Debug and Release abort on programmer error (arena OOM, buffer write bounds)
- `ENGINE_PROFILE_SCOPE("name")` — RAII CPU scope. Default `FrameProfiler` keeps 8 named slots and reports the previous frame via `profiler_scope_ms()`. `profiler_begin_frame()` runs at the start of `Engine::run`. Overlay: F3 `P`/`X`/`E`/`G`.

## Math

Column-major `Mat4`, right-handed Y-up. Object space uses **counter-clockwise** fronts (Blender / OBJ). The D3D12 backend sets `FrontCounterClockwise`.

- `Mat4::translate`, `scale`, `look_at`, `perspective`
- `Mat4::inverse_affine` — view matrix inversion
- `operator*(Mat4, Mat4)`
- `Vec3::cross`, `constants.hpp` for `radians()` / `degrees()`
- `Frustum::from_view_proj` — six clip planes; `intersects(Aabb)` for extract skip

## Platform

### Input

`InputState` tracks `keys_down`, `keys_pressed`, `keys_released` (same for mouse)
and four `GamepadState` slots (Xbox layout). Sticks are −1..1 after a circular
deadzone; triggers 0..1. `IInput::key_pressed` / `button_pressed` are convenience
helpers. Window `Focus` / `Unfocus` events call `IInput::set_focused`. Unfocused
windows report no keys, mouse buttons, or pads, so `GetAsyncKeyState` / XInput
cannot steer from another app. `platform-win32` polls XInput 1.4.

### Window

Events: `Close`, `Resize`, `Focus`, `Unfocus`.  
`dpi_scale()` returns monitor DPI / 96.

`WindowMode`: `Windowed`, `Borderless`, and `Fullscreen`. Borderless and
Fullscreen cover the current monitor with a popup (no exclusive DXGI).
`IWindow::set_mode` / `set_vsync` are the controls. `IDevice::set_present_interval`
follows vsync (`0` immediate, `1` vblank). Sandbox **F11** toggles windowed /
borderless. Exclusive display-mode fullscreen is not this API.

### Filesystem (Win32 impl)

`read_file`, `write_file`, `exists`, `resolve` via `std::filesystem`.

## Engine loop

Phases per frame (in order):

1. `profiler_begin_frame` then `begin_frame` — time + arena reset
2. `poll_events`
3. `input->update()`
4. `fixed_update` — drains fixed-step accumulator
5. `update` — variable timestep callback
6. `on_extract` — sandbox copies `World` into `ExtractInstance` (mesh + material
   metal/rough); `renderer::extract_visible` does frustum skip and sun bounds
7. `render` — `setup_standard_frame` graph records passes, then `IDevice::end_frame` presents

**New pass:** add it in `packages/renderer/src/standard_frame.cpp`, not in the sandbox.

`IDevice::begin_frame` / `end_frame` do not transition the backbuffer. The graph is the presenter: it moves swapchain color PRESENT ↔ RenderTarget (or Copy) and leaves it in Present before submit.

`Engine::shutdown` waits the GPU and `RenderGraph::clear()`s transients **before** destroying the device.

Register callbacks:

```cpp
engine::EngineCallbacks cb{};
cb.on_update = [](const engine::FrameContext& frame) { /* ... */ };
app.set_callbacks(cb);
```

Escape closes the window via engine default handling.

## GPU frame memory

`IDevice::alloc_frame_memory(size)` bump-allocates 256-byte-aligned slices from a 64 KiB upload ring **per** `frame_slot()`. `begin_frame` waits that slot, then resets the bump pointer, so the GPU is done with the region before CPU reuse. Opaque draws, the F3 overlay, and F4 debug lines write constants / vertices here — not a persistent CBV that can race with in-flight frames.

Staging copies wait only for the previous copy list fence, not a full `wait_idle()`.

Sampled textures: `TextureUsage::ShaderResource` plus pixel bytes. Offscreen color that is both a render target and an SRV uses `TextureUsage::ColorShaderResource` (HDR `scene_color`). Pipelines with `shader_resource_count > 0` get a root SRV table and a wrap linear sampler. Bind with `ICommandList::set_shader_resource`. PNG decode is `assets-png` (WIC).
