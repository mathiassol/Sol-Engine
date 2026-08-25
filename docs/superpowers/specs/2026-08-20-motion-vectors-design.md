# Motion vectors (Renderer #13)

Date: 20 Aug 2026  
Status: implemented

## Sources

- Unity URP: motion is a **UV offset**. `prev_uv = curr_uv - motion`. RG
  channels. Camera-only can be a depth fullscreen pass; **object** motion
  needs previous world matrices.
- Alex Tardif / typical TAA: current and previous clip positions, perspective
  divide, difference. Unjittered matrices (jitter is TAA, not this row).
- Evergine / Threepipe: store `PreWorld` per object; extra geometry pass.

School notes in `reasarch/GRAPICS-RESEARCH.md` describe TAA consuming these
vectors. Treat that file as a feature list, not the encoding.

## Not this

- TAA, jitter, history color, neighborhood clamp (Renderer #14).
- Motion blur.
- RG16 format (RHI has RGBA16; extra channels stay unused).
- Skinned / morph previous positions (Assets #10 / Animation #1).
- Camera-only fullscreen from depth (sky stays 0 velocity until TAA cares).
- Stacking TAA on SMAA.

## Decision

A **geometry velocity pass** after opaque forward, before sky:

1. Clear velocity to 0 (sky / background = no motion).
2. Redraw visible opaques with depth **equal**, no depth write.
3. `clip_curr = view_proj * model * pos`
4. `clip_prev = prev_view_proj * prev_model * pos`
5. UV: `ndc * float2(0.5, -0.5) + 0.5` (D3D Y vs texture v).
6. Store `curr_uv - prev_uv` in RG of an RGBA16 graph transient.

Previous camera matrices and per-instance models live in `MotionHistory`
(64 slots, keyed by extract `id`). First sighting: `prev = current` (zero
motion). Culled instances still update history so they do not teleport when
they re-enter.

Own CBV (`MotionConstants`, 256 bytes). Forward `FrameConstants` stays 400.

## Gate

`Motion gate: uv=yes camera=yes object=yes history=yes equal=yes pass=yes (pass)`

1. Static pose → UV motion ~ 0.
2. Camera move, object still → non-zero matching CPU encode.
3. Object translate, camera still → non-zero matching CPU encode.
4. First history frame → zero; second static frame → zero.
5. Graph has `motion_vectors` after `forward` and before `sky`.
6. `DepthTest::Equal` on the velocity PSO.

SMAA/FXAA unchanged. TAA does not run.

## Out of scope

TAA, jitter, motion blur, skins, RG16, G-buffer.
