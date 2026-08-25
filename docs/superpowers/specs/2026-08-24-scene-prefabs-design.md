# Prefabs / templates (Scene #5)

Date: 24 Aug 2026  
Status: implemented

## Decision

A prefab is a **scene fragment** of the same `solscene` text as Scene #4.
`extract_prefab` copies a named root and its named descendants (materials
used, not lights). `instantiate_prefab` appends that fragment into a live
`World`.

```
bool extract_prefab(world, root_name, out)
u32 instantiate_prefab(dest, prefab, world_transform, prefix)
u32 instantiate_prefab(dest, text, world_transform, prefix)  // read_world then spawn
```

- Unparented locals become `world_transform * local`. Children keep local.
- Spawned names are `prefix + original` and must fit 31 chars.
- Materials are appended. Parents resolve by the prefixed names.
- Unnamed instances are skipped (same rule as the scene file).
- Overflow (64 instances / 16 materials) or a missing root returns failure
  without a successful spawn index (`kInvalidInstance`).

## Gate

`Scene prefab gate: extract=yes spawn=yes prefix=yes compose=yes miss=yes full=yes (pass)`

## Not this

- Additive / streamed worlds (Assets #9).
- Nested prefab overrides / unpack in an editor.
- Unique-name enforcement beyond the prefix.
