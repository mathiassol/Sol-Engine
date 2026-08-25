# Assets pak (Assets #8)

Date: 24 Aug 2026
Status: implemented

## Sources

- Jason Gregory, *Game Engine Architecture*: one archive of cooked blobs
  for shipping; runtime opens by virtual path, not by walking a folder of
  source files.
- Cooker (Assets #7) already emits `SOLC`. The pak concatenates those
  (or any named payload) into one little-endian blob.

## Not this

- Placing `content.pak` next to `game.exe` (Build #5) — implemented separately.
- Zip / installer (Build #8).
- Compression, encryption, patching, or a GUID database.
- Switching the sandbox off loose glTF/PNG/HLSL mounts.
- Filesystem I/O in `assets` (bytes in / bytes out, like cooker).

## Decision

Little-endian `SOLP` blob, version 1:

```
magic "SOLP"
version u32 = 1
entry_count u32          // 1..65535
for each entry:
  name_len u32
  name bytes             // UTF-8 virtual path
  offset u32             // from start of file
  size u32
payloads packed tightly after the table, in table order
```

Names start with `/`, use `/` separators, reject `.` / `..` / empty
segments / trailing slash / duplicates / empty payloads. Offsets must
equal the packed layout (no gaps, no overlap, no trailing bytes).

`write_pak` / `read_pak_entry` / `peek_pak` plus `create_pak_loader` so
`IAssetLoader::load_bytes` can serve virtual paths from a pak without
`assets-filesystem`.
