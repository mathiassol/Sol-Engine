# Named GPU PIX-style markers (Debug #4)

Date: 25 Aug 2026
Status: implemented

## Sources

- Object `SetName` already exists on buffers, textures, and PSOs. That is
  the resource list in PIX, not the GPU timeline.
- `ID3D12GraphicsCommandList::BeginEvent` / `EndEvent` / `SetMarker` are the
  PIX-style timeline events RenderDoc and PIX both decode (ANSI metadata `1`).
- The render graph already has pass names (`shadow`, `forward`, …).

## Not this

- WinPixEventRuntime NuGet / `pix3.h` / shipping a PIX capture DLL.
- Graph dump to text/dot (Debug #5).
- CPU profiler scopes (Foundation #5 / F3 already exist).

## Decision

`ICommandList` grows `begin_event`, `end_event`, and `set_marker`. D3D12
writes PIX ANSI events into the command stream in Debug **and** Release so a
player PIX capture of `game.exe` still shows pass names. A small CPU stack
(`debug_event_depth` / `debug_event_name` / `last_debug_marker`) is what
`--gates` can assert without PIX attached.

`RenderGraph::execute` wraps every executed pass (graphics and copy) in
`GpuDebugEvent` using the pass name. The renderer does not include D3D12.

Gate:

```
Pix gate: begin=yes nest=yes marker=yes depth=0 (pass)
```
