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

### Config / cvars — `engine::Cvar`

Named knobs, settable from `config.cfg` and the command line without a
recompile. `core` parses config **text** only; it never opens a file. The engine
reads the bytes through `platform::IFileSystem` (`engine/cvar_file.hpp`) and
hands the text down, so `core` keeps no OS dependency.

Declare a knob at file scope next to whatever reads it:

```cpp
namespace { engine::Cvar cv_vsync{"r.vsync", true, "Present with vsync"}; }
```

Construction self-registers. Names are dotted lowercase groups (`r.*`,
`window.*`, `gate.*` reserved for the gate). Duplicate names assert. The
registry is **not** thread-safe — drive it from startup and the main thread.

| Type | Constructor literal | Accessor |
|------|--------------------|----------|
| `Bool` | `true` / `false` | `as_bool()` |
| `Int` | `1280` (`i32`) | `as_int()` |
| `Float` | `2.2f` — a bare `2.2` is ambiguous and fails to compile | `as_float()` |
| `String` | `"windowed"` | `as_string()` → `std::string_view` |

Reading a cvar as the wrong type is a programming error, not a config error:
the accessors assert (`ENGINE_ASSERT_MSG`) and abort rather than return a wrong
value.

Registry free functions: `find_cvar(name)` (`nullptr` when unknown, name match
is case-insensitive), `cvar_count()`, `cvar_at(index)`,
`apply_cvar_text(text, source)`, `apply_cvar_args(argc, argv)`. Each `apply_*`
returns `CvarApplyStats { applied, unknown, invalid, ignored }`.

**Precedence** — one rule: a write from source `S` is accepted only when
`S >= the source that last wrote the cvar`.

```
Default (0) < File (1) < CommandLine (2) < Code (3)
```

That single comparison is why `Engine::init` may load `config.cfg` **after**
`main` has parsed the command line and `--set` still wins; no future reordering
of startup can silently flip precedence. A `Code` write always wins, including
over a previous `Code` write. `EngineConfig` fields stay the code-level
defaults — a cvar overlays one only when `source() != CvarSource::Default`.

**File location.** `config.cfg`, searched in two places, first existing file
wins:

1. A discoverable repo root, when one exists and differs from the content root.
2. `<content_root>/config.cfg` — the copy that ships next to `game.exe`.

Candidate 1 exists because `sandbox.exe`'s content root resolves to
`build\bin\Debug` (the build copies `content/` next to the binary, so it looks
like an install layout), and knobs left in build output die on the next clean
build. A player install has no repo root above it, so candidate 2 is used. The
startup `Cvars:` log line names the file actually used, or `none`. A missing
file is silence, not an error. `config.cfg` is git-ignored — it is local
preference.

**Line format.** One knob per line; `key value`, `key = value` and `key=value`
all parse. Blank lines are skipped.

```
# comment (also //)
r.vsync 0            # off while testing
window.mode = borderless
r.aa fxaa
```

- A line starting with `#` or `//` is a comment.
- A **trailing** comment is whitespace + `#`, stripped for every type — so
  `r.vsync 0 # note` applies `0`. A `#` *not* preceded by whitespace stays in
  the value (`Room#3`). `//` is a line comment only, never trailing: a trailing
  `//` rule would corrupt values that legitimately contain it, such as
  `//server/share` or `http://host/x`.
- Bools are case-insensitive: `1/0`, `true/false`, `on/off`, `yes/no`.
  Enum-valued knobs (`window.mode`, `r.aa`) take **lowercase tokens only** —
  each lists its accepted values in its help string, and wrong case is reported
  like any other bad value.
- Ints and floats accept a leading `+`; trailing junk is rejected, and so are
  non-finite floats (`inf`, `nan`).
- An empty value is `Invalid` for every type, `String` included, so `--set key=`
  reads as a typo rather than a silent empty assignment.
- An unknown key, a bad value, or a malformed line **warns and counts** — it
  never stops the engine booting. Warnings from a file name the 1-based line
  number; warnings from the command line carry no line suffix.
- A leading UTF-8 BOM is skipped (Notepad and PowerShell 5.1 both write one). A
  UTF-16 BOM produces a single "save the file as UTF-8" warning instead of one
  garbled unknown-key warning per line.

**Command line.** `--set key=value` and `--set key value`, applied as
`CvarSource::CommandLine` by `apply_cvar_args` before `Engine::init`. Anything
that is not `--set` is ignored — this is a knob overlay, not a general argument
parser, and `--gates` keeps its own parse. A trailing `--set` with no value, or
a `--set` whose value is the next `--set`, counts `invalid` without eating the
following flag. Command-line values keep `#` verbatim; the trailing-comment rule
is a file rule only.

Shipped knobs:

| cvar | type | default | read by |
|------|------|---------|---------|
| `window.width` | `Int` | `1280` | `Engine::init` |
| `window.height` | `Int` | `720` | `Engine::init` |
| `window.mode` | `String` | `windowed` (`windowed \| borderless \| fullscreen`) | `Engine::init` |
| `r.vsync` | `Bool` | `true` | `Engine::init` |
| `r.aa` | `String` | `off` (`off \| fxaa \| smaa \| taa`) | sandbox demo setup |

A non-positive `window.width` / `window.height` warns and keeps the default.
No change callbacks, no dirty flags, no saving back to disk — consumers poll
their cvar. Environment variables are not cvars: `ENGINE_GPU_DEBUG` is read
inside the D3D12 and DXC backends before any registry exists.

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
