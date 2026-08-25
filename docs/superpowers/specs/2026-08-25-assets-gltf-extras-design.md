# glTF extras (Assets #4)

Date: 25 Aug 2026
Status: implemented

## Sources

- glTF 2.0: a mesh is N triangle primitives, each with its own material.
  `metallicRoughnessTexture` is packed **B = metal, G = roughness**, multiplied
  by the factors already loaded in Assets #3. `normalTexture` is tangent-space.
- The husky is still one primitive and albedo-only. The extras path must not
  require new husky maps.

## Not this

- Skins / morphs (Assets #10).
- BC7 / GPU formats (Assets #5).
- Transparency / alpha (Renderer #16).
- Vertex tangents on `VertexPN` / cooked v2. TBN comes from screen-space
  derivatives so OBJ and `SOLC` stay 32-byte verts.
- Per-primitive draws in the sandbox (63 huskies × N would blow the 64-instance
  extract cap). Concatenate into one `MeshData`; the gate proves the table.

## Decision

`GltfLoadResult` keeps `mesh` (all triangle primitives concatenated, indices
shifted by vertex base) plus `primitives[]` with index range and URIs
(albedo, metallic-roughness, normal) and factors. Legacy
`albedo_uri` / `metallic` / `roughness` remain primitive 0 so the existing
glTF gate does not change.

`platform` is not involved. Missing MR samples as white (factors pass
through). Missing normal samples as `(0.5, 0.5, 1)` (vertex normal). Sandbox
binds 1×1 defaults on every draw (`t5` / `t6`). Forward shader multiplies
factors by the packed MR texture and applies a derivative TBN.

`--gates` writes a two-primitive probe glTF (no PNG files required; URIs are
paths). No hardware, no new content in `content/`.

## Gate

`glTF extras gate: prims=yes mr=yes normal=yes (pass)`

1. Two triangle primitives concatenated (`indices == 6`, second `first_index == 3`).
2. Primitive 0 has metallic-roughness and normal URIs; factors 0.25 / 0.5.
3. Primitive 1 has a different albedo URI.

Prior glTF / cook / PBR gates still pass.

## Out of scope

Skins, morphs, BC7, alpha, vertex tangents, rumble-style “must show on the husky”.
