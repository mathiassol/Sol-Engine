# Scene file save / load (Scene #4)

Date: 24 Aug 2026  
Status: implemented

## Decision

Text format in `scene`, not a filesystem package. Caller writes bytes
(`IFileSystem` / tests). Camera is fly-cam runtime and is not stored.

```
solscene 1
ambient x y z
sun_dir x y z
sun_color x y z
point i px py pz cx cy cz radius intensity
material albedo metallic roughness
instance "name" mesh_id mesh_gen material parent_or_- local16
```

- Unnamed instances are omitted on write and cannot round-trip.
- Parents are **names**, not indices, so dropping unnamed does not scramble the tree.
- `Instance.model` is local. Load parents with `keep_world=false`.
- Mesh is the `MeshHandle` id + generation (same as `make_mesh_handle` / store).
- Unknown keywords and bad version fail `read_world`.

## Gate

`Scene file gate: named=yes unnamed=drop hierarchy=yes lights=yes mesh=yes reject=yes (pass)`

## Not this

- Prefabs (Scene #5) — a prefab is a scene fragment of the same format.
- Additive / streamed worlds (still needs Assets #9).
- Editor inspector.
- Mesh **path** strings on `World` (handle id is what World stores).
