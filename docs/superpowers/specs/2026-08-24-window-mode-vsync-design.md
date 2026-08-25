# Fullscreen / borderless / vsync (Platform #6)

Date: 24 Aug 2026  
Status: implemented

## Decision

Controls live on `IWindow`, not buried in `Present(1, 0)`.

```
enum class WindowMode { Windowed, Borderless, Fullscreen };
IWindow::set_mode / mode
IWindow::set_vsync / vsync
IDevice::set_present_interval / present_interval   // 0 or 1
```

Win32 Borderless and Fullscreen both cover the current monitor (`WS_POPUP` +
`MONITORINFO.rcMonitor`). Exclusive DXGI `SetFullscreenState` is not this item;
`DXGI_MWA_NO_ALT_ENTER` stays on. Engine copies window vsync onto the device
each render. Immediate present uses tearing when the factory allows it.

`WindowDesc.mode` / `vsync` set the create-time defaults. `Engine::window()`
exposes the handle so games can toggle without cvars (Build #10).

## Gate

`Window gate: startup=yes borderless=yes fullscreen=yes restore=yes vsync=yes present=yes (pass)`

1. Default after init is Windowed + vsync on.
2. `set_mode(Borderless)` then `Fullscreen` report those modes.
3. `set_mode(Windowed)` restores the previous client size.
4. `set_vsync(false/true)` round-trips on the window.
5. `set_present_interval(0/1)` round-trips on the device.

## Visible check

Sandbox **F11**: Windowed ↔ Borderless.

## Not this

- Exclusive DXGI fullscreen / display mode change.
- Quality presets / options menu (Build #10, needs Foundation #8 cvars).
- Per-monitor DPI mode switch beyond existing `dpi_scale()`.
