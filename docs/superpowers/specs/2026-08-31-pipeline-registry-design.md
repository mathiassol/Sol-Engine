# Pipeline registry — one identity per pipeline, held honest by the compiler

Date: 31 Aug 2026
Status: spec

Not an ENGINE_MAP row. This is finding **A1** from the 31 Aug 14:11 audit
(`docs/analysis/2026-08-31-1411-full.md`), the one criterion that report named as
holding Architecture out of the Exemplary band. Written as a path rather than a
link because full reports rotate — only the five newest are kept — so a reader
of this spec in six months should be pointed at a name to search for, not sent
to a file that no longer exists. (`doc-links` would not have complained either
way: `docs/superpowers/` is excluded from it precisely because specs are dated
archives whose links are allowed to age.)

## Why

A render pass reaches the renderer by having its `IGraphicsPipeline*` copied by
hand through four structs. Measured in the tree at `a110b95`:

| Hop | Where | Nature |
|-----|-------|--------|
| `ForwardDemo` → `WorldExtractAssets` | `sandbox/src/main.cpp:275-287` | 13 assignments, renaming `shadow_pipeline` → `shadow` |
| `WorldExtractAssets` → `ExtractDesc` | `sandbox/src/world_extract.cpp:119-130` | 12 assignments, renaming back |
| `ExtractDesc` → `RenderSnapshot` | `renderer/src/extract.cpp:52-63` | 12 assignments, field-for-field identity |

The middle two structs carry the pointers **unchanged**. Three copy blocks exist
only because the same pointers are spelled two different ways along the route.

**164 references** across seven files:

```
80  packages/sandbox/src/main.cpp
24  packages/renderer/src/render_graph.cpp
16  packages/renderer/src/extract.cpp
12  packages/sandbox/src/world_extract.cpp
12  packages/renderer/include/engine/renderer/render_snapshot.hpp
12  packages/renderer/include/engine/renderer/extract.hpp
 8  packages/renderer/src/standard_frame.cpp
```

Adding one pass costs **8 edits across 4 files**: one owning `unique_ptr`, one
creation-table row, three struct fields, three copy lines. And the failure is
silent — 7 of the 16 `should_execute` predicates read a pipeline pointer and
treat null as "feature disabled", so a forgotten copy line produces a pass that
never runs and never complains. `.claude/rules/renderer-boundaries.md` documents
this as a known hazard rather than fixing it.

Two facts make the fix cheap, both verified rather than assumed:

- `packages/sandbox/src/world_extract.hpp` already includes
  `renderer/extract.hpp` and `renderer/render_snapshot.hpp`, so a type defined
  in `renderer` is **already visible** to the app-side struct. No new package
  edge, no layering change.
- Creation is **already table-driven** for 11 of the 13 pipelines — a
  pointer-to-member table at `main.cpp:5182`. Only forward and shadow are
  hand-built, because their descs do not share the fullscreen shape.

## Precedent in this tree

Two existing patterns, both reused rather than invented:

- `static_assert(sizeof(InstanceData) == 144)`
  (`renderer/include/engine/renderer/render_snapshot.hpp:57`) keeps a C++ struct
  and its HLSL mirror from drifting by making drift a build error.
- `physics_cpu.cpp:400` pairs `enum class CapacityKind` with a parallel
  `kCapacityNames[]` and asserts the sizes match, so a kind cannot be added
  without a name.

## Design

One type is the single identity for a frame's pipelines. It lives in `renderer`
because `renderer`, `sandbox` and the extract path all need it and `renderer` is
the lowest package of the three.

### The type and its table

New header, `packages/renderer/include/engine/renderer/frame_pipelines.hpp`:

```cpp
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

struct FramePipelineEntry {
    rhi::IGraphicsPipeline* FramePipelines::*field;
    const char* name;
};

inline constexpr FramePipelineEntry kFramePipelines[] = {
    {&FramePipelines::forward, "forward"},
    // … one row per field, same order …
};

// A field with no table entry cannot be checked, logged, or filled by the
// creation loop, and would fail silently in exactly the way this design exists
// to remove. A table entry with no field does not compile. This makes the
// remaining case - a field nobody tabulated - a build error too.
static_assert(sizeof(FramePipelines)
        == (sizeof(kFramePipelines) / sizeof(kFramePipelines[0])) * sizeof(void*),
    "every FramePipelines field needs exactly one kFramePipelines entry");
```

Thirteen entries. `forward` is included even though the renderer consumes it
per-batch rather than per-frame (`world_extract.cpp:97` assigns
`item.pipeline`), because uniformity is the point: the completeness gate should
cover it, and `assets.pipelines.forward` reads the same as its twelve siblings.

### Ownership

`ForwardDemo` loses its thirteen named `unique_ptr` members and gains:

```cpp
FramePipelines pipelines;                                        // the identities
std::vector<std::unique_ptr<rhi::IGraphicsPipeline>> owned;      // the lifetimes
```

Ownership no longer needs a stable index or a name — `owned` is a bag whose only
job is to outlive the frame, and `pipelines` carries identity. A creation site
does two things: `demo->pipelines.*field = p.get()` and
`demo->owned.push_back(std::move(p))`.

The existing fullscreen creation table gains a `field` column of the same
pointer-to-member type, which is what links creation to identity. It stays a
runtime local rather than joining `kFramePipelines`, because its shader paths are
runtime-resolved `std::string`s and cannot be `constexpr`. Shadow and forward
stay hand-built for the same reason as today — their descs do not share the
fullscreen shape — and simply assign their own field.

### Data flow after the change

`FramePipelines` is embedded by value in `WorldExtractAssets`, `ExtractDesc` and
`RenderSnapshot`. The three copy blocks become one assignment each:

```cpp
assets.pipelines = demo.pipelines;     // main.cpp
desc.pipelines   = assets.pipelines;   // world_extract.cpp
out.pipelines    = desc.pipelines;     // extract.cpp
```

Read sites change spelling only: `snapshot.taa_pipeline` →
`snapshot.pipelines.taa`. That is ~32 mechanical, compiler-checked renames in
`render_graph.cpp` and `standard_frame.cpp`.

**Cost per new fullscreen pass afterwards: 3 edits in 2 files** — one
`FramePipelines` field, one `kFramePipelines` entry (the adjacent line), one
creation-table row. No copy to forget, and forgetting any of the three is a
build error or a red gate.

## Error handling

Collapsing the plumbing removes the forgotten-copy failure. It does not remove
the *uncreated* pipeline: null still reaches a predicate and still reads as
"feature off". Two additions close that, chosen to keep the tree's
degrade-and-log stance rather than replace it:

1. **`RenderGraph::compile()` logs the passes it will skip**, once per compile,
   naming each. A pass whose `should_execute` is false because a pipeline is null
   is currently indistinguishable from one that is legitimately off; the message
   makes the first case visible without changing behaviour.
2. **A gate asserts the set is complete.** After setup, all thirteen entries must
   be non-null. A forgotten pipeline turns `--gates` red rather than producing a
   quietly incomplete frame.

Both are loops over `kFramePipelines`, which is why the table earns its keep —
in a named-fields-only design each diagnostic would need its own hand-maintained
list, reintroducing the drift being removed.

The AA pipelines are legitimately optional at *runtime* (F5 cycles Off / FXAA /
SMAA / TAA, and `aa::effective_mode` already degrades on a null pipeline). The
gate asserts they were *created*, not that they are in use. That distinction is
the reason for a gate plus a log rather than a hard failure in `compile()`.

## Testing

Follows the gate protocol in CLAUDE.md — written first, watched to fail.

- **`run_pipeline_set_gate`** — asserts all thirteen entries non-null after
  setup, and reports the count and the first missing name:
  `Pipeline set gate: present=13/13 missing=none (pass)`. Watch it fail by
  commenting out one creation row; it must name that pipeline.
- **The `static_assert`** is itself a test, and the cheapest one: add a field
  without a table entry and the build fails. Verify by doing it once, on purpose,
  before wiring anything up.
- **No behaviour change is the headline assertion.** All 73 existing gates must
  pass unchanged in Debug and Release, `Graph compiled: 25 passes, 19 resources`
  must be identical, and `ENGINE_GPU_DEBUG=1` must stay at 0 messages. The AA
  gate (`default=off exclusive=yes`) and the TAA/motion/bloom gates are the ones
  that would catch a mis-wired rename.

## Scope

**In:** `FramePipelines` + table + `static_assert`; `ForwardDemo` ownership
change; the three copy blocks; the ~32 read-site renames; the two diagnostics;
one new gate; the checklist in
`.claude/rules/renderer-boundaries.md` rewritten to the new 3-edit shape.

**Out, deliberately:**

- **Compute pipelines.** `PassKind` has no compute kind
  (`render_graph.hpp:29`), so there is no compute pass to carry one for. When
  there is, it gets its own set rather than being bolted onto this one.
- **Per-batch pipelines.** `DrawBatch::pipeline` and `DrawItem::pipeline` are
  the instanced-draw batching key, not frame state. Untouched.
- **Self-registering passes.** Letting a pass declare its own shader, pipeline
  and registration in one place would be a larger win and moves pipeline
  ownership out of the app, which the non-negotiables in CLAUDE.md deliberately
  keep app-side. That is a separate decision, not a step of this one.
- **Splitting `main.cpp`.** Audit finding A4. This change touches `main.cpp`
  heavily and A4 restructures it; doing them together would make both
  unreviewable.

## Do not

- Do not turn `FramePipelines` into a string- or hash-keyed lookup. The
  compile-time field is what makes a wrong name a build error, and a per-frame
  lookup buys nothing here.
- Do not merge the runtime creation table into `kFramePipelines`. The shader
  paths are runtime-resolved and the two tables answer different questions —
  identity versus how to build.
- Do not make a null pipeline fail `compile()`. The AA modes are legitimately
  optional at runtime and the tree degrades rather than dying.
- Do not fold the ~32 renames into the same commit as the diagnostics. The
  renames are mechanical and reviewable in bulk; the diagnostics change
  behaviour and deserve to be revertible on their own.
