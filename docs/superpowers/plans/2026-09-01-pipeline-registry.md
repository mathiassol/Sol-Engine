# Pipeline Registry Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the hand-copied pipeline plumbing with one `FramePipelines`
type carried by value, so adding a render pass costs 3 edits instead of 8 and a
forgotten step is a build error or a red gate instead of a silently dead pass.

**Architecture:** A plain struct of 13 named `IGraphicsPipeline*` fields lives in
`renderer`, embedded by value in `WorldExtractAssets`, `ExtractDesc` and
`RenderSnapshot` so the three copy blocks become one assignment each. A
`constexpr` pointer-to-member table beside it makes the struct iterable for
diagnostics, and a `static_assert` comparing the struct's size to the table's
length makes a field without an entry fail the build.

**Tech Stack:** C++20, MSVC `/W4 /permissive-`, CMake 4.2 + Visual Studio 18
2026 generator, D3D12.

**Spec:** `docs/superpowers/specs/2026-08-31-pipeline-registry-design.md`

---

## Read this before Task 1 — five project rules that will bite you

This repository does not work the way most do. Each of these has cost real time.

1. **There is no test framework. A test is a *gate*** — a plain function in
   `packages/sandbox/src/main.cpp` that asserts on real values and logs a line
   ending `(pass)` or `(FAIL)`, called from the gate sequence. TDD here means
   write the gate, run it, *watch it fail*, then implement. See "What a gate is"
   in `CLAUDE.md`.

2. **Every commit must be green.** `--gates` must exit 0 and
   `check-invariants.ps1` must pass 13/13. A red gate is never committed, so
   "watch it fail" is a step you perform, not a state you commit.

3. **`docs/ROADMAP.md` is an implicit dependency of every source change.** The
   `roadmap-audit` invariant recounts C++/HLSL lines and fails if
   `docs/ROADMAP.md:50` disagrees. **Any task that adds or removes source lines
   must update that line**, or the phase goes red. Get the number from the
   failure message itself — it prints the recount.

4. **Never run `clang-format`.** `.clang-format` is descriptive only; the tree is
   not formatter-clean by design (108 of 141 files diverge). Match the lines
   around what you touch, by hand.

5. **Trunk-based.** Commit to `main` and push after each task. No branches, no
   worktrees, no PRs.

### The verification block

Run this at the end of every task. It is the definition of green.

```powershell
cmake --build build --config Debug
.\build\bin\Debug\sandbox.exe --gates
$env:ENGINE_GPU_DEBUG=1; .\build\bin\Debug\sandbox.exe --gates; $env:ENGINE_GPU_DEBUG=$null
cmake --build build --config Release --target game
.\build\bin\Release\game.exe --gates
pwsh -NoProfile -File tools/check-invariants.ps1
```

Expected: build clean; **73 `(pass)` lines and 0 `FAIL`** in both configurations
before Task 2, **74 from Task 2 onward**; `D3D12 debug layer: 0 message(s), 0
error(s), 0 warning(s)`; `all 13 checks passed`.

### The one mapping table, used by Tasks 2 and 3

Every rename in this plan comes from this table. `FramePipelines` field names are
the short forms, chosen because `WorldExtractAssets` already uses them.

| `FramePipelines` field | `ForwardDemo` member (old) | `ExtractDesc` / `RenderSnapshot` field (old) | `WorldExtractAssets` field (old) |
|---|---|---|---|
| `forward` | `pipeline` | — (travels per-item) | `forward` |
| `shadow` | `shadow_pipeline` | `shadow_pipeline` | `shadow` |
| `sky` | `sky_pipeline` | `sky_pipeline` | `sky` |
| `bloom_downsample` | `bloom_downsample_pipeline` | `bloom_downsample_pipeline` | `bloom_downsample` |
| `bloom_upsample` | `bloom_upsample_pipeline` | `bloom_upsample_pipeline` | `bloom_upsample` |
| `tonemap` | `tonemap_pipeline` | `tonemap_pipeline` | `tonemap` |
| `tonemap_aces` | `tonemap_aces_pipeline` | `tonemap_aces_pipeline` | `tonemap_aces` |
| `fxaa` | `fxaa_pipeline` | `fxaa_pipeline` | `fxaa` |
| `smaa_edge` | `smaa_edge_pipeline` | `smaa_edge_pipeline` | `smaa_edge` |
| `smaa_weights` | `smaa_weights_pipeline` | `smaa_weights_pipeline` | `smaa_weights` |
| `smaa_blend` | `smaa_blend_pipeline` | `smaa_blend_pipeline` | `smaa_blend` |
| `motion` | `motion_pipeline` | `motion_pipeline` | `motion` |
| `taa` | `taa_pipeline` | `taa_pipeline` | `taa` |

`ExtractDesc` and `RenderSnapshot` have no `forward` field today and will gain
one by embedding the struct. It is unused there. That is the deliberate cost of
one uniform set — see the spec's Design section.

---

## Task 1: The identity type and its assert

**Files:**
- Create: `packages/renderer/include/engine/renderer/frame_pipelines.hpp`

No CMake change: `packages/renderer/CMakeLists.txt` lists `SOURCES` only, so
headers are picked up by the include directory.

- [ ] **Step 1: Write the header**

Create `packages/renderer/include/engine/renderer/frame_pipelines.hpp`:

```cpp
#pragma once

#include <engine/core/types.hpp>
#include <engine/rhi/resources.hpp>

namespace engine::renderer {

// Every pipeline a frame binds, in one place, carried by value from the app to
// the render graph. Before this existed the same 13 pointers were hand-copied
// through four structs under two different spellings, and a forgotten copy line
// disabled a pass with no diagnostic - 7 of the 16 should_execute predicates
// read null as "feature off".
//
// `forward` is here even though the renderer consumes it per-batch rather than
// per-frame (world_extract.cpp assigns DrawItem::pipeline). Uniformity is the
// point: the completeness gate covers it and it reads like its twelve siblings.
struct FramePipelines {
    rhi::IGraphicsPipeline* forward = nullptr;
    rhi::IGraphicsPipeline* shadow = nullptr;
    rhi::IGraphicsPipeline* sky = nullptr;
    rhi::IGraphicsPipeline* bloom_downsample = nullptr;
    rhi::IGraphicsPipeline* bloom_upsample = nullptr;
    rhi::IGraphicsPipeline* tonemap = nullptr;
    rhi::IGraphicsPipeline* tonemap_aces = nullptr;
    rhi::IGraphicsPipeline* fxaa = nullptr;
    rhi::IGraphicsPipeline* smaa_edge = nullptr;
    rhi::IGraphicsPipeline* smaa_weights = nullptr;
    rhi::IGraphicsPipeline* smaa_blend = nullptr;
    rhi::IGraphicsPipeline* motion = nullptr;
    rhi::IGraphicsPipeline* taa = nullptr;
};

// Pairs each field with a name, so the set can be iterated - which is what the
// completeness gate needs, and what a named-fields-only design cannot give
// without a second hand-maintained list.
struct FramePipelineEntry {
    rhi::IGraphicsPipeline* FramePipelines::*field;
    const char* name;
};

inline constexpr FramePipelineEntry kFramePipelines[] = {
    {&FramePipelines::forward, "forward"},
    {&FramePipelines::shadow, "shadow"},
    {&FramePipelines::sky, "sky"},
    {&FramePipelines::bloom_downsample, "bloom_downsample"},
    {&FramePipelines::bloom_upsample, "bloom_upsample"},
    {&FramePipelines::tonemap, "tonemap"},
    {&FramePipelines::tonemap_aces, "tonemap_aces"},
    {&FramePipelines::fxaa, "fxaa"},
    {&FramePipelines::smaa_edge, "smaa_edge"},
    {&FramePipelines::smaa_weights, "smaa_weights"},
    {&FramePipelines::smaa_blend, "smaa_blend"},
    {&FramePipelines::motion, "motion"},
    {&FramePipelines::taa, "taa"},
};

inline constexpr usize kFramePipelineCount
    = sizeof(kFramePipelines) / sizeof(kFramePipelines[0]);

// A field with no table entry cannot be filled by the creation loop, checked by
// the gate, or named in a log - it would fail in exactly the silent way this
// type exists to remove. A table entry with no field does not compile. This
// makes the remaining case a build error too. Same idiom as
// static_assert(sizeof(InstanceData) == 144) in render_snapshot.hpp, and the
// enum-plus-names assert in physics_cpu.cpp.
static_assert(sizeof(FramePipelines) == kFramePipelineCount * sizeof(void*),
    "every FramePipelines field needs exactly one kFramePipelines entry");

} // namespace engine::renderer
```

- [ ] **Step 2: Prove the assert bites — add a field with no entry**

Temporarily add a 14th field immediately after `taa`:

```cpp
    rhi::IGraphicsPipeline* taa = nullptr;
    rhi::IGraphicsPipeline* deliberate_mistake = nullptr;   // TEMPORARY
```

Then include the header from a translation unit that already compiles, so it is
actually instantiated. Add this line to
`packages/renderer/src/standard_frame.cpp` under its existing includes:

```cpp
#include <engine/renderer/frame_pipelines.hpp>
```

- [ ] **Step 3: Build and watch it fail**

Run: `cmake --build build --config Debug`

Expected: a compile error naming the assert, of the form
`error C2338: static_assert failed: 'every FramePipelines field needs exactly one kFramePipelines entry'`
in `frame_pipelines.hpp`.

If it builds clean, the header is not being instantiated — check that the
`#include` in Step 2 landed.

- [ ] **Step 4: Remove the deliberate mistake and rebuild**

Delete the `deliberate_mistake` line. Keep the `#include` in
`standard_frame.cpp` — Task 3 needs it anyway.

Run: `cmake --build build --config Debug`
Expected: clean.

- [ ] **Step 5: Verify and commit**

Run the verification block. Expected: 73 `(pass)`, 0 `FAIL`, 13/13 invariants.
No `ROADMAP.md` update is needed *if* the recount happens to be unchanged —
check the invariant output and update `docs/ROADMAP.md:50` if `roadmap-audit`
fails, using the number it prints.

```bash
git add packages/renderer/include/engine/renderer/frame_pipelines.hpp \
        packages/renderer/src/standard_frame.cpp docs/ROADMAP.md
git commit -m "feat(renderer): FramePipelines, one identity per frame pipeline (analizeMax A1)"
git push origin main
```

---

## Task 2: Ownership in ForwardDemo, and the completeness gate

**Files:**
- Modify: `packages/sandbox/src/main.cpp` — `ForwardDemo` (lines 206-220), the
  creation sites (5157, 5171, the table at 5182-5216), the 23 `demo->*_pipeline`
  uses, the 2 gate parameters at 2461 and 2481
- Modify: `docs/ROADMAP.md:50`

- [ ] **Step 1: Write the failing gate**

Add this immediately above `bool run_scene_capacity_gate() {` in
`packages/sandbox/src/main.cpp`:

```cpp
// The safety net for the whole registry: every entry must have been created.
// A forgotten creation row makes --gates red here instead of producing a frame
// that is quietly missing a pass, which is what the old hand-copied plumbing
// did. Iterating kFramePipelines is why the table exists.
bool run_pipeline_set_gate(const engine::renderer::FramePipelines& pipelines) {
    engine::u32 present = 0;
    const char* first_missing = nullptr;
    for (engine::usize k = 0; k < engine::renderer::kFramePipelineCount; ++k) {
        const auto& entry = engine::renderer::kFramePipelines[k];
        if (pipelines.*(entry.field) != nullptr) {
            present += 1;
        } else if (!first_missing) {
            first_missing = entry.name;
        }
    }
    const bool passed = present == engine::renderer::kFramePipelineCount;
    char message[160];
    std::snprintf(message, sizeof(message),
        "Pipeline set gate: present=%u/%zu missing=%s (%s)",
        present, engine::renderer::kFramePipelineCount,
        first_missing ? first_missing : "none",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}
```

`usize` is `std::size_t` (`core/types.hpp:21`), so `%zu` is correct. `main.cpp`
already includes `<vector>`, needed for `owned` in Step 2.
```

Add the include beside the other `engine/renderer/` includes near the top of
`main.cpp`:

```cpp
#include <engine/renderer/frame_pipelines.hpp>
```

Call it from the gate sequence, immediately after the `run_scene_load_gate`
block added by C1:

```cpp
    if (!run_pipeline_set_gate(demo->pipelines) && fail_on_gate) {
        return false;
    }
```

- [ ] **Step 2: Add the new members, populate nothing, and watch it fail**

In `ForwardDemo` (around line 207), **add** these two members. Do not remove the
13 old `unique_ptr` members yet — this step exists to see red:

```cpp
    engine::renderer::FramePipelines pipelines;
    std::vector<std::unique_ptr<engine::rhi::IGraphicsPipeline>> owned;
```

Run: `cmake --build build --config Debug` then
`.\build\bin\Debug\sandbox.exe --gates`

Expected: **FAIL**, with the line
`Pipeline set gate: present=0/13 missing=forward (FAIL)` and exit code 1.
That is the gate proving it measures something.

- [ ] **Step 3: Move creation onto the new members**

Replace the forward pipeline creation at lines 5157-5162, which currently reads
exactly:

```cpp
    demo->pipeline = device->create_graphics_pipeline(
        make_forward_pipeline_desc(vs_bytecode.data, ps_bytecode.data));
    if (!demo->pipeline) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render, "Forward pipeline creation failed");
        return false;
    }
```

with:

```cpp
    {
        auto p = device->create_graphics_pipeline(
            make_forward_pipeline_desc(vs_bytecode.data, ps_bytecode.data));
        if (!p) {
            engine::log(engine::LogLevel::Error, engine::LogChannel::Render, "Forward pipeline creation failed");
            return false;
        }
        demo->pipelines.forward = p.get();
        demo->owned.push_back(std::move(p));
    }
```

Replace the shadow creation at lines 5171-5175, which currently reads exactly:

```cpp
    demo->shadow_pipeline = device->create_graphics_pipeline(make_shadow_pipeline_desc(shadow_bytecode.data));
    if (!demo->shadow_pipeline) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render, "Shadow pipeline creation failed");
        return false;
    }
```

with:

```cpp
    {
        auto p = device->create_graphics_pipeline(
            make_shadow_pipeline_desc(shadow_bytecode.data));
        if (!p) {
            engine::log(engine::LogLevel::Error, engine::LogChannel::Render, "Shadow pipeline creation failed");
            return false;
        }
        demo->pipelines.shadow = p.get();
        demo->owned.push_back(std::move(p));
    }
```

Change the fullscreen table's `field` column from a pointer-to-member on
`ForwardDemo` to one on `FramePipelines`:

```cpp
    const struct {
        engine::rhi::IGraphicsPipeline* engine::renderer::FramePipelines::*field;
        const std::string& path;
        const char* name;
        MakePipelineDesc make_desc;
    } pipelines[] = {
        {&engine::renderer::FramePipelines::tonemap, tonemap_path, "Tonemap",
         make_tonemap_pipeline_desc},
        {&engine::renderer::FramePipelines::sky, sky_path, "Sky",
         make_sky_pipeline_desc},
        {&engine::renderer::FramePipelines::bloom_downsample, bloom_down_path,
         "Bloom downsample", make_bloom_downsample_pipeline_desc},
        {&engine::renderer::FramePipelines::bloom_upsample, bloom_up_path,
         "Bloom upsample", make_bloom_upsample_pipeline_desc},
        {&engine::renderer::FramePipelines::fxaa, fxaa_path, "FXAA",
         make_fxaa_pipeline_desc},
        {&engine::renderer::FramePipelines::smaa_edge, smaa_edge_path,
         "SMAA edge", make_smaa_edge_pipeline_desc},
        {&engine::renderer::FramePipelines::smaa_weights, smaa_weights_path,
         "SMAA weights", make_smaa_weights_pipeline_desc},
        {&engine::renderer::FramePipelines::smaa_blend, smaa_blend_path,
         "SMAA blend", make_smaa_blend_pipeline_desc},
        {&engine::renderer::FramePipelines::motion, motion_path,
         "Motion vector", make_motion_pipeline_desc},
        {&engine::renderer::FramePipelines::taa, taa_path, "TAA",
         make_taa_pipeline_desc},
        {&engine::renderer::FramePipelines::tonemap_aces, tonemap_aces_path,
         "ACES tonemap", make_tonemap_aces_pipeline_desc},
    };
```

Replace the loop at 5211 so it owns via `owned` and identifies via `pipelines`:

```cpp
    for (const auto& entry : pipelines) {
        std::unique_ptr<engine::rhi::IGraphicsPipeline> p;
        if (!build_fullscreen_pipeline(*device, compiler, entry.path, entry.name,
                entry.make_desc, p)) {
            return false;
        }
        demo->pipelines.*(entry.field) = p.get();
        demo->owned.push_back(std::move(p));
    }
```

`build_fullscreen_pipeline` keeps its existing
`std::unique_ptr<IGraphicsPipeline>& out` signature; only the caller changes.

- [ ] **Step 4: Delete the old members and fix their 25 uses**

Remove all 13 `std::unique_ptr<engine::rhi::IGraphicsPipeline>` members from
`ForwardDemo` (lines 207-219).

The build will now name every remaining use. Fix each with the mapping table
above: `demo->X_pipeline.get()` becomes `demo->pipelines.Y`, and
`demo->pipeline.get()` becomes `demo->pipelines.forward`. There are 23 such uses
plus the 2 gate parameters at lines 2461 and 2481, whose types change from
`const engine::rhi::IGraphicsPipeline*` — unchanged — but whose call sites now
pass `demo->pipelines.shadow` and `demo->pipelines.tonemap`.

Leave `make_extract_assets` (lines 273-287) alone. Task 3 owns it.

- [ ] **Step 5: Watch the gate pass**

Run: `cmake --build build --config Debug` then
`.\build\bin\Debug\sandbox.exe --gates`

Expected: `Pipeline set gate: present=13/13 missing=none (pass)` and **74
`(pass)` lines**, 0 `FAIL`, exit 0.

- [ ] **Step 6: Verify and commit**

Run the full verification block. `roadmap-audit` **will** fail — this task
changes line counts. Update `docs/ROADMAP.md:50` with the number the failure
prints, then re-run until 13/13.

```bash
git add packages/sandbox/src/main.cpp docs/ROADMAP.md
git commit -m "refactor(sandbox): ForwardDemo owns pipelines by identity, not by name (analizeMax A1)"
git push origin main
```

---

### Task 2, as implemented — two corrections to the steps above

Recorded rather than rewritten, so the difference between what was planned and
what shipped stays visible.

**The append pattern in Steps 5, 6 and 8 was wrong, and would have leaked.**
`main.cpp` has a shader hot-reload path whose old code was
`demo.pipeline = std::move(pipeline)` — a `unique_ptr` assignment, which *freed*
the superseded pipeline. Appending there retains every previously-live forward
pipeline for the life of the process: one leaked D3D12 pipeline state object per
shader save. No gate catches it — the async-compile gate reloads once, and the
mesh-reload VRAM gate measures meshes, not PSOs.

The shipped code routes every hand-over through one helper on `ForwardDemo`
instead, so "`owned` holds exactly the live set" is one function's job and there
is exactly one `owned.push_back` in the file:

```cpp
    void adopt(engine::rhi::IGraphicsPipeline* engine::renderer::FramePipelines::*field,
        std::unique_ptr<engine::rhi::IGraphicsPipeline> p) {
        engine::rhi::IGraphicsPipeline* const previous = pipelines.*field;
        pipelines.*field = p.get();
        if (previous != nullptr) {
            for (auto& slot : owned) {
                if (slot.get() == previous) {
                    slot = std::move(p);
                    return;
                }
            }
        }
        owned.push_back(std::move(p));
    }
```

All four sites call it: forward creation, shadow creation, the fullscreen loop
(`demo->adopt(entry.field, std::move(p))`), and hot reload.

**The Step 9 grep was incomplete.** It anchored on the `demo->` / `demo.`
receiver and so missed two genuine member accesses reached another way: the
truthiness check on `state.forward->pipeline` inside the `on_extract` lambda, and
the hot-reload assignment. Use a receiver-agnostic search instead:

```bash
grep -n "\bpipeline\b\|[a-z_]*_pipeline\b" packages/sandbox/src/main.cpp
```

then filter out `create_graphics_pipeline`, `IGraphicsPipeline`,
`make_*_pipeline_desc`, `DrawItem::pipeline`, `DrawBatch::pipeline`,
`snapshot.*_pipeline` (a `RenderSnapshot` field, not `ForwardDemo`) and the two
gate parameters.

**Verified load-bearing, not vacuous.** Deleting one creation row gives
`Pipeline set gate: present=12/13 missing=smaa_weights (FAIL)` and exit 1. Worth
doing once when adding a gate of this kind.

---

## Task 3: Collapse the three copy blocks

**Files:**
- Modify: `packages/sandbox/src/world_extract.hpp` — `WorldExtractAssets`
- Modify: `packages/sandbox/src/world_extract.cpp` — copy block 2 (119-130), and
  line 97
- Modify: `packages/renderer/include/engine/renderer/extract.hpp` — `ExtractDesc`
  (42-53)
- Modify: `packages/renderer/include/engine/renderer/render_snapshot.hpp` —
  `RenderSnapshot` (136-147)
- Modify: `packages/renderer/src/extract.cpp` — copy block 3 (52-63)
- Modify: `packages/renderer/src/render_graph.cpp` — 24 read sites
- Modify: `packages/renderer/src/standard_frame.cpp` — 8 read sites
- Modify: `packages/sandbox/src/main.cpp` — copy block 1 (273-287)
- Modify: `docs/ROADMAP.md:50`

This task is one atomic rename. Splitting it would leave the tree non-building
between steps, because three files read fields that a fourth declares.

- [ ] **Step 1: Replace the field blocks with the embedded struct**

In `WorldExtractAssets` (`world_extract.hpp`), delete the 13
`engine::rhi::IGraphicsPipeline*` members and put in their place:

```cpp
    engine::renderer::FramePipelines pipelines;
```

Add `#include <engine/renderer/frame_pipelines.hpp>` to that header's include
block, which already includes `engine/renderer/extract.hpp`.

In `ExtractDesc` (`extract.hpp`), delete the 12 `rhi::IGraphicsPipeline*` members
at lines 42-53 and put in their place:

```cpp
    FramePipelines pipelines;
```

Add `#include <engine/renderer/frame_pipelines.hpp>` to that header.

In `RenderSnapshot` (`render_snapshot.hpp`), do the same for lines 136-147:

```cpp
    FramePipelines pipelines;
```

Add the same include. Leave `DrawBatch::pipeline` and `DrawItem::pipeline`
untouched — they are the batching key, not frame state.

- [ ] **Step 2: Collapse the three copy blocks**

`main.cpp`, in `make_extract_assets` — replace all 13 assignment lines with:

```cpp
    assets.pipelines = demo.pipelines;
```

`world_extract.cpp` lines 119-130 — replace all 12 with:

```cpp
    desc.pipelines = assets.pipelines;
```

`world_extract.cpp` line 97 — `item.pipeline = assets.forward;` becomes:

```cpp
        item.pipeline = assets.pipelines.forward;
```

`extract.cpp` lines 52-63 — replace all 12 with:

```cpp
    out.pipelines = desc.pipelines;
```

- [ ] **Step 3: Rename the read sites**

The build now names every remaining reference. Apply the mapping table: in
`render_graph.cpp` (24 sites) and `standard_frame.cpp` (8 sites),
`snapshot.X_pipeline` becomes `snapshot.pipelines.Y` and `ctx.snapshot.X_pipeline`
becomes `ctx.snapshot.pipelines.Y`. For example:

```cpp
// standard_frame.cpp, was:
        return snapshot.motion_pipeline != nullptr;
// becomes:
        return snapshot.pipelines.motion != nullptr;
```

```cpp
// standard_frame.cpp, was:
        const bool smaa = snapshot.smaa_edge_pipeline && snapshot.smaa_weights_pipeline
            && snapshot.smaa_blend_pipeline;
// becomes:
        const bool smaa = snapshot.pipelines.smaa_edge && snapshot.pipelines.smaa_weights
            && snapshot.pipelines.smaa_blend;
```

```cpp
// render_graph.cpp, was:
    if (!ctx.snapshot.tonemap_pipeline || ctx.shader_read_count == 0 || !ctx.shader_reads[0]) {
// becomes:
    if (!ctx.snapshot.pipelines.tonemap || ctx.shader_read_count == 0 || !ctx.shader_reads[0]) {
```

Repeat mechanically until the build is clean. Every one is compiler-checked;
there is no way to miss one silently.

- [ ] **Step 4: Build and prove no behaviour change**

Run: `cmake --build build --config Debug` then
`.\build\bin\Debug\sandbox.exe --gates`

Expected: **74 `(pass)`, 0 `FAIL`**, and these four lines byte-identical to
before the task — they are what catches a mis-wired rename:

```
Graph compiled: 25 passes, 19 resources
AA gate: default=off exclusive=yes after_tonemap=yes fxaa=yes (pass)
Motion gate: uv=yes camera=yes object=yes history=yes equal=yes pass=yes (pass)
TAA gate: default=off optional=yes hdr=yes jitter=yes clip=ycocg pass=yes (pass)
```

- [ ] **Step 5: Verify and commit**

Run the full verification block, including `ENGINE_GPU_DEBUG=1` — this task
changes what the renderer binds, so a silent mis-wire would show up as a debug
layer message. Update `docs/ROADMAP.md:50` from the `roadmap-audit` failure.

```bash
git add packages/sandbox/src/world_extract.hpp packages/sandbox/src/world_extract.cpp \
        packages/renderer/include/engine/renderer/extract.hpp \
        packages/renderer/include/engine/renderer/render_snapshot.hpp \
        packages/renderer/src/extract.cpp packages/renderer/src/render_graph.cpp \
        packages/renderer/src/standard_frame.cpp packages/sandbox/src/main.cpp \
        docs/ROADMAP.md
git commit -m "refactor(renderer): carry FramePipelines by value, three copy blocks become three lines (analizeMax A1)"
git push origin main
```

---

## Task 4: Log a skipped pass, once

**Files:**
- Modify: `packages/renderer/include/engine/renderer/render_graph.hpp` — one
  member on the private section of `RenderGraph`
- Modify: `packages/renderer/src/render_graph.cpp` — `execute()` around line 819,
  and `clear()`
- Modify: `docs/ROADMAP.md:50`

This is a development convenience, not the safety net — Task 2's gate is. It
cannot say *why* a pass was skipped: `should_execute` is an opaque
`std::function<bool(const RenderSnapshot&)>`. Do not add a `requires_pipeline`
field to `RenderPassDesc` to make it specific; that adds a field to the struct
this work exists to slim, to describe a case the gate already makes impossible.

- [ ] **Step 1: Add the latch**

In `render_graph.hpp`, in `RenderGraph`'s private section beside the other
per-graph state:

```cpp
    // Passes already reported as skipped. A predicate that is false every frame
    // would otherwise log at 60 Hz - the mistake alloc_frame_memory and
    // warn_physics_capacity both avoid with a latch.
    std::vector<std::string> skip_reported_;
```

`<vector>` and `<string>` are already included by that header.

- [ ] **Step 2: Log on the skip path**

Add `#include <algorithm>` to `render_graph.cpp` — it is **not** currently
included, and `std::find` needs it.

In `render_graph.cpp`, the skip branch at lines 818-821 currently reads exactly:

```cpp
    for (RenderPassDesc& pass : passes_) {
        if (pass.should_execute && !pass.should_execute(snapshot)) {
            continue;
        }
```

Replace with:

```cpp
    for (RenderPassDesc& pass : passes_) {
        if (pass.should_execute && !pass.should_execute(snapshot)) {
            if (std::find(skip_reported_.begin(), skip_reported_.end(), pass.name)
                == skip_reported_.end()) {
                skip_reported_.push_back(pass.name);
                log(LogLevel::Info, LogChannel::Render,
                    std::string("Graph: pass '") + pass.name
                        + "' skipped - its should_execute predicate declined");
            }
            continue;
        }
```

In `clear()`, beside the other resets, add:

```cpp
    skip_reported_.clear();
```

- [ ] **Step 3: Verify it fires once, and only for a skipped pass**

Run: `.\build\bin\Debug\sandbox.exe --gates`

Expected: exactly one `Graph: pass 'X' skipped` line per genuinely-off pass, and
each appearing **once**. AA defaults to Off, so the FXAA, SMAA and TAA passes are
the ones that should report. Count them:

```powershell
(.\build\bin\Debug\sandbox.exe --gates 2>&1 | Select-String "skipped -").Count
```

Expected: a small number, and **no duplicates** — check with
`... | Select-String "skipped -" | Group-Object | Where-Object Count -gt 1`,
which must return nothing.

- [ ] **Step 4: Verify and commit**

Run the full verification block. Expected 74 `(pass)`, 0 `FAIL`, 13/13. Update
`docs/ROADMAP.md:50`.

```bash
git add packages/renderer/include/engine/renderer/render_graph.hpp \
        packages/renderer/src/render_graph.cpp docs/ROADMAP.md
git commit -m "feat(renderer): name a pass whose predicate declined, once (analizeMax A1)"
git push origin main
```

---

## Task 5: Rewrite the pass-adding checklist

**Files:**
- Modify: `.claude/rules/renderer-boundaries.md`

The rule currently tells a session that adding a pass means eight files, four
structs and three hand-written copy blocks, and that missing one fails silently.
After Tasks 1-4 that is false, and a stale rule is worse than none — this is the
file a session loads before touching the renderer.

- [ ] **Step 1: Replace step 3 of the checklist**

The current step 3 reads:

> 3. **Plumbing** → add the `I<Thing>Pipeline*` field to **all four** structs it
>    passes through, and to the three hand-written copy blocks between them:
>    `ForwardDemo` (main.cpp) → `WorldExtractAssets` (world_extract.hpp) →
>    `ExtractDesc` (renderer/extract.hpp) → `RenderSnapshot`
>    (renderer/render_snapshot.hpp).

Replace with:

> 3. **Plumbing** → two adjacent lines in
>    `packages/renderer/include/engine/renderer/frame_pipelines.hpp`: a field on
>    `FramePipelines` and its `kFramePipelines` entry. Nothing else. The struct
>    travels by value through `WorldExtractAssets` → `ExtractDesc` →
>    `RenderSnapshot`, so there is no copy block to forget, and a field without a
>    table entry fails the `static_assert`.

- [ ] **Step 2: Replace the silent-failure warning in the preamble**

The paragraph above the numbered list currently reads:

> `add_pass` is where the pass is *registered*, but a working pass touches two
> packages and eight files — the four structs in step 3 live in four different
> files, and the shader, registration and recorder add four more. Missing a step
> fails **silently**: every
> `should_execute` predicate treats a null pipeline as "feature disabled", so a
> forgotten copy line produces a pass that never runs and never complains.

Replace with:

> `add_pass` is where the pass is *registered*, but a working pass spans two
> packages and five files: the shader, the pipeline, two adjacent lines in
> `frame_pipelines.hpp`, the registration, and the recorder — plus its gate.
> Missing a step is no longer silent. A field with no `kFramePipelines` entry
> fails the `static_assert`; a pipeline that is never created turns
> `Pipeline set gate` red and names itself. A `should_execute` predicate still
> treats a null pipeline as "feature disabled", but the gate makes that state
> unreachable, and `RenderGraph::execute` names any pass whose predicate
> declines.

- [ ] **Step 3: Verify and commit**

Run: `pwsh -NoProfile -File tools/check-invariants.ps1`
Expected: `all 13 checks passed` — `doc-links` must still resolve.

No `ROADMAP.md` update: markdown only.

```bash
git add .claude/rules/renderer-boundaries.md
git commit -m "docs: the pass checklist is five files now, not eight (analizeMax A1)"
git push origin main
```

---

## Definition of done

- [ ] `Pipeline set gate: present=13/13 missing=none (pass)` in Debug and Release
- [ ] 74 `(pass)` lines, 0 `FAIL`, exit 0, both configurations
- [ ] `Graph compiled: 25 passes, 19 resources` unchanged
- [ ] `D3D12 debug layer: 0 message(s), 0 error(s), 0 warning(s)`
- [ ] `all 13 checks passed`
- [ ] No `IGraphicsPipeline*` field remains on `WorldExtractAssets`, `ExtractDesc`
      or `RenderSnapshot` except `DrawBatch::pipeline` and `DrawItem::pipeline`:
      `grep -rn "IGraphicsPipeline\* [a-z_]*pipeline" packages/renderer/include packages/sandbox/src`
      returns only those two and `FramePipelines`' own fields
- [ ] `.claude/rules/renderer-boundaries.md` describes the 5-file shape
