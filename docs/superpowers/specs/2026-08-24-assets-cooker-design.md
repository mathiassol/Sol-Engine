# Assets cooker (Assets #7)

Date: 24 Aug 2026
Status: implemented

## Sources

- Jason Gregory, *Game Engine Architecture*: cook offline; runtime loads
  engine-native blobs, not source parsers.
- Existing CPU loaders already produce `MeshData`, `ImageData`, and PCM.
  The cooker packs those. It does not re-parse glTF / PNG / WAV.

## Not this

- Pak / archive (Assets #8) — implemented separately.
- BC7 / GPU formats (Assets #5).
- GUID database (Assets #6).
- Streaming music decoder (Audio #5).
- Filesystem I/O in `assets` (bytes in / bytes in, like scene files).
- A dependency from `assets` on `audio`.

## Decision

Little-endian `SOLC` blob, version 1:

```
magic "SOLC"
version u32 = 1
kind u32   // 1=mesh, 2=image, 3=audio
payload
```

- Mesh: `vertex_count`, `index_count`, AABB (6 f32), `VertexPN[]`, `u32[]`.
- Image: `width`, `height`, RGBA8 `width*height*4`.
- Audio: `sample_rate`, `channels` (1 or 2), `bits` (16), `byte_count`, PCM.

Reject wrong magic, bad version, truncated payloads, wrong kind, empty
mesh/image/audio, RGBA size mismatch, out-of-range indices.

`packages/assets` is a static lib (`cooked.cpp`). Source loaders still live
in `assets-obj` / `assets-gltf` / `assets-png`.
