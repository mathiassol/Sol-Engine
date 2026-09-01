# GPU baseline and shipped DLLs

What a player machine needs to run `game.exe`. Build #9.

## GPU and OS

| Requirement | Baseline |
|-------------|----------|
| OS | Windows 10 64-bit (version 1709 or later) or Windows 11 |
| API | Direct3D 12 from the OS (`d3d12.dll`, `dxgi.dll`) |
| Feature level | 11_0 |
| Shader model | 6.0 (DXIL) |
| GPU | Hardware adapter; WARP / Microsoft Basic Render is skipped |

The device create path asks for Feature Level 11_0 and then
`CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL)`. Adapters that cannot do
SM 6.0 are skipped. `--gates` logs that contract:

```
Ship gate: dxc=yes dxil=yes agility=os sm=6.0 fl=11_0 (pass)
```

## Next to the exe (required)

Copied on POST_BUILD and `cmake --install`:

| File | Why |
|------|-----|
| `dxcompiler.dll` | Windows SDK DXC. HLSL → DXIL at runtime (and hot-reload). |
| `dxil.dll` | DXIL validator used by DXC. |

A missing DXC pair is a **configure error** for D3D12 runtime apps, not a
silent warning.

## Not shipped

| File | Why |
|------|-----|
| `D3D12Core.dll` / Agility SDK | Inbox OS D3D12 already covers FL 11_0 + SM 6.0. No `D3D12SDKVersion` export. `agility=os` on the ship gate. |
| `d3d12.dll`, `dxgi.dll` | OS components. |
| VC++ redistributable DLLs | Not needed. The CRT is linked **statically** (`CMAKE_MSVC_RUNTIME_LIBRARY` = `MultiThreaded`), so `game.exe` imports no `MSVCP140` / `VCRUNTIME140` and no `api-ms-win-crt-*` at all. Build #8. |

Verify on any build, and on anything about to be released:

```powershell
dumpbin /dependents build\bin\Release\game.exe
```

Expected, and nothing else: `ole32.dll`, `VERSION.dll`, `USER32.dll`,
`XINPUT1_4.dll`, `d3d12.dll`, `dxgi.dll`, `KERNEL32.dll`, `dxcompiler.dll`.
Every one of those is either an OS component or shipped next to the exe. The
release workflow asserts this before it uploads.

Do not add Agility until a feature actually needs a newer D3D12 runtime than
Windows 10 inbox provides (mesh shaders, enhanced barriers, and similar).

## Related layout

`game.exe` also expects `content.pak`, `content/`, and `debug/` next to the
binary (Build #3 / #5). That is content, not GPU redistributables.
