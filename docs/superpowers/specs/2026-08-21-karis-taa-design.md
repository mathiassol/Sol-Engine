# Karis TAA (Renderer #14)

Date: 21 Aug 2026  
Status: implemented

## Sources

- Brian Karis, High Quality Temporal Supersampling (SIGGRAPH 2014, UE4): Halton
  jitter, HDR resolve, YCoCg AABB clip (not RGB clamp), ~0.05 current / 0.95
  history.
- Playdead INSIDE (Pedersen): `clip_aabb` toward the neighborhood box.
- Alex Tardif TAA starter: Catmull-Rom history, 3×3 neighborhood, closest-depth
  velocity, un-jitter the current sample with a small filter.
- Yang / Liu / Salvi, A Survey of Temporal Antialiasing Techniques (EG 2020).
- This tree: motion vectors store unjittered `curr_uv - prev_uv` (Unity URP).
  SMAA/FXAA are one exclusive post-AA slot after ACES.

School notes in `reasarch/GRAPICS-RESEARCH.md` are a feature list, not encoding.

## Not this

- SMAA T2x, TSR, TAAU, FSR2, DLSS, TXAA, MSAA hybrids.
- Stacking TAA on SMAA/FXAA.
- Variance clip (Salvi) / k-DOP. YCoCg AABB clip is v1.
- Sharpen / CAS, mip bias, dilated velocity, sky camera-motion pass.
- Texture UAVs.

## Decision

Native-res **Karis TAA**, optional on F5. Default AA is Off. F5 cycles Off → FXAA → SMAA → TAA.

**Where:** after bloom, before ACES. TAA composites `scene + bloom * 0.06` in
HDR, writes RGBA16 history, then a 1-SRV ACES pass produces `ldr_color`.
Spatial AA stays after tonemap on the same exclusive enum. When TAA is on,
copy/FXAA/SMAA do not run.

**Jitter:** Halton(2,3), length 8. Clip-space offset
`clip.xy += jitter_ndc * clip.w`. NDC: `((h2-0.5)*2/w, (h3-0.5)*-2/h)` so
texture v matches D3D. Forward projection is jittered. Sky rays subtract the
same NDC from the fullscreen NDC (`ndc_scale.zw`). Motion raster applies the
same clip jitter so `DepthTest::Equal` hits jittered depth; the stored UV
delta is still unjittered curr−prev. Off / FXAA / SMAA do not jitter.

**History:** two persistent RGBA16 `ColorShaderResource` textures, imported
into the graph as `taa_history_a` / `taa_history_b` (ping-pong). Graph cycle
checker cannot read and write the pair, so the *other* ping is bound as SRV
from the snapshot (like IBL), not as a graph read. First TAA frame (and after
resize / leaving TAA) ignores history (`reset=1`), binds scene as dummy t4.

**Resolve (one fullscreen pass):**

1. Convert jitter NDC to UV (`x*0.5`, `y*-0.5`). 3×3 tent of current HDR at
   `uv - jitter_uv` (reconstructs at the pixel center).
2. 3×3 YCoCg min/max AABB around that same UV.
3. Motion at the current pixel (swapchain depth has no SRV; closest-depth
   velocity is a follow-up).
4. `history_uv = uv - motion` (Sol encoding). Out of 0–1 → current only.
5. Catmull-Rom sample of history.
6. Playdead `clip_aabb` in YCoCg, convert back.
7. `out = lerp(current, clipped, 0.95)` unless reset.

**Passes:** `taa_even` / `taa_odd` write the imported pings; `tonemap_taa_even`
/ `tonemap_taa_odd` ACES that ping into `ldr_color`. Existing `tonemap` runs
when TAA is off.

FrameConstants stays 400 bytes. TAA has its own 48-byte CBV.

## Gate

`TAA gate: default=off optional=yes hdr=yes jitter=yes clip=ycocg pass=yes (pass)`

1. Default mode is Off; F5 order Off→FXAA→SMAA→TAA; never stacked.
2. Halton jitter is non-zero and `apply_jitter` shifts clip x/y; inverse is identity at 0.
3. YCoCg round-trips; a history color outside the AABB is clipped inside.
4. Graph has `taa_even` after last bloom upsample and before `tonemap_taa_even`.
5. Motion CBV uses unjittered `projection * view` for UV math; raster adds
   clip jitter so coverage matches color.
6. TAA pipeline exists; spatial AA still exclusive fallbacks.
7. `jitter_to_uv` packs into the TAA CBV; history weight is 0.95.

## Out of scope

Variance clip, sharpen, TAAU, sky velocity, MSAA, stacking.
