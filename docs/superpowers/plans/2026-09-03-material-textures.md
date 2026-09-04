# Material and Texture System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the engine able to render a scene it was not built for — materials that reference their own albedo, normal and metal-rough textures, loaded from an arbitrary glTF.

**Architecture:** A `GpuTextureStore` in `assets-gpu` mirrors the existing
`GpuMeshStore`, deduping uploads by content path. `scene::Material` gains three
`TextureHandle`s into it. The scene→renderer bridge moves out of the sandbox
into a new Layer-4 `scene-render` package, taking the two orphaned
`static_assert`s with it. `assets-gltf` gains an additive node-list path so a
multi-object scene imports as real instances rather than one baked blob.

**Tech Stack:** C++20, no new third-party code. One `Gpu` gate.

Design: [2026-09-03-material-textures-design.md](../specs/2026-09-03-material-textures-design.md)

---

## Read first

**There is no test framework.** The test *is* a gate: a plain function in
`packages/sandbox/src/gates/gates_<domain>.cpp`, declared in `gates/gates.hpp`,
classified in `kGates` in `gates/gate_registry.cpp`, and called from the
sequence in `main.cpp`. See "What a gate is" in [CLAUDE.md](../../../CLAUDE.md).

**This plan's gate is `Gpu`, not `Cpu`.** Texture upload needs a device, so
unlike `reflect`'s gate this one does not run in CI. It is called directly from
`main.cpp` with a live device, and carries `nullptr` in `kGates`.

**A C++ first red is a wrong value, not a compile error.** A gate that does not
compile has not gone red; it has not run. Where a task can stub a function to
return a deliberately wrong value, it does, and the stub is named in the task.

**Every term in the gate's `passed` must also appear in its message.** A guard
that is in `passed` but not the message is halfway to being deleted — two
drafts of the `reflect` plan removed one by rewriting a format string.

**A gate clause must compare two independently-derived values.** Reading back a
literal written beside it is not a check.

**Trunk-based.** Commit directly to `main`, push after every commit. No
branches.

**The LOC audit goes stale on every commit that changes source lines.** Run
`pwsh -NoProfile -File tools/check-invariants.ps1`; when the figure is stale
`roadmap-audit` prints the number it recounted. Put *that* number in
`docs/ROADMAP.md`. **Do not use `Measure-Object -Line`** — it does not count
blank lines and under-reports this tree by about 4,000.

**Reverting a temporary edit:** prefer `git checkout -- <path>` then
`(Get-Item <path>).LastWriteTime = Get-Date` before rebuilding. Without the
touch MSBuild may skip the recompile and you will run a **stale binary** that
reports your experiment's result against reverted source, with a clean
`git status`.

**Use `pwsh`, not `powershell`.** Format hygiene is machine-checked: LF, no
tabs, 100 columns, final newline, no BOM.

---

## Design decisions this plan locks in

| Decision | Why |
|---|---|
| The texture store keys on the **resolved content path** | 286 images sit behind 329 references. Keying on the material or the raw URI re-uploads shared textures and the scene still renders correctly — the failure is invisible without the gate's inequality |
| Colour space is a **required parameter** of `store()` *and* part of its key | The three maps disagree — albedo is colour, normal and metal-rough are data ([`resources.hpp`](../../../packages/rhi/include/engine/rhi/resources.hpp) owns the rule) — so a default silently gamma-decodes a normal map and every TBN normal is wrong with nothing failing. And the same file legitimately serves as an sRGB albedo in one material and a linear mask in another, so a path-only key hands the second caller the first one's format while the dedupe count still reads as correct |
| The bridge moves to a new package, **not into `engine`** | `engine` deliberately does not depend on `scene`; that is why the renderer never sees the scene. ARCHITECTURE.md names it as load-bearing |
| The glTF node path is **additive** | The husky and `run_gltf_gate`, `run_gltf_node_transform_gate`, `run_gltf_extras_gate` all use the baked path. It stays |
| The importer stays in the **sandbox** | `document` (step 3) replaces it. A package we know we will delete is the scaffolding `packageRules.md` forbids |
| A new gate's name must be checked against the existing ones | `run_material_gate` was already taken by a renderer gate that proves roughness and opacity travel to `DrawItem`. `gate-registry` would have failed on the duplicate, and two `"Material gate:"` lines would have made `--gates` ambiguous |
| An assertion about a field belongs where the road is already checked | The normal-map check went into the existing `run_material_gate` beside roughness and opacity rather than into the new store gate, because that gate already extracts and compares `DrawItem`s. Asserting `mat.normal.valid()` on a bare struct proves only that the field exists |
| A material's textures are **not** serialized yet | `TextureHandle` is a hash of a path plus a colour space. Writing it into a plain-text scene puts an opaque number where a path belongs, and the path-based form needs the store at load time — `document`'s job, per the spec's D5. The token is parsed and discarded; decision **S5** |
| `TextureHandle` mirrors `MeshHandle` exactly | Same `{u64 id, u32 generation}` shape, same `fnv1a64` keying, so a stale handle after `unload` is detectable rather than silently reused |
| Caps rise in **Task 5**, not Task 3, and `Scene #12` is still not triggered | The original reasoning — `ForwardDemo` is `unique_ptr`-held so `World` is already on the heap — was true of the *demo* `World` and only of it. 19 more are function-scope stack locals in the gate files, and at 3072 three gate functions overflow the 1 MiB stack. Task 5 Step 0 heap-holds those locals first. Heap-*holding* a local is not `Scene #12`, which heap-backs the *type* |
| The demo floor checker stays `ColorSpace::Linear` | It is created `RGBA8_UNORM` today (`main.cpp:1003`) and its texel values were chosen to look right sampled that way. Relabelling a procedural pattern as sRGB darkens the demo's midtones without fixing anything — the sRGB rule is about *authored* colour. Deliberate, documented exception |

---

## File Structure

| File | Responsibility |
|---|---|
| `packages/assets/include/engine/assets/texture.hpp` | `TextureHandle`, `make_texture_handle` |
| `packages/assets-gpu/include/engine/assets/gpu/texture_store.hpp` | `GpuTextureStore` declaration |
| `packages/assets-gpu/src/texture_store.cpp` | Its implementation, including path-keyed dedupe |
| `packages/scene/include/engine/scene/world.hpp` | `Material` gains three handles; caps rise |
| `packages/scene-render/CMakeLists.txt` | New package |
| `packages/scene-render/include/engine/scene_render/extract.hpp` | The bridge's public surface |
| `packages/scene-render/src/extract.cpp` | The bridge, moved from the sandbox |
| `packages/assets-gltf/include/engine/assets/gltf/gltf_loader.hpp` | `GltfNode`, `GltfSceneResult`, `load_scene` |
| `packages/assets-gltf/src/gltf_loader.cpp` | The node walk |
| `packages/sandbox/src/scene_import.cpp` | glTF → `World`. Temporary, replaced by `document` |
| `packages/sandbox/src/gates/gates_assets.cpp` | `run_texture_store_gate` |
| `tools/downscale-textures.ps1` | Offline resample. No engine change |

---

## Task 1: `TextureHandle` and `GpuTextureStore`

**Files:**
- Create: `packages/assets/include/engine/assets/texture.hpp`
- Create: `packages/assets-gpu/include/engine/assets/gpu/texture_store.hpp`
- Create: `packages/assets-gpu/src/texture_store.cpp`
- Modify: `packages/assets-gpu/CMakeLists.txt`
- Modify: `packages/sandbox/src/gates/gates_assets.cpp`, `gates/gates.hpp`, `gates/gate_registry.cpp`, `main.cpp`

- [ ] **Step 1: Create `TextureHandle`**

`packages/assets/include/engine/assets/texture.hpp` — mirrors `mesh.hpp`'s
`MeshHandle` exactly, so the two read identically at call sites:

```cpp
#pragma once

#include <engine/core/hash.hpp>
#include <engine/core/types.hpp>

#include <string_view>

namespace engine::assets {

// Mirrors MeshHandle. A generation makes a handle stale after unload rather
// than silently addressing whatever took the slot.
struct TextureHandle {
    u64 id = 0;
    u32 generation = 0;

    bool valid() const { return id != 0 && generation != 0; }
    bool operator==(TextureHandle other) const {
        return id == other.id && generation == other.generation;
    }
    bool operator!=(TextureHandle other) const { return !(*this == other); }
};

inline TextureHandle make_texture_handle(std::string_view key, u32 generation = 1) {
    TextureHandle handle{fnv1a64(key), generation};
    if (handle.id == 0) {
        handle.id = 1;
    }
    return handle;
}

} // namespace engine::assets
```

- [ ] **Step 2: Declare `GpuTextureStore` with a stubbed `store`**

`packages/assets-gpu/include/engine/assets/gpu/texture_store.hpp`:

```cpp
#pragma once

#include <engine/assets/image.hpp>
#include <engine/assets/texture.hpp>
#include <engine/rhi/device.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace engine::assets::gpu {

// GPU textures keyed by their resolved content path.
//
// The dedupe is the point, not an optimisation: a scene shares one texture
// across many materials, and a store that uploaded every reference separately
// would render identically while costing several times the VRAM. Nothing but
// a count catches that, which is what run_texture_store_gate asserts.
class GpuTextureStore {
public:
    TextureHandle store(rhi::IDevice& device, std::string_view key, const ImageData& image);
    bool unload(TextureHandle handle);
    const rhi::ITexture* get(TextureHandle handle) const;

    // Live entries. The gate compares this against the number of store()
    // calls; equality means dedupe is not happening.
    usize size() const { return live_count_; }

private:
    struct Entry {
        std::string key;
        std::unique_ptr<rhi::ITexture> gpu;
        u32 generation = 0;
        bool live = false;
    };

    std::unordered_map<u64, Entry> entries_;
    usize live_count_ = 0;
};

} // namespace engine::assets::gpu
```

- [ ] **Step 3: Implement it, but with dedupe deliberately absent**

`packages/assets-gpu/src/texture_store.cpp`. The `store` below uploads on
**every** call — that missing dedupe is this task's red:

```cpp
#include <engine/assets/gpu/texture_store.hpp>

namespace engine::assets::gpu {

namespace {

std::unique_ptr<rhi::ITexture> upload(rhi::IDevice& device, const ImageData& image) {
    rhi::TextureDesc desc{};
    desc.width = image.width;
    desc.height = image.height;
    desc.mip_levels = 0; // 0 means "generate a full chain" - see TextureDesc
    desc.format = rhi::Format::RGBA8_UNORM_SRGB;
    desc.usage = rhi::TextureUsage::ShaderResource;
    return device.create_texture(desc, image.rgba.data());
}

} // namespace

TextureHandle GpuTextureStore::store(
    rhi::IDevice& device, std::string_view key, const ImageData& image) {
    // STUB - Task 1 Step 6 adds the dedupe this function exists for.
    const TextureHandle handle = make_texture_handle(key);
    Entry entry;
    entry.key = std::string(key);
    entry.gpu = upload(device, image);
    entry.generation = 1;
    entry.live = true;
    entries_[handle.id] = std::move(entry);
    ++live_count_;
    return handle;
}

bool GpuTextureStore::unload(TextureHandle handle) {
    const auto it = entries_.find(handle.id);
    if (it == entries_.end() || !it->second.live || it->second.generation != handle.generation) {
        return false;
    }
    it->second.gpu.reset();
    it->second.live = false;
    it->second.generation += 1;
    --live_count_;
    return true;
}

const rhi::ITexture* GpuTextureStore::get(TextureHandle handle) const {
    const auto it = entries_.find(handle.id);
    if (it == entries_.end() || !it->second.live || it->second.generation != handle.generation) {
        return nullptr;
    }
    return it->second.gpu.get();
}

} // namespace engine::assets::gpu
```

Note `live_count_` is incremented unconditionally here. That is what makes the
gate's inequality fail.

- [ ] **Step 4: Add it to the package**

`packages/assets-gpu/CMakeLists.txt`:

```cmake
engine_add_package(assets-gpu
    SOURCES
        src/mesh_upload.cpp
        src/mesh_store.cpp
        src/texture_store.cpp
    PUBLIC_DEPS engine::assets engine::rhi
)
```

- [ ] **Step 5: Write the failing gate**

In `packages/sandbox/src/gates/gates_assets.cpp`, inside `namespace sandbox {`.
Add `#include <engine/assets/gpu/texture_store.hpp>` at the top if absent.

```cpp
bool run_texture_store_gate(engine::rhi::IDevice& device) {
    using engine::assets::gpu::GpuTextureStore;

    // Two 2x2 images, distinguishable so a wrong handle is visible.
    engine::assets::ImageData red{};
    red.width = 2;
    red.height = 2;
    red.rgba.assign(2 * 2 * 4, 0);
    for (engine::usize i = 0; i < 4; ++i) {
        red.rgba[i * 4 + 0] = 255;
        red.rgba[i * 4 + 3] = 255;
    }
    engine::assets::ImageData blue = red;
    for (engine::usize i = 0; i < 4; ++i) {
        blue.rgba[i * 4 + 0] = 0;
        blue.rgba[i * 4 + 2] = 255;
    }

    GpuTextureStore store;
    // Six references over three distinct paths - the sharing a real scene has.
    const char* refs[] = {"/a.png", "/b.png", "/a.png", "/c.png", "/b.png", "/a.png"};
    engine::assets::TextureHandle handles[6]{};
    for (engine::u32 i = 0; i < 6; ++i) {
        handles[i] = store.store(device, refs[i], (i % 2) == 0 ? red : blue);
    }
    const engine::u32 references = 6;
    const engine::u32 distinct = 3;
    const engine::u32 uploaded = static_cast<engine::u32>(store.size());

    // The assertion the store exists for. A store without dedupe uploads six
    // and renders identically, so only this count can tell the difference.
    const bool deduped = uploaded == distinct;

    // The same path must hand back the same handle, or callers cannot share.
    const bool stable = handles[0] == handles[2] && handles[0] == handles[5]
        && handles[1] == handles[4];

    // Every live handle resolves.
    bool resolves = true;
    for (const auto& h : handles) {
        resolves = resolves && store.get(h) != nullptr;
    }

    // A handle stale after unload is detected, not silently reused.
    const bool unloaded = store.unload(handles[0]);
    const bool stale_detected = store.get(handles[0]) == nullptr;

    const bool passed = deduped && stable && resolves && unloaded && stale_detected;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Texture store gate: refs=%u distinct=%u uploaded=%u stable=%s resolves=%s "
        "unloaded=%s stale_detected=%s (%s)",
        references, distinct, uploaded, stable ? "yes" : "no", resolves ? "yes" : "no",
        unloaded ? "yes" : "no", stale_detected ? "yes" : "no", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}
```

- [ ] **Step 6: Declare, classify and call it**

In `packages/sandbox/src/gates/gates.hpp`, under the `// ── assets ──` block:

```cpp
bool run_texture_store_gate(engine::rhi::IDevice& device);
```

In `packages/sandbox/src/gates/gate_registry.cpp`, beside the other assets
entries. **`Gpu`, with `nullptr`** — it needs a device, so a headless run
cannot call it:

```cpp
    {"run_texture_store_gate", GateKind::Gpu, nullptr},
```

In `packages/sandbox/src/main.cpp`, in the gate sequence beside the other
device-taking gates:

```cpp
    gates_ok = run_texture_store_gate(device) && gates_ok;
```

The gate goes on the **left** of `&&`: short-circuiting would let one red gate
hide the next.

- [ ] **Step 7: Build and watch it fail**

Run:
```powershell
cmake --build build --config Debug
.\build\bin\Debug\sandbox.exe --gates
```
Expected — six uploads where three were wanted:
```
Texture store gate: refs=6 distinct=3 uploaded=6 stable=yes resolves=yes unloaded=yes stale_detected=yes (FAIL)
```
and `--gates` exits `1`.

**Do not proceed until you have seen `uploaded=6`.** That number is the proof
the dedupe assertion is real rather than trivially satisfied.

- [ ] **Step 8: Add the dedupe**

In `texture_store.cpp`, replace the stubbed `store` body:

```cpp
TextureHandle GpuTextureStore::store(
    rhi::IDevice& device, std::string_view key, const ImageData& image) {
    const TextureHandle handle = make_texture_handle(key);
    const auto it = entries_.find(handle.id);
    if (it != entries_.end() && it->second.live) {
        // Already resident under this path. Returning the existing handle is
        // what makes a shared texture cost one upload instead of one per
        // material that names it.
        return TextureHandle{handle.id, it->second.generation};
    }
    Entry entry;
    entry.key = std::string(key);
    entry.gpu = upload(device, image);
    entry.generation = (it != entries_.end()) ? it->second.generation + 1 : 1;
    entry.live = true;
    entries_[handle.id] = std::move(entry);
    ++live_count_;
    return TextureHandle{handle.id, entries_[handle.id].generation};
}
```

Note the generation on re-store after an unload: it continues from the previous
value rather than resetting, so a handle held across an unload/reload cycle
stays stale.

- [ ] **Step 9: Build and watch it pass**

Run:
```powershell
cmake --build build --config Debug
.\build\bin\Debug\sandbox.exe --gates
```
Expected:
```
Texture store gate: refs=6 distinct=3 uploaded=3 stable=yes resolves=yes unloaded=yes stale_detected=yes (pass)
```
and exit `0`.

- [ ] **Step 10: Document, recount, commit**

Add a `assets-gpu` note to `docs/ARCHITECTURE.md`'s package table row — it now
uploads textures as well as meshes. Describe only what exists at this commit
(see "A doc describes the commit it lands in" in CLAUDE.md).

Then run `pwsh -NoProfile -File tools/check-invariants.ps1`, update
`docs/ROADMAP.md`'s audit sentence from `roadmap-audit`'s reported recount, and
re-run until `all 20 checks passed`.

```bash
git add packages/assets/include/engine/assets/texture.hpp packages/assets-gpu packages/sandbox/src/gates/gates_assets.cpp packages/sandbox/src/gates/gates.hpp packages/sandbox/src/gates/gate_registry.cpp packages/sandbox/src/main.cpp docs/ARCHITECTURE.md docs/ROADMAP.md
git commit -m "feat(assets-gpu): a texture store that uploads a shared texture once"
git push
```

---

## Task 2: The `scene-render` package — a pure move

This task changes **no behaviour**. Its whole purpose is that the gate suite
must look identical before and after, which is what proves the move was clean.

**Files:**
- Create: `packages/scene-render/CMakeLists.txt`
- Create: `packages/scene-render/include/engine/scene_render/extract.hpp`
- Create: `packages/scene-render/src/extract.cpp`
- Delete: `packages/sandbox/src/world_extract.cpp`, `packages/sandbox/src/world_extract.hpp`
- Modify: `CMakeLists.txt`, `cmake/EngineRuntimeApp.cmake`, `tools/check-invariants.ps1`, `docs/ARCHITECTURE.md`
- Modify: every sandbox file that included `world_extract.hpp`

- [ ] **Step 1: Record the before state**

Run:
```powershell
.\build\bin\Debug\sandbox.exe --gates
```
Copy the full output to a scratch file. Every gate line must be identical after
the move. This is the task's only real check — there is no new assertion to
write, because nothing new is being built.

- [ ] **Step 2: Create the package**

`packages/scene-render/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.24)

engine_add_package(scene-render
    SOURCES
        src/extract.cpp
    PUBLIC_DEPS engine::scene engine::renderer engine::debug-draw engine::assets-gpu
                engine::math
)
```

All five are real, and each is there because a header the moved code keeps
includes it: `engine::debug-draw` for `<engine/debug/debug_lines.hpp>`,
`engine::assets-gpu` for `<engine/assets/gpu/mesh_store.hpp>` — both already in
`world_extract.hpp` today. `engine::rhi` is **not** listed because
`engine::renderer` re-exports it publicly. If the build complains about a
missing header, add the dep the header belongs to rather than the header.

In the root `CMakeLists.txt`, after `add_subdirectory(packages/engine)`:

```cmake
add_subdirectory(packages/scene-render)
```

In `tools/check-invariants.ps1`, add it to `$Layers` at **rank 5**, beside
`engine`:

```powershell
    # Layer 4 - runtime
    'engine' = 5; 'scene-render' = 5
```

Rank 5, not 4, and `dependency-direction` is what forces it: `scene`,
`renderer` and `debug-draw` are all rank 4, and the check rejects a dependency
on a package at the *same* rank. A bridge exists to sit above the things it
bridges, so the rank the check demands is also the correct one.

- [ ] **Step 3: Move the files verbatim**

Copy `packages/sandbox/src/world_extract.hpp` to
`packages/scene-render/include/engine/scene_render/extract.hpp` and
`world_extract.cpp` to `packages/scene-render/src/extract.cpp`. Change only:

- the namespace, from `sandbox` to `engine::scene_render`
- the include of `world_extract.hpp` to `<engine/scene_render/extract.hpp>`
- `WorldExtractAssets` keeps its name

Do **not** change any logic, any assertion, or either `static_assert`. If you
find yourself improving something, stop — this task is a move, and a behaviour
change hidden inside a 160-line relocation is the one thing the before/after
comparison cannot catch.

- [ ] **Step 4: Point the sandbox at it**

In `cmake/EngineRuntimeApp.cmake`, add `engine::scene-render` to the
`target_link_libraries` list beside `engine::scene`.

Update every `#include "world_extract.hpp"` in the sandbox to
`#include <engine/scene_render/extract.hpp>`, and qualify the call sites:
`sandbox::extract_lighting` becomes `engine::scene_render::extract_lighting`,
and likewise for `scene_world_bounds` and the extract entry point. Delete the
two old files.

- [ ] **Step 5: Build and compare**

Run:
```powershell
cmake --build build --config Debug
.\build\bin\Debug\sandbox.exe --gates
```
Expected: exit `0`, and **every gate line identical** to the Step 1 capture.
Diff them rather than eyeballing:

```powershell
.\build\bin\Debug\sandbox.exe --gates 2>&1 | Out-File after.txt
Compare-Object (Get-Content before.txt) (Get-Content after.txt)
```
Expected: no output. Any difference means the move was not clean — investigate
rather than accepting it.

- [ ] **Step 6: Document, recount, commit**

`docs/ARCHITECTURE.md` needs three edits, and `doc-claims` fails without the
first two: add `scene-render` to the Layer 3/4 dependency graph, add its row to
the Packages table, and note in the "Swap test" section that the bridge is no
longer app code.

Then the invariants and the LOC recount as in Task 1.

```bash
git add packages/scene-render packages/sandbox CMakeLists.txt cmake/EngineRuntimeApp.cmake tools/check-invariants.ps1 docs/ARCHITECTURE.md docs/ROADMAP.md
git commit -m "refactor(scene-render): the scene-to-renderer bridge becomes engine capability (A2)"
git push
```

The `(A2)` suffix is a decision reference, not a roadmap row, so it does not
violate the `(Category #N)` convention.

---

## Task 3: Materials carry their own textures

**Files:**
- Modify: `packages/scene/include/engine/scene/world.hpp`
- Modify: `packages/scene/src/scene_file.cpp` (albedo is serialized — see Step 2a)
- Modify: `packages/sandbox/src/gates/gates_scene.cpp` (two gates read `albedo`)
- Modify: `packages/scene-render/src/extract.cpp`
- Modify: `packages/sandbox/src/main.cpp`, `sandbox_common.hpp`
- Modify: `packages/sandbox/src/gates/gates_renderer.cpp` (the existing `run_material_gate`)
- Modify: `VISION.md`

- [ ] **Step 1: Extend the gate to demand a non-default normal map**

Add to the **existing** `run_material_gate` in `gates_renderer.cpp`. This asserts the thing
that is currently impossible — that a material can name its own normal map and
the bridge honours it:

**Why here and not in the new store gate.** `run_material_gate` already
proves roughness and opacity travel the road `Material` → `ExtractInstance` →
`DrawItem`, with a probe value and a before/after extract. A texture travels
the same road, so it is asserted the same way and in the same place. Checking
`mat.normal.valid()` on a bare struct — which an earlier draft of this plan did
— proves only that the field exists, not that anything reads it.

Add after the opacity block, in that gate's established style:

```cpp
    // Textures travel the same road as roughness and opacity. Before this task
    // the bridge pinned normal_map to a built-in default, so a material naming
    // its own normal was silently ignored - which reads as a flat-looking
    // surface rather than as a bug, and is exactly the failure this gate's
    // road-checking shape exists to catch.
    //
    // The probe is material 0's albedo: a real, live texture that is
    // definitely not the default normal, and one the gate can name without a
    // device to create anything.
    const engine::assets::TextureHandle probe_handle = copy.materials[0].albedo;
    const engine::rhi::ITexture* probe = assets.textures->get(probe_handle);

    engine::scene::World textured = copy;
    for (engine::u32 i = 0; i < textured.material_count; ++i) {
        textured.materials[i].normal = probe_handle;
    }
    engine::Arena arena_textured(256 * 1024);
    engine::renderer::RenderSnapshot textured_snap{};
    textured_snap.width = 1280;
    textured_snap.height = 720;
    engine::scene_render::extract_world(textured, camera.position, assets, false, nullptr,
        arena_textured, textured_snap);

    bool normal_travels = probe != nullptr && !textured_snap.draws.empty();
    for (const engine::renderer::DrawItem& draw : textured_snap.draws) {
        if (draw.normal_map != probe) {
            normal_travels = false;
        }
    }
    // And the unmutated extract must NOT already produce it, or the assertion
    // above would hold even with the old hardcoded pin still in place. This is
    // the clause that makes the check non-vacuous.
    const bool normal_was_default = !before.draws.empty()
        && before.draws[0].normal_map != probe;
```

Add `normal_travels && normal_was_default` to `passed`, and
`normal_travels=%s normal_was_default=%s` to the message.

**Grow the message buffer.** It is `char message[288]` and the existing format
already reaches ~268 bytes at its widest (two `%zu` counts and a `%zu/%zu`
pair, all `usize`). The two new fields add ~42, so 288 truncates — and
`snprintf` truncates *silently*, taking the trailing `(FAIL)` off the end
first. Take it to `char message[384]`. This is the failure mode "every term in
`passed` must also appear in the message" exists to prevent, arriving by
truncation rather than by editing.

- [ ] **Step 2: Build and watch it fail to compile**

`scene::Material` has no `normal` member and `WorldExtractAssets` has no
`textures`, so this is a compile error rather than a red gate — expected for
*new fields*, since there is no value to stub.

Run: `cmake --build build --config Debug`
Expected: `error C2039: 'normal': is not a member of 'engine::scene::Material'`.

**After Step 4 makes it compile, the gate must still be watched red once.**
Temporarily leave the bridge's `item.normal_map = assets.default_normal;` line
in place while the rest of Step 4 lands, build, and confirm
`normal_travels=no`. That is the red proving the assertion catches the pin it
was written against — without it, the check passes from the first build and
nothing shows it ever would have failed.

- [ ] **Step 2a: `Material::albedo` is serialized — deal with that first**

`Material::albedo` is not only read by the bridge. It is written to and parsed
from the text scene format, and an existing gate asserts it round-trips:

| Where | What |
|---|---|
| `packages/scene/src/scene_file.cpp:195` | `out += std::to_string(material.albedo);` |
| `packages/scene/src/scene_file.cpp:305` | `take_u32(text, i, material.albedo)` |
| `packages/sandbox/src/gates/gates_scene.cpp:251,301` | sets `mat.albedo = 2`, asserts `loaded.materials[0].albedo == 2` |
| `packages/sandbox/content/scenes/demo.solscene:13-14` | `material 0 0 1` — three tokens |
| `packages/sandbox/src/gates/gates_scene.cpp:335,389` | `run_scene_prefab_gate` reads `albedo == 3` (prefab copy, not file I/O) |

Neither `scene_file.cpp` nor `demo.solscene` was in this task's file list. That
was the defect; this is the resolution, recorded as **S5** in
`docs/analysis/DECISIONS.md`.

**Keep the on-disk token shape. Parse it and discard it. Always write `0`.**

Do *not* serialize the handle, even though `MeshHandle` does
(`scene_file.cpp:320` writes `id generation`). A `TextureHandle` is
`fnv1a64(path + colour space)` — writing it puts an opaque 20-digit number
where a path belongs, in a file `VISION.md` says a human and an agent must both
be able to edit. The path-based form needs the store at load time, which is
`document`'s job, which is why the design spec's **D5** says no `.solscene`
changes. Put that reasoning in a comment on both sides, because a discarded
parse looks like a bug otherwise.

`demo.solscene` stays **byte-identical**. Nothing real is lost: today's
`albedo` is an index into the hardcoded husky/floor branch that Step 4 deletes.

**Replace the gate's assertion; do not delete it.** In
`run_scene_load_gate`, `albedo == 2` becomes: the loader must produce an
**invalid** handle —

```cpp
        && !loaded.materials[0].albedo.valid()
        && std::abs(loaded.materials[0].metallic - 0.1f) < 1.e-3f
        && std::abs(loaded.materials[0].roughness - 0.4f) < 1.e-3f;
```

That is positive and falsifiable: a future change that reinterprets the token
as an id goes red. And note the gate never asserted `metallic`/`roughness`
round-trip at all, so those two clauses are new — one meaningless assertion
out, two real ones in. Add the new terms to that gate's message too.

`run_scene_prefab_gate`'s `albedo == 3` is a prefab *copy*, not file I/O, so it
stays a valid assertion — swap the probe value for a handle
(`make_texture_handle("probe")` will do) and keep the check.

- [ ] **Step 3: Change `Material`**

In `packages/scene/include/engine/scene/world.hpp`, add
`#include <engine/assets/texture.hpp>` and replace `Material`:

```cpp
struct Material {
    assets::TextureHandle albedo{};
    assets::TextureHandle normal{};
    assets::TextureHandle metallic_roughness{};
    f32 metallic = 0.f;
    f32 roughness = 0.5f;
    // 1 means opaque and takes the opaque pipeline; anything less takes the
    // blended one, chosen in the scene-to-renderer bridge.
    f32 opacity = 1.f;
};
```

**Leave the caps alone.** An earlier draft of this task also raised
`kMaxInstances` to 3072 and `kMaxMaterials` to 128. That belongs to Task 5,
which is the only place the plan actually reads them (the importer's clamps),
and putting it here would have overflowed the stack — see Task 5 Step 0.

Nothing in Task 3 or Task 4 needs a bigger cap: the demo scene has five
materials and a few dozen instances. What Task 3 *does* grow is `Material`
itself, 16 → 64 bytes, which at the current 16-material cap adds 768 bytes to
`World`. That is free.

`packages/renderer/include/engine/renderer/motion.hpp` is therefore **not**
touched by this task either — `kHistorySlots` moves with `kMaxInstances`, and
`kMaxInstances` is not moving yet.

- [ ] **Step 4: Make the bridge read the store**

In `packages/scene-render/src/extract.cpp`, delete the hardcoded branch:

```cpp
        engine::rhi::ITexture* texture = nullptr;
        if (material.albedo < kHuskyVariantCount) {
            texture = assets.husky_albedos[material.albedo];
        } else if (material.albedo == kFloorAlbedoIndex) {
            texture = assets.floor_albedo;
        }
```

and replace it, along with the two pinned defaults, with store lookups:

```cpp
        // Each map falls back to a built-in when the material names none, so a
        // material with no normal renders flat rather than binding null.
        const auto* albedo = assets.textures->get(material.albedo);
        const auto* normal = assets.textures->get(material.normal);
        const auto* mr = assets.textures->get(material.metallic_roughness);
        item.texture = albedo ? albedo : assets.default_albedo;
        item.normal_map = normal ? normal : assets.default_normal;
        item.metallic_roughness = mr ? mr : assets.default_mr;
```

`DrawItem` holds a non-const `rhi::ITexture*`, so the store needs a non-const
`get()`. **That overload already exists** — Task 1's review fix shipped it
early, because the strengthened store gate's readback needed it too
(`cmd.transition`, `read_texture` and `set_shader_resource` all take
`ITexture&`). Do not add it again; just check it is there.

What this step does still have to change is the member: `textures` is a
**non-const** `GpuTextureStore*`, as written in the struct above. A
`const` pointer would only reach the `const` overload and put a `const_cast`
back at all three call sites — which is a claim the reader has to verify three
times, where the overload states once that the store hands out mutable
textures because the RHI's binding calls take them that way.

`run_material_gate` takes `const WorldExtractAssets&`, which makes
`assets.textures` a `GpuTextureStore* const` — a const *pointer* to a
non-const store, so the non-const `get()` is still reachable from the gate.
That is why the member's constness and not the parameter's is what matters
here.

Deleting that branch also makes `kHuskyVariantCount` and `kFloorAlbedoIndex`
unused in `scene-render`. They moved there with the bridge in Task 2 only
because the branch read them; **move them back to the sandbox now** (they
belong beside the demo content that defines them) and qualify or unqualify the
four sandbox users accordingly. An engine package holding two constants about a
demo husky is the leftover Task 2 was allowed to create and this task is
obliged to clean up.

In `WorldExtractAssets`, replace `husky_albedos`, `floor_albedo`,
`default_mr` and `default_normal` with:

```cpp
    engine::assets::gpu::GpuTextureStore* textures = nullptr;
    engine::rhi::ITexture* default_albedo = nullptr;
    engine::rhi::ITexture* default_normal = nullptr;
    engine::rhi::ITexture* default_mr = nullptr;
```

- [ ] **Step 5: Update the sandbox's own setup**

`main.cpp` currently loads four husky albedos and a floor texture into named
members and assigns `material.albedo = <index>`. Move those five into a
`GpuTextureStore` and assign the returned handles to the demo scene's
materials. Create the three built-in defaults — a 1×1 white, a flat normal
`(128,128,255,255)`, and a 1×1 metal-rough — through the same store.

`store()` takes a `ColorSpace` and has no default, so every one of those calls
names it: the four husky albedos and the 1×1 white are `ColorSpace::Srgb`; the
flat normal and the metal-rough are `ColorSpace::Linear`.

**The floor checker is the exception: `ColorSpace::Linear`.** It is created
`RGBA8_UNORM` today (`main.cpp:1003`) and its texels were picked to look right
sampled that way, so calling it sRGB would darken the demo floor's midtones —
a visible change to the demo that fixes nothing. The sRGB rule
(`rhi/resources.hpp:12-15`) is about authored colour, not procedurally
generated patterns. Put that reason in a comment at the call, because
`ColorSpace::Linear` on something named `albedo` will otherwise read as the
bug Task 1's review caught.

- [ ] **Step 6: Build and watch it pass**

Run:
```powershell
cmake --build build --config Debug
.\build\bin\Debug\sandbox.exe --gates
```
Expected, from `run_material_gate`: `normal_travels=yes
normal_was_default=yes` and `(pass)`, exit `0`. (An earlier draft of this step
expected a field called `carries_maps`, which Step 1 never defines.)

Also: **the husky still renders with its textures** — run without `--gates` and
look at it. A green gate here does not prove the demo scene survived the
material change, because the gate probes the material table it is handed and
Step 5 rewrites how that table is filled.

- [ ] **Step 7: `VISION.md` needs nothing from this task**

The two cap rows are still accurate, because this task no longer changes the
caps. They are updated in Task 5, where the numbers actually move. `vision-gap`
fails when a named symbol is *gone*, and both still exist with their stated
values — so it stays green without an edit here.

Check that this is still true rather than assuming it: run the invariants and
read `vision-gap`'s line.

- [ ] **Step 8: Recount and commit**

Invariants and LOC recount as in Task 1.

```bash
git add packages/scene packages/scene-render packages/sandbox docs/ROADMAP.md
git commit -m "feat(scene): materials reference their own albedo, normal and metal-rough"
git push
```

---

## Task 4: A glTF node list

**Files:**
- Modify: `packages/assets-gltf/include/engine/assets/gltf/gltf_loader.hpp`
- Modify: `packages/assets-gltf/src/gltf_loader.cpp`
- Modify: `packages/sandbox/src/gates/gates_assets.cpp`

- [ ] **Step 1: Write the failing gate**

Add a second gate to `gates_assets.cpp`. It uses the husky, which the sandbox
already mounts, and asserts the node path returns what the baked path hides:

```cpp
bool run_gltf_node_gate(engine::assets::gltf::IGltfLoader& loader,
    std::string_view husky_path) {
    engine::assets::gltf::GltfSceneResult scene{};
    const bool loaded = loader.load_scene(husky_path, scene);

    // The baked path welds every node into one mesh. The node path must not:
    // a scene with N drawable nodes has to come back as N entries, each with
    // its own transform, or nothing downstream can cull or move an object.
    const engine::u32 nodes = static_cast<engine::u32>(scene.nodes.size());
    const bool has_nodes = nodes > 0;

    // Every node must name a mesh that exists in the result.
    bool refs_ok = true;
    for (const auto& node : scene.nodes) {
        refs_ok = refs_ok && node.mesh < scene.meshes.size();
    }

    // At least one transform must differ from identity, or transforms are
    // still being baked into the vertices and the node list is decorative.
    //
    // Mat4 has no operator!=, so compare the translation column directly. A
    // node placed anywhere but the origin has a non-zero cols[3].
    bool any_transform = false;
    for (const auto& node : scene.nodes) {
        const engine::math::Vec4& t = node.transform.cols[3];
        any_transform = any_transform
            || t.x != 0.f || t.y != 0.f || t.z != 0.f;
    }

    const bool passed = loaded && has_nodes && refs_ok && any_transform;
    char message[224];
    std::snprintf(message, sizeof(message),
        "glTF node gate: loaded=%s nodes=%u meshes=%u refs_ok=%s any_transform=%s (%s)",
        loaded ? "yes" : "no", nodes, static_cast<engine::u32>(scene.meshes.size()),
        refs_ok ? "yes" : "no", any_transform ? "yes" : "no", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}
```

Declare it in `gates.hpp`, classify it `Cpu` in `kGates` with a lambda that
supplies the loader and the husky path from `CpuGateContext`, and call it from
`main.cpp`.

- [ ] **Step 2: Add the types with a stubbed `load_scene`**

In `gltf_loader.hpp`, alongside the existing types — **do not modify
`GltfLoadResult` or `GltfPrimitive`**, three gates and the husky depend on
them:

```cpp
// One drawable node: a world transform and the mesh and material it draws.
// Additive - the baked GltfLoadResult path above stays, because the husky and
// three gates use it.
struct GltfNode {
    math::Mat4 transform = math::Mat4::identity();
    u32 mesh = 0;
    u32 material = 0;
};

struct GltfMaterial {
    std::string albedo_uri;
    std::string metallic_roughness_uri;
    std::string normal_uri;
    f32 metallic = 0.f;
    f32 roughness = 1.f;
};

struct GltfSceneResult {
    std::vector<MeshData> meshes;
    std::vector<GltfMaterial> materials;
    std::vector<GltfNode> nodes;
};
```

and on `IGltfLoader`:

```cpp
    // Returns nodes with their transforms rather than one welded mesh.
    virtual bool load_scene(std::string_view path, GltfSceneResult& out) = 0;
```

Implement it in `gltf_loader.cpp` as a stub that returns `true` and leaves
`out` empty — that empty result is the red.

Add `#include <engine/math/mat4.hpp>` to the header, and `engine::math` to
`assets-gltf`'s `PUBLIC_DEPS` in its `CMakeLists.txt` if not already present.

- [ ] **Step 3: Build and watch it fail**

Run:
```powershell
cmake --build build --config Debug
.\build\bin\Debug\sandbox.exe --gates
```
Expected:
```
glTF node gate: loaded=yes nodes=0 meshes=0 refs_ok=yes any_transform=no (FAIL)
```
and exit `1`.

**Do not proceed until you have seen `nodes=0`.**

- [ ] **Step 4: Implement the node walk**

In `gltf_loader.cpp`, implement `load_scene` with this shape. The transform
composition and the vertex unpack already exist for the baked path — **call
them, do not write second copies**; two walks that can disagree about winding
or the cofactor normal transform is exactly the drift this codebase keeps
adding invariants against.

```cpp
bool GltfLoader::load_scene(std::string_view path, GltfSceneResult& out) {
    cgltf_data* data = nullptr;
    if (!parse_and_validate(path, &data)) {   // the same helper the baked path uses
        return false;
    }

    // Unpack each mesh once. The alley has 1,254 meshes behind 2,806 nodes, so
    // unpacking per node would triple the work and the memory.
    std::unordered_map<const cgltf_mesh*, u32> mesh_index;
    std::unordered_map<const cgltf_material*, u32> material_index;

    for (const cgltf_node* node = data->nodes; node != data->nodes + data->nodes_count; ++node) {
        if (node->mesh == nullptr) {
            continue;   // a transform-only node: real in glTF, not drawable
        }
        auto found = mesh_index.find(node->mesh);
        if (found == mesh_index.end()) {
            MeshData mesh{};
            if (!unpack_mesh(*node->mesh, mesh)) {   // shared with the baked path
                cgltf_free(data);
                return false;
            }
            found = mesh_index.emplace(node->mesh, static_cast<u32>(out.meshes.size())).first;
            out.meshes.push_back(std::move(mesh));
        }

        const cgltf_material* mat = node->mesh->primitives_count > 0
            ? node->mesh->primitives[0].material : nullptr;
        auto mat_found = material_index.find(mat);
        if (mat_found == material_index.end()) {
            mat_found = material_index.emplace(
                mat, static_cast<u32>(out.materials.size())).first;
            out.materials.push_back(read_material(mat));   // uri + factor extraction
        }

        GltfNode entry{};
        // World transform through the parent chain - the same composition the
        // baked path performs, except it is stored rather than applied to the
        // vertices. That is the whole difference between the two paths.
        entry.transform = world_transform_of(*node);
        entry.mesh = found->second;
        entry.material = mat_found->second;
        out.nodes.push_back(entry);
    }

    cgltf_free(data);
    return true;
}
```

Three details that matter:

- **A node with no mesh is skipped, not an error.** glTF uses transform-only
  nodes for grouping, and the alley has 2,808 nodes of which 2,806 draw.
- **Material is taken from the mesh's first primitive.** A multi-material mesh
  therefore imports under one material. That is a known simplification; note it
  in the commit, because the alley has 1,377 primitives across 1,254 meshes so
  a handful of meshes lose their second material.
- **`nullptr` is a valid material key** — a mesh with no glTF material maps to
  one default entry rather than one per node.

- [ ] **Step 5: Build and watch it pass**

Expected: `nodes=` a non-zero count, `refs_ok=yes`, `any_transform=yes`,
`(pass)`, exit `0`. The husky's own gates must still pass unchanged — the baked
path was not touched.

- [ ] **Step 6: Recount and commit**

```bash
git add packages/assets-gltf packages/sandbox docs/ROADMAP.md
git commit -m "feat(assets-gltf): a node-list load path that does not bake transforms"
git push
```

---

## Task 5: Import the alley

- [ ] **Step 0: Raise the caps, and pay for them**

This step was Task 3's until it was measured. `World` is a fixed-array POD
whose size scales with `kMaxInstances` — and **19 `scene::World` objects are
function-scope stack locals** in the gate files, at a 1 MiB default stack with
no `/STACK` anywhere in the tree.

Measured, compiled against the real headers:

| | `Material` | `NameTable` | `World` |
|---|---|---|---|
| 512 / 16 | 16 | 16,420 | **66,136** |
| 512 / 16, Task 3's `Material` | 64 | 16,420 | **66,904** |
| 3072 / 128 | 64 | 98,340 | **401,752** |

`NameTable` is `chars[kMaxInstances + 1][32]`, so it scales with the instance
cap too — a third of the growth is names.

Three gate functions overflow outright at 3072:

| Function | `World` locals | Stack |
|---|---|---|
| `run_scene_prefab_gate` (`gates_scene.cpp:321`) | 5 | **1.92 MiB** |
| `run_material_gate` (`gates_renderer.cpp:309`) | 3, +1 from Task 3 | **1.53 MiB** |
| `run_scene_file_gate` (`gates_scene.cpp:230`) | 3 | **1.15 MiB** |

So: **heap-hold every `World` local in the gate files first**, with
`auto w = std::make_unique<engine::scene::World>();`, then raise the caps.

```cpp
constexpr u32 kMaxInstances = 3072;
constexpr u32 kMaxMaterials = 128;
```

and in `packages/renderer/include/engine/renderer/motion.hpp`:

```cpp
inline constexpr u32 kHistorySlots = 3072;
```

`kHistorySlots` must move with `kMaxInstances` — the `static_assert` in the
bridge enforces it, and without it instances past the slot count silently get
`prev_model == model` and TAA reprojects them wrongly.

**This is not `Scene #12`.** That row is *"Heap-backed `World`, past the fixed
512 array"* — changing the **type** so its arrays are heap-allocated, which
costs the trivially-copyable property and the compile-time bound that protects
`read_world`, and which is blocked on `Scene #11`. Heap-*holding* a local
changes no type and loses neither property; the `World` on the heap is the same
fixed-array POD it was on the stack. Do not touch `World`'s definition beyond
the two constants.

Order matters, and the order gives you a real red for free: raise the caps
**before** heap-holding and `--gates` dies with a stack overflow rather than a
`FAIL` line. Watch that once if you like — it is the failure a green pass count
cannot show — but land the heap-holding first.

Then update **`VISION.md`**: `vision-gap` fails when a named symbol is gone,
and `kMaxInstances`/`kMaxMaterials` still exist but their stated values are now
wrong. Update the two rows' descriptions, and say in the surrounding text that
the cap is a limit rather than a hard wall. Do **not** delete the rows — the
caps still exist and the vision wants them gone entirely.


**Files:**
- Create: `tools/downscale-textures.ps1`
- Create: `packages/sandbox/src/scene_import.cpp`, `scene_import.hpp`
- Modify: `packages/sandbox/CMakeLists.txt` if it lists sources, else nothing
- Modify: `packages/sandbox/src/main.cpp`

- [ ] **Step 1: Write the downscale script**

`tools/downscale-textures.ps1` — resamples every PNG and JPG in a directory to
a maximum edge, writing PNGs to an output directory. Uses `System.Drawing`,
which ships with Windows:

```powershell
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$In,
    [Parameter(Mandatory)] [string]$Out,
    [int]$MaxEdge = 512
)

Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force $Out | Out-Null

$files = Get-ChildItem -Path $In -File | Where-Object { $_.Extension -match '\.(png|jpg|jpeg)$' }
$done = 0
foreach ($f in $files) {
    $src = [System.Drawing.Image]::FromFile($f.FullName)
    $scale = [Math]::Min(1.0, $MaxEdge / [Math]::Max($src.Width, $src.Height))
    $w = [Math]::Max(1, [int]($src.Width * $scale))
    $h = [Math]::Max(1, [int]($src.Height * $scale))
    $dst = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($dst)
    $g.InterpolationMode = 'HighQualityBicubic'
    $g.DrawImage($src, 0, 0, $w, $h)
    $g.Dispose()
    $dst.Save((Join-Path $Out ($f.BaseName + '.png')), [System.Drawing.Imaging.ImageFormat]::Png)
    $dst.Dispose(); $src.Dispose()
    $done++
}
Write-Host "resampled $done file(s) to max edge $MaxEdge"
```

Run it:
```powershell
pwsh -NoProfile -File tools/downscale-textures.ps1 `
  -In "C:\Users\mathi\Downloads\alley" `
  -Out "packages\sandbox\content\scenes\alley\textures" -MaxEdge 512
```
Expected: `resampled 286 file(s) to max edge 512`. The output directory should
be roughly 30–60 MB against the source's 1.3 GB.

**This directory must not be committed.** Add it to `.gitignore` — the repo is
public and a scene's textures are not engine source.

- [ ] **Step 2: Write the importer**

`packages/sandbox/src/scene_import.cpp`:

```cpp
// glTF -> scene::World. TEMPORARY: `document` (step 3 of the editor
// architecture spec) replaces this with the real per-entity text format. Do
// not grow it - if this file starts wanting structure, that is the signal to
// build `document` rather than to refactor here.

bool import_gltf_scene(const engine::assets::gltf::GltfSceneResult& src,
    engine::rhi::IDevice& device, engine::assets::IAssetLoader& loader,
    engine::assets::gpu::GpuMeshStore& meshes,
    engine::assets::gpu::GpuTextureStore& textures,
    engine::scene::World& out, ImportStats& stats) {

    // One helper for all three maps: resolve the URI against the glTF's own
    // directory, decode, and store keyed on the RESOLVED PATH. Keying on the
    // URI as written would re-upload a texture two materials reference by
    // different relative paths, and the scene would render identically.
    //
    // `space` is a parameter and not a default, because the three maps do not
    // agree: albedo is colour, normal and metal-rough are data. It is also
    // part of the store's key, so one file used both ways is two textures.
    auto load_texture = [&](std::string_view uri,
                            engine::assets::gpu::ColorSpace space)
        -> engine::assets::TextureHandle {
        if (uri.empty()) {
            return {};   // the bridge falls back to a built-in default
        }
        std::string resolved;
        if (!resolve_content(loader, uri, resolved)) {
            ++stats.missing_textures;
            return {};
        }
        engine::assets::ImageData image{};
        if (!engine::assets::png::load_png_file(resolved, image)) {
            ++stats.missing_textures;
            return {};
        }
        return textures.store(device, resolved, image, space);
    };

    for (const auto& m : src.materials) {
        if (out.material_count >= engine::scene::kMaxMaterials) {
            log_capacity("materials", src.materials.size(), engine::scene::kMaxMaterials);
            break;
        }
        engine::scene::Material dst{};
        dst.albedo = load_texture(m.albedo_uri, engine::assets::gpu::ColorSpace::Srgb);
        dst.normal = load_texture(m.normal_uri, engine::assets::gpu::ColorSpace::Linear);
        dst.metallic_roughness =
            load_texture(m.metallic_roughness_uri, engine::assets::gpu::ColorSpace::Linear);
        dst.metallic = m.metallic;
        dst.roughness = m.roughness;
        out.materials[out.material_count++] = dst;
    }

    std::vector<engine::assets::MeshHandle> mesh_handles;
    mesh_handles.reserve(src.meshes.size());
    for (engine::usize i = 0; i < src.meshes.size(); ++i) {
        char key[64];
        std::snprintf(key, sizeof(key), "gltf_mesh_%zu", i);
        mesh_handles.push_back(meshes.store(device, key, src.meshes[i]));
    }

    for (const auto& node : src.nodes) {
        if (out.instance_count >= engine::scene::kMaxInstances) {
            log_capacity("instances", src.nodes.size(), engine::scene::kMaxInstances);
            break;
        }
        if (node.material >= out.material_count || node.mesh >= mesh_handles.size()) {
            ++stats.skipped_nodes;
            continue;
        }
        auto& inst = out.instances[out.instance_count++];
        inst.mesh = mesh_handles[node.mesh];
        inst.model = node.transform;
        inst.material = static_cast<engine::scene::MaterialHandle>(node.material);
        inst.parent = engine::scene::kInvalidInstance;   // transforms are already world
    }

    stats.materials = out.material_count;
    stats.instances = out.instance_count;
    stats.textures = static_cast<engine::u32>(textures.size());
    return true;
}
```

`ImportStats` carries `materials`, `instances`, `textures`, `skipped_nodes` and
`missing_textures`, and is logged after the import. Those last two matter: a
scene that half-loaded because textures were missing otherwise looks like a
scene that rendered badly.

**Capacity is logged, not asserted.** `world.hpp`'s own rule is that how many
instances a scene has is a content outcome, not a programmer error — so
exceeding a cap stops the loop and says so, rather than aborting.

**`parent` is the invalid sentinel** because `world_transform_of` already
composed the parent chain. Setting a parent here would apply it twice.

- [ ] **Step 3: Load it behind a cvar**

In `main.cpp`, when `r.scene` names a glTF path, import it instead of building
the husky demo scene. Default empty, so the existing demo and every gate are
untouched.

```powershell
.\build\bin\Debug\sandbox.exe --set r.scene=/scenes/alley/ph_hidden_alley.gltf
```

- [ ] **Step 4: Run it and look**

Expected: the alley renders, walkable with the existing fly camera. Record
what you actually see — including anything wrong — rather than only whether it
launched.

Then run `--gates` and confirm the husky demo path still passes, since the
default cvar leaves it selected.

- [ ] **Step 5: Recount and commit**

```bash
git add tools/downscale-textures.ps1 packages/scene packages/renderer packages/sandbox     .gitignore docs/ROADMAP.md VISION.md
git commit -m "feat(sandbox): import a glTF scene into the world behind r.scene"
git push
```

---

## Task 6: Close out

- [ ] **Step 1: Both gate paths and the release build**

Run:
```powershell
cmake --build build --config Release --target game
.\build\bin\Release\game.exe --gates
.\build\bin\Debug\sandbox.exe --gates-cpu
```
Expected: all exit `0`. Confirm `run_gltf_node_gate`'s line appears in the
`--gates-cpu` output and `run_texture_store_gate`'s does **not** — the first is
`Cpu`, the second `Gpu`, and a misclassification would let a gate silently stop
running.

- [ ] **Step 2: The GPU debug layers**

Run:
```powershell
$env:ENGINE_GPU_DEBUG=1
.\build\bin\Debug\sandbox.exe --gates
.\build\bin\Debug\sandbox.exe --gates --rhi vulkan
```
Expected: exit `0` both times, no debug-layer or validation messages. This
change uploads far more textures than before; a descriptor or barrier problem
would surface here and nowhere else.

- [ ] **Step 3: Flip the rows**

The two demo-scene constants `kHuskyVariantCount` and `kFloorAlbedoIndex` moved
into `engine::scene_render` with the bridge in Task 2, because the hardcoded
albedo branch still read them. Task 3 Step 4 deletes that branch — confirm they
went back to the sandbox with it. An engine package holding two constants about
a demo husky is exactly the kind of leftover a pure move is allowed to create
and the next task is obliged to clean up.

In `docs/ENGINE_MAP.md`, mark **Renderer #7** ("Materials as data") — it is
currently **Done** describing the scalar-only material, which this work
supersedes. Reword it to say materials carry texture references, keeping it
Done. Update **Scene #1**'s "(512)" to the new cap.

Update the header total and the category subtotals if any status changed.
`map-dependencies` checks that they agree.

- [ ] **Step 4: The ROADMAP entry**

Add above the newest entry, in the house Why / Choice / Gate / Do-not shape.
Cover: that this answers open decision **A2** by moving the bridge into
`scene-render`; that the texture store's dedupe is the assertion the whole
thing rests on, since a non-deduping store renders identically; that the glTF
node path is additive because the husky and three gates use the baked one; that
the caps rose without needing `Scene #12`, because `World` was already
heap-held; and the measured result on the alley — the instance, material and
texture counts it actually loaded.

Do-not: do not key the texture store on the resolved path alone — the colour
space is part of the key, because one image legitimately serves as an sRGB
albedo and a linear mask and a path-only key hands the second caller the first
one's format with the dedupe count still reading as correct; do not
remove the baked glTF path; do not grow `scene_import.cpp`, which `document`
replaces; do not raise `kMaxInstances` without moving `kHistorySlots`.

- [ ] **Step 5: Answer decision A2 on the service**

The service owns the decision list, not this repo (see CLAUDE.md's "The
management service"). Task 2 answered **A2** — the scene→renderer bridge is no
longer application code — so record it there rather than leaving the answer
implicit in a commit subject:

```powershell
pwsh -NoProfile -File tools/aim.ps1 decisions answer A2 `
    --note "..." --ref packages/scene-render
```

`docs/analysis/DECISIONS.md:108` still describes A2 in the present tense and
names the deleted `packages/sandbox/src/world_extract.cpp`. Update that entry
too, since it is the local rescued copy and not a dated snapshot.

- [ ] **Step 6: Recount last, verify, commit**

Run the invariants, update the LOC sentence from `roadmap-audit`'s recount, and
re-run until `all 20 checks passed`.

```bash
git add docs/ENGINE_MAP.md docs/ROADMAP.md
git commit -m "docs(scene-render): materials with textures, and a scene the engine did not ship with"
git push
```

---

## Definition of done

- [x] `run_texture_store_gate` seen red at `uploaded=8 resident=4`, then green at
      `uploaded=4`. The strengthened gate counts *uploads*, not residency — the
      original `uploaded=6`/`3` pair measured live entries, which a store that
      re-uploads over a live entry satisfies while making eight upload calls
- [ ] `run_gltf_node_gate` seen red at `nodes=0`, then green
- [ ] The existing `run_material_gate` seen red at `normal_travels=no` before the
      bridge stopped pinning `normal_map` to the default
- [x] Task 2's before/after gate output diffed with no differences **outside the
      minidump byte count and the async-compile timings**. Those two lines differ
      between two consecutive runs of one unchanged binary, so establish that
      baseline noise floor first and mask exactly those fields — a mask chosen
      without proving it hides only noise would hide real drift too
- [ ] The husky demo still renders correctly, checked visually, not just gated
- [ ] The alley loads and is walkable, with its counts recorded
- [ ] `--gates` exits `0` on Debug `sandbox`, Release `game`, and `--rhi vulkan`
- [ ] `ENGINE_GPU_DEBUG=1` silent on both backends
- [ ] `run_gltf_node_gate` appears in `--gates-cpu`; `run_texture_store_gate` does not
- [ ] `all 20 checks passed`
- [ ] `VISION.md`'s two cap rows updated
- [ ] Every commit pushed to `main`

## Out of scope

- **BC7 compression** (`Assets #5`) — downscaling removes the need
- **Cooking to `.solscene`** — `document`, step 3
- **Relative mouse** (`Platform #10`) — independent Ready row
- **Mesh collision** — `ShapeType` has no mesh shape; fly-camera only
- **HDRI sky** — `ibl::bake()` is procedural; separate work
- **Moving the importer into a package** — `document` replaces it
