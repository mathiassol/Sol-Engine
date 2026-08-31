# Exposure control — one scalar before the tonemap

Date: 31 Aug 2026
Status: approved

ENGINE_MAP **Renderer #29**. Follows
[the colour-space work](2026-08-29-colour-space-design.md) (Renderer #28),
which applied the display encode Narkowicz's note asks for and left the
exposure multiply it asks for in the same sentence still missing.

## Sources

- Krzysztof Narkowicz, "ACES Filmic Tone Mapping Curve" —
  <https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/>:
  "you need to multiply by exposure before the tone mapping and do the gamma
  correction after." Renderer #28 did the second half.
- Photographic EV: each stop halves or doubles exposure, so the multiplier is
  `2^ev`. Chosen as the human-facing unit because a raw multiplier is much
  harder to tune by feel.

## Why

Renderer #28 made the pipeline correct and left the sandbox visibly
over-exposed. The retune in `efb9fd6` then measured *why* tuning cannot fix it:
dropping the sun 2.4x moved mean frame luminance only 212/255 -> 206/255.
`world.sun.color` drives the direct punctual term only. The bulk of the light
is the baked sky cubemap and the split-sum IBL derived from it, both taking
their magnitude from `sky_radiance()` in `renderer/src/ibl.cpp`.

Three independent light magnitudes, no single knob. Exposure is the single
knob: one scalar, applied after all of them have been summed into
`scene_color`, scaling sun, sky and IBL together.

## Control surface

A cvar, in EV stops:

```cpp
engine::Cvar cv_exposure{"r.exposure", -1.5f, "Exposure in EV stops; multiplier is 2^ev"};
```

declared in `main.cpp` beside `cv_aa`, following the `r.aa` precedent.

The default shown is a starting point, not a derived value. It is found by
screenshot iteration during implementation — beginning at `-1.5` (a 2.8x
reduction) and adjusted until the frame reads well. Whatever ships is recorded
in the ROADMAP entry with its measured mean luminance beside it, so a later
reader can tell a tuned number from a guessed one.

**The sandbox owns EV; the renderer receives a linear multiplier.** EV is a
photographic convention for humans, and the renderer has no business knowing
about it. `RenderSnapshot::exposure` is a plain multiplier defaulting to `1.0`,
so the renderer ships neutral and only changes behaviour when an app sets it.

The sandbox declares its cvar with a tuned EV value. That is scene authoring,
visibly separate from the mechanism, and settable at runtime:

```
sandbox.exe --set r.exposure=-1.5
```

This matters for the work itself. Tuning under `efb9fd6` was a
build-screenshot-judge loop at minutes per iteration. As a cvar it is seconds,
with no rebuild.

Rejected: a `scene::World` field serialized into `.solscene`. That is the right
eventual home if scenes ever need their own authored look, but it means
designing the format and extending the round-trip gate for a value nobody
authors yet.

Rejected: a compile-time constant like `bloom::kIntensity`. Tuning is the
immediate need.

## Where it applies

Chosen from three approaches. **Exposure multiplies `scene_color` at each of
its three first-read sites, and never touches bloom's output**, which site 1
has already exposed.

| Site | Carried in | Change |
|---|---|---|
| `bloom_downsample.hlsl`, first mip | `bloom::Constants::params.y` | scale before `apply_knee` |
| `tonemap.hlsl` | new tonemap CBV | `hdr = scene * exposure + bloom * intensity` |
| `taa.hlsl` | `taa::Constants::params.w` | scale the scene reads only |

`tonemap_aces.hlsl` is untouched: its input is TAA history, already exposed.

Both `params` slots are currently unused, so neither struct changes size and
neither `static_assert(sizeof(Constants) == 48)` moves.

### Why not the cheaper options

**Pre-exposure — scale radiance in `forward.hlsl` and `sky.hlsl`.** Two shader
edits, no new constant buffer, nothing downstream touched. Materially the
cheapest, and rejected anyway: `scene_color` would stop holding radiance and
start holding exposed radiance, baking a camera setting into the scene
representation. It also has to be undone when auto-exposure arrives, since
measuring luminance needs un-exposed values.

**A dedicated exposure pass.** Cleanest separation, but a full-screen RGBA16
transient and roughly 15 MB/frame of bandwidth at 720p, a 26th graph pass, and
the full eight-file pass cost — to achieve what two multiplies achieve.

The chosen approach also *improves* behaviour rather than only adding a knob.
`bloom::kThreshold = 1.0` currently thresholds on absolute scene radiance,
ignoring camera settings entirely. Post-exposure it means "would clip on the
sensor", which is the physical intent and what Unreal does.

### The consistency requirement

Pressing F5 must not change image brightness. The two AA paths composite bloom
in *different shaders* — non-TAA in `tonemap.hlsl`, TAA inside `taa.hlsl`,
which carries its own `bloom_intensity` in `params.z`. Both must apply exposure
identically to scene and never to bloom. The gate asserts they agree
numerically rather than leaving it to inspection.

## The new tonemap constants

`packages/renderer/include/engine/renderer/tonemap.hpp` (new), one `Vec4`:
`.x = exposure`, `.y = bloom_intensity`. `make_tonemap_pipeline_desc` gains
`constant_buffer_count = 1`. Root-parameter budget is fine: `1 + 2 = 3` against
`kMaxRootParams`.

`bloom_intensity` is folded in alongside exposure because it is **already**
duplicated — `bloom::kIntensity = 0.06f` in C++ and
`static const float kBloomIntensity = 0.06` in `tonemap.hlsl`, with nothing
keeping them equal. The CBV has to exist now regardless, so routing the
existing constant through it removes a live drift risk at no extra cost.

`record_tonemap` follows the pattern corrected in `cc206b6`: return early when
`alloc_frame_memory` fails, rather than drawing with an unbound root CBV. It is
the reason that bug is worth not repeating in new code.

One deliberate placement: `make_downsample_constants` sets
`params.y = first_mip ? exposure : 1.0f`, so the shader multiplies
unconditionally. Branch logic in C++ is branch logic a gate can assert; the
same logic in HLSL is not.

## Gate

`run_exposure_gate()` in `main.cpp`. Every assertion is on values, and none
needs a GPU readback. Written before the implementation and watched to fail.

1. **EV conversion.** `exp2(0) == 1`, `exp2(-1) == 0.5`, `exp2(1) == 2`.
2. **Plumbing survives the renderer end to end.** Build an `ExtractDesc` with a
   known exposure, run `extract_visible`, assert `snapshot.exposure` matches.
   This is the load-bearing assertion: it covers exactly the failure the 29 Aug
   audit named in finding A1, where a field added to three of the four
   plumbing structs produces a silently disabled feature.
3. **Both AA paths agree.**
   `taa::make_constants(..., e).params.w == tonemap::make_constants(e).params.x`,
   making the F5 brightness requirement a machine-checked fact rather than a
   note in a spec.
4. **Bloom applies it exactly once.**
   `make_downsample_constants(w, h, true, e).params.y == e` and
   `make_downsample_constants(w, h, false, e).params.y == 1.0f`.
5. **The cvar round-trips.** Set `r.exposure`, read it back via `find_cvar`.

## Out of scope

- **Auto-exposure / eye adaptation.** Needs a luminance reduction, which wants a
  compute pass, and `PassKind` is `{Graphics, Copy}` — the renderer cannot
  express one (29 Aug audit, finding A2). Blocked on unrelated work and a
  separate feature besides.
- **Per-scene exposure** in `.solscene`. Deferred, as above.
- **Retuning `sun` / `ambient` again.** They stay where `efb9fd6` left them.
  Exposure is now the knob for overall brightness; that is the point of it.

## Files

Eleven, two packages plus docs:

| File | Change |
|---|---|
| `packages/renderer/include/engine/renderer/tonemap.hpp` | new |
| `packages/renderer/include/engine/renderer/bloom.hpp` | exposure in `params.y`, gated on `first_mip` |
| `packages/renderer/include/engine/renderer/taa.hpp` | exposure in `params.w` |
| `packages/renderer/include/engine/renderer/extract.hpp` | `ExtractDesc::exposure` |
| `packages/renderer/include/engine/renderer/render_snapshot.hpp` | `RenderSnapshot::exposure` |
| `packages/renderer/src/extract.cpp` | copy it into the snapshot |
| `packages/renderer/src/render_graph.cpp` | tonemap CBV; pass exposure to bloom and TAA |
| `packages/sandbox/content/shaders/bloom_downsample.hlsl` | apply |
| `packages/sandbox/content/shaders/tonemap.hlsl` | apply, read `bloom_intensity` from the CBV |
| `packages/sandbox/content/shaders/taa.hlsl` | apply to scene reads only |
| `packages/sandbox/src/main.cpp` | cvar, EV conversion, plumbing, gate, pipeline desc |

Fewer than a render pass, because it adds no pass — there is no new shader,
pipeline, graph registration or recorder.

`docs/ROADMAP.md`'s LOC audit moves with any line-count change, as the
`roadmap-audit` invariant enforces on every commit touching source.
