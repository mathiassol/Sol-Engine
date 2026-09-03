# Materials that reference textures, and a scene the engine did not ship with

Date: 3 Sep 2026
Status: approved

The engine can render exactly one scene: the one the sandbox hardcodes. This
spec is what changes that.

`packages/sandbox/src/world_extract.cpp:91` decides a draw's albedo like this:

```cpp
if (material.albedo < kHuskyVariantCount) {
    texture = assets.husky_albedos[material.albedo];
} else if (material.albedo == kFloorAlbedoIndex) {
    texture = assets.floor_albedo;
}
item.metallic_roughness = assets.default_mr;
item.normal_map         = assets.default_normal;
```

`material.albedo` is not an index into a texture table. **There is no texture
table.** It is a hardcoded branch over five demo textures held as named
pointers, and normal and metal-rough are pinned to defaults because
`scene::Material` has nowhere to put them. The renderer's `DrawItem` has all
three slots and the glTF loader already reads all three URIs; the scene sitting
between them cannot express the connection.

That gap is why the husky needs a bespoke path in `main.cpp`, and why any new
content would need its own.

---

## What this is measured against

The Poly Haven `hidden_alley` scene, exported to glTF:

| | Count |
|---|---|
| Nodes referencing a mesh | **2,806** |
| Materials | **122** |
| Images / texture references | **286** / 329 |
| Meshes / primitives | 1,254 / 1,377 |
| Texture VRAM, RGBA8 + mips | **3.02 GB** |

Against `kMaxInstances = 512`, `kMaxMaterials = 16`, and no texture table.

The VRAM figure is handled **outside the engine**: the textures are resampled
offline before import (286 files at 512² is ~0.4 GB). BC7 compression
(`Assets #5`) would be the better answer at 0.75 GB and full resolution, and
its stated trigger — "GPU memory being the wall" — is now genuinely met, but it
is a separate row and not required to make this work.

---

## Decisions

### D1 — The texture table and the scene→renderer bridge are engine capability

This answers open decision **A2** ("Should the scene-to-renderer extract move
into an engine package?") in the affirmative.

`world_extract.cpp` is 160 lines of *application* code, and two `static_assert`s
inside it are the only thing coupling `scene::kMaxPointLights` to
`renderer::kMaxPointLights` and `kMaxInstances` to `motion::kHistorySlots`. The
file's own comment says that if those diverge "it is a buffer overrun with
nothing to catch it." A game built on Sol inherits no guard, because it must
write its own extract.

Keeping the texture table app-side would repeat the husky pattern one scale up:
every future game and the editor would each write this. `VISION.md` commits to
a third-party editor building on open APIs, and a material system only the
sandbox knows how to assemble does not clear that bar.

### D2 — The bridge cannot live in `engine`

`engine` deliberately does **not** depend on `scene` — that is precisely *why*
the renderer never sees the scene, and ARCHITECTURE.md names it as one of two
facts the package shape is load-bearing on. Putting the bridge in `engine` would
break it.

So the bridge goes in a **new Layer-4 package**, `scene-render`, which apps link
alongside `scene` and `gameplay`. `engine` is untouched.

### D3 — The glTF loader emits a node list, not one baked mesh

Today `GltfLoadResult` holds a single `MeshData` with node transforms baked into
the vertices, and `GltfPrimitive` entries as index ranges into it. Correct for
the husky, which is one object. For the alley it collapses 2,806 nodes into one
welded blob with 1,377 ranges — no instances, no per-object bounds, nothing that
can be culled, moved, streamed, or referenced by an entity.

The cheaper alternative (one instance per primitive over a shared buffer, with
`first_index` plumbed into `DrawItem`) would put the alley on screen sooner and
then have to be replaced the moment step 2 lands, re-importing the scene.

So the loader gains a second, additive path that returns nodes with their
transforms. **The existing baked path stays**, because the husky and its gates
depend on it.

### D4 — The glTF→`World` builder stays in the sandbox

The material system is permanent; this importer is not. Step 3 (`document`)
replaces it with the real per-entity text format. Making it a package now means
creating one we already know we will delete, which is the scaffolding the
package rules forbid.

### D5 — Runtime load, no cooking

The sandbox reads the `.gltf` directly. No `.solscene` changes, no cooker work.
`VISION.md`'s "text is source, cooked is runtime" is the eventual shape and
arrives with `document`; building the cook path now would mean nothing visible
until the whole chain works.

---

## Package layout

```
Layer 2   assets-gpu    → assets, rhi            gains GpuTextureStore

Layer 4   scene-render  → scene, renderer,       new
                          assets-gpu, math
```

`scene-render` receives `world_extract.cpp` from the sandbox, and with it the
two `static_assert`s, which stop being application code guarding an engine
invariant.

`assets-gltf` is **not** a dependency of either — the importer that needs it
stays in the sandbox (D4).

---

## `GpuTextureStore`

Mirrors `GpuMeshStore` exactly, one asset type over. Same file, same shape:

```cpp
class GpuTextureStore {
public:
    TextureHandle store(rhi::IDevice&, std::string_view key, const ImageData&);
    bool unload(TextureHandle);
    const rhi::ITexture* get(TextureHandle) const;

private:
    struct Entry {
        std::string key;
        std::unique_ptr<rhi::ITexture> gpu;
        u32 generation = 0;
        bool live = false;
    };
    std::unordered_map<u64, Entry> entries_;
};
```

`TextureHandle` mirrors `MeshHandle` — `{u64 id, u32 generation}`, `valid()`,
equality — and lives beside it in `packages/assets`. A stale handle after
`unload` is detectable rather than silently reused.

**Keying is the load-bearing detail.** The alley has 286 images behind 329
references, so sharing is real: `store()` keyed on the resolved content path
returns the existing handle when a path is already resident. That dedupe is what
makes 122 materials cost 286 uploads rather than 366, and it is what the gate
asserts.

Three built-in handles are created at startup — a 1×1 white albedo, a flat
normal `(128,128,255)`, and a default metal-rough — so a material naming no
texture renders correctly rather than binding null. These replace
`default_mr` / `default_normal` in `WorldExtractAssets`.

## `scene::Material`

```cpp
struct Material {
    assets::TextureHandle albedo{};
    assets::TextureHandle normal{};
    assets::TextureHandle metallic_roughness{};
    f32 metallic = 0.f;
    f32 roughness = 0.5f;
    f32 opacity  = 1.f;
};
```

The scalars stay: glTF carries both factors and textures, and the shader
multiplies them. The `albedo`-as-magic-integer scheme is deleted along with the
hardcoded branch that reads it.

This is the change that makes the engine able to render a scene it was not
built for. Everything else in this spec is plumbing around it.

## Caps

| Constant | From | To | Why |
|---|---|---|---|
| `scene::kMaxInstances` | 512 | 3072 | 2,806 nodes, with headroom |
| `motion::kHistorySlots` | 512 | 3072 | the `static_assert` couples them |
| `scene::kMaxMaterials` | 16 | 128 | 122 needed |

`World` grows to ~370 KB and the motion history to ~196 KB. Both are fine:
`ForwardDemo` is held as `std::unique_ptr` in `SandboxState`, so `World` is
already heap-allocated. **`Scene #12` (heap-backed `World`) is not required** —
that was the expensive possibility and it is not triggered.

Two of the six contradictions `VISION.md` lists shrink, so the `vision-gap`
invariant will require those rows updated. That is the check working.

## The glTF node path

`assets-gltf` gains an additive result type — nodes carrying a world transform,
a mesh reference, and a material index — alongside the existing baked
`GltfLoadResult`. The existing path is untouched, because `run_gltf_gate`,
`run_gltf_node_transform_gate` and the husky depend on it.

The sandbox's importer then walks that list: for each distinct glTF material it
uploads the three textures through `GpuTextureStore` (deduped by path) and
creates a `scene::Material`; for each node it uploads the mesh through
`GpuMeshStore` and creates a `scene::Instance`.

---

## The gate

`run_material_gate`, classified **`Gpu`** — texture upload needs a device, so
unlike `reflect`'s gate this one cannot run in CI. It asserts on measured
values:

- **Dedupe is real.** Import a fixture whose materials share textures; assert
  `unique_uploads < total_texture_references`. That inequality is the entire
  point of path-keying, and a store that uploaded every reference separately
  would still render correctly — so nothing else would catch it.
- **Every material's three handles resolve** to a live texture, including the
  built-in defaults for a material that names none.
- **A handle stale after `unload` is detected**, not silently reused.
- **A scene above the old cap loads**, with `instance_count` matching the node
  count — the assertion that the cap raise is real rather than nominal.

The alley itself is a manual check, not a gate: 1.3 GB of textures cannot live
in the repo, and no hosted runner has a GPU.

---

## Out of scope

| Not here | Why |
|---|---|
| BC7 compression (`Assets #5`) | Offline downscaling removes the need; separate row, trigger now met |
| Cooking to `.solscene` | D5 — step 3 (`document`) owns the format |
| Relative mouse (`Platform #10`) | Independent Ready row, unrelated to materials |
| Mesh collision | `ShapeType` has no mesh shape; fly-camera first |
| HDRI sky | `ibl::bake()` takes no arguments and is procedural; separate work |
| Emissive (`Renderer #31`) | Ready, and a natural follow-on, but not needed to load a scene |
| The importer as a package | D4 — known temporary |

## Do not

- **Do not put the bridge in `engine`.** It would make `engine` depend on
  `scene` and break the property that keeps the renderer scene-free.
- **Do not remove the baked glTF path.** The husky and three gates use it; the
  node path is additive.
- **Do not let the texture store key on anything but the resolved path.**
  Keying on the material or the URI as written re-uploads shared textures, and
  the scene still renders — the failure is invisible without the gate's
  inequality.
- **Do not raise the caps without moving `kHistorySlots` with them.** The
  `static_assert` in the bridge is what stops that becoming a buffer overrun,
  and it is the reason the bridge is moving somewhere it can be owned.
- **Do not add BC7 to make the alley fit.** Downscale offline. The compression
  row is worth doing on its own terms, not as a dependency of this.
