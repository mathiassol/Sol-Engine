# Cooked pack next to the exe (Build #5)

Date: 24 Aug 2026
Status: implemented

## Sources

- Jason Gregory: cook offline; the player binary reads engine-native blobs.
- Assets #7 (`SOLC`) and Assets #8 (`SOLP`) already exist as bytes in / bytes
  out. This item places a real pack next to the shipped exe.

## Not this

- Switching the sandbox off loose glTF / PNG / HLSL mounts.
- Loading the live husky draw from SOLC (mesh loaders still parse source).
- Zip / installer (Build #8).
- Compression, embedding the pak inside the exe, or a GUID database.

## Decision

Host tool `cook.exe` (`packages/cook`) reads sandbox content, writes `SOLC`
meshes/images plus a short PCM beep, and packs them into
`build/cooked/content.pak`. POST_BUILD copies that file next to `sandbox.exe`
and `game.exe`. `cmake --install` installs `content.pak` beside `game.exe`.

Virtual paths inside the pak:

- `/content/meshes/cube.solc`
- `/content/meshes/cartoon_husky.solc`
- `/content/textures/albedo.solc`
- `/content/textures/husky/Cartoon_Husky_Albedo1.solc`
- `/content/audio/beep.solc`

Gate (CPU, both apps): pak exists next to the exe, peeks as `SOLP`, cooked
cube mesh roundtrips.

```
Pack gate: file=yes peek=yes get=yes (pass)
```
