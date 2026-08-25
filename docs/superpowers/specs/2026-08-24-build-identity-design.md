# Icon, version, window title (Build #4)

Date: 24 Aug 2026
Status: implemented

## Sources

- Win32 `VERSIONINFO` + `ICON` in an `.rc` linked into the exe (Explorer,
  taskbar, file properties).
- Existing `ENGINE_GAME_APP` already chose window title `Sol` vs
  `Engine Sandbox`. This item makes that title queryable and pairs it with
  a file version and a branded icon on `game.exe`.

## Not this

- Store listing art, code signing (Build #11), zip/installer (Build #8).
- Changing the sandbox into a shipped product. Sandbox keeps its proving
  title and no branded icon.
- Exclusive DXGI / splash (later Build rows).

## Decision

CMake `project(VERSION 0.1.0)` feeds a configured `.rc` for both runtime
apps. `game.exe` also embeds `sol.ico` as `IDI_APP_ICON` (101). Win32
`WNDCLASSEX` loads that icon when present, else `IDI_APPLICATION`.

`IWindow::title()` returns the create-time title. `IPlatform` reports
file version (`0.1.0`) and whether resource 101 exists.

Gate:

```
Identity gate: title=... icon=yes|no version=0.1.0 (pass)
```

Sandbox: title `Engine Sandbox`, icon no. Game: title `Sol`, icon yes.
