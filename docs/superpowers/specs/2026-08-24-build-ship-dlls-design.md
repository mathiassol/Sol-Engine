# GPU baseline and required DLLs (Build #9)

Date: 24 Aug 2026
Status: implemented

## Sources

- Windows SDK DXC (`dxcompiler.dll` + `dxil.dll`) is already copied POST_BUILD
  and `cmake --install`s next to `game.exe`. Runtime shader compile loads it
  from the exe directory.
- Device create already asks for `D3D_FEATURE_LEVEL_11_0`. Shaders are SM 6.0
  / DXIL. Inbox OS D3D12 on Windows 10 covers that set.
- D3D12 Agility (`D3D12Core.dll` + `D3D12SDKVersion` export) is for features
  newer than the OS runtime. This engine does not use those APIs.

## Not this

- Zip / installer (Build #8). VC++ redistributable is documented, not copied.
- Code signing (Build #11), quality presets (Build #10).
- Shipping a private D3D12Core because a paper feature wants it.

## Decision

**Required next to the exe:** `dxcompiler.dll`, `dxil.dll`. Missing DXC at
configure time is a hard error for D3D12 runtime apps.

**Not shipped:** D3D12 Agility. Device create uses the OS `d3d12.dll`.
Adapters that cannot do Feature Level 11_0 **and** Shader Model 6.0 are
skipped. `IDevice::gpu_baseline()` reports that contract.

Gate:

```
Ship gate: dxc=yes dxil=yes agility=os sm=6.0 fl=11_0 (pass)
```

`agility=os` means no `D3D12Core.dll` next to the exe (or in `D3D12/`).
Canonical player text: `docs/GPU_BASELINE.md`.
