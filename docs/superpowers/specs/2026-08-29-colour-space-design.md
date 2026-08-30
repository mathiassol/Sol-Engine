# Colour-space correctness — sRGB decode on input, sRGB encode on output

Date: 29 Aug 2026
Status: implemented

Closes audit finding **S1** from
[docs/analysis/2026-08-29-1149-full.md](../../analysis/2026-08-29-1149-full.md),
the only Critical finding in that report and the reason Stability graded D+.

## Sources

- Krzysztof Narkowicz, "ACES Filmic Tone Mapping Curve" —
  <https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/>.
  The exact curve at `tonemap.hlsl:21-28` is his fit. He states: "you need to
  multiply by exposure before the tone mapping and do the gamma correction
  after," and that the fit was made after *removing* the 2.4 gamma from the
  Rec.709 D65 ODT output. It is linear-in, linear-out.
- IEC 61966-2-1 sRGB transfer function. The piecewise form, not `pow(1/2.2)`:
  the two differ most in the darks, where sRGB has a linear segment below
  0.04045 (decode) / 0.0031308 (encode).
- D3D12 `DXGI_FORMAT_R8G8B8A8_UNORM_SRGB`: the hardware applies the transfer
  function on sample *before* filtering, so bilinear and trilinear
  interpolation happen in linear space. sRGB formats transform RGB only —
  **alpha is untouched**.

## The problem

The engine runs a physically-based lighting model in the wrong colour space at
both ends, and nothing detects it. Three distinct defects:

1. **Input.** Albedo is created as `Format::RGBA8_UNORM`
   (`packages/sandbox/src/main.cpp:4301`) and sampled straight into linear PBR
   maths (`forward.hlsl:132-139`). sRGB midtones sit far above their linear
   equivalents — sRGB 0.5 is linear 0.214 — so every diffuse and Fresnel term
   is computed on inflated albedo.
2. **Mip filtering.** The CPU mip chain box-filters raw encoded bytes
   (`append_box_mip`, `device_d3d12.cpp:98-131`). Averaging encoded values is
   not averaging light.
3. **Output.** Nothing applies a transfer function anywhere. `ldr_color` is
   `RGBA8_UNORM`, the swapchain is `DXGI_FORMAT_R8G8B8A8_UNORM`, and no shader
   encodes. The linear tonemapped result is presented raw.

Errors 1 and 3 partially cancel, which is why the image looks plausible and
why this shipped unnoticed. But the split-sum IBL, GGX specular, Smith
visibility and energy-conservation terms all have gates asserting the
*formulas* while being fed and consumed in the wrong space. Of 67 gates, none
references colour space.

Nothing in `docs/`, the root markdown files or `.claude/` mentions sRGB, gamma
or colour space, so this was never a recorded decision.

## Principle

8-bit colour is stored encoded. Everything between sampling and tonemapping is
linear. The encode is re-applied exactly once, at the end.

The rule this establishes for future textures: **`_SRGB` is for colour, plain
`UNORM` is for data.** Albedo and emissive are colour. Metallic-roughness,
normal maps, masks and the BRDF LUT are data and stay linear. Alpha is linear
even inside an sRGB texture.

No metallic-roughness or normal-map texture is uploaded today — the glTF loader
parses their URIs and `DrawBatch` carries the pointers, but they are always
null — so the rule matters for when that changes, not now.

## Approach

Chosen from three considered:

| | Approach | Verdict |
|---|---|---|
| **A** | sRGB texture format on input; explicit encode in the two tonemap shaders on output | **chosen** |
| B | Hardware sRGB on both ends (sRGB `ldr_color` + `_SRGB` RTV over the `UNORM` swapchain buffer) | rejected |
| C | Output encode only, leave albedo linear-sampled | rejected |

**B was rejected on a correctness ground, not a cost one.** Flip-model
swapchains do not accept `_SRGB` formats, so the supported route is an `_SRGB`
render-target view over the `UNORM` buffer — and all three
`CreateRenderTargetView` calls in this engine pass `nullptr` for the desc, so
that means new RHI surface for format-aliased views. Worse, FXAA and SMAA read
`ldr_color` through an SRV: an sRGB view would decode it back to linear,
putting their luma edge-detection heuristics in the wrong space. Avoiding that
needs typeless resources with two differently-formatted views per resource. B
also silently changes how the debug lines and stats overlay look, since they
would write into an encoded target.

**C was rejected as a half-fix that looks like a whole one.** It leaves
lighting consuming encoded albedo, and removing the output error without the
input error strips the cancellation — the image gets visibly worse while the
actual PBR defect survives.

A fixes all three defects, touches one RHI enum instead of introducing view
aliasing, and leaves the AA passes reading the perceptual data they were
designed for.

## Design

### Transfer functions — `packages/math/include/engine/math/srgb.hpp` (new)

```cpp
namespace engine::math {
f32 srgb_to_linear(f32 encoded);  // IEC 61966-2-1, piecewise
f32 linear_to_srgb(f32 linear);
}
```

Two C++ consumers need these: the mip builder in `rhi-d3d12` and the gate in
the sandbox. `math` is where numeric transforms live and it sits below both.

This adds one dependency edge, `rhi-d3d12 → math`. Ranks 3 → 1, downward, so
the `dependency-direction` invariant permits it. The alternative home, `core`,
needs no new edge but is a less natural fit for colour science; `math` was
chosen deliberately and this is the one placement call worth revisiting if the
edge proves unwelcome.

### RHI — one new format

`Format::RGBA8_UNORM_SRGB` in
`packages/rhi/include/engine/rhi/resources.hpp`, mapped to
`DXGI_FORMAT_R8G8B8A8_UNORM_SRGB` in `to_dxgi`.

**Both exhaustive switches over `Format` must gain the case** — `to_dxgi`
(`device_d3d12.cpp:247`) and `format_name` (`render_graph.cpp:34`). Neither has
a `default:` label, and `/W4` emits C4062 for an unhandled enumerator in that
situation. The tree currently builds with zero warnings and must continue to.
This is why a one-line enum addition spans three packages.

### Mip generation — average in linear

`can_mips` (`device_d3d12.cpp:2115`) currently tests
`desc.format == Format::RGBA8_UNORM` exactly, so it must accept the sRGB
variant too — otherwise the albedo silently loses its 12-mip chain. The
existing `Mip gate: mips=12 expected=12` catches that regression.

The mip builder **moves out of `rhi-d3d12` into `math`** as
`math::build_rgba8_mip_chain(top, w, h, mip_count, srgb)`, replacing the private
`build_rgba8_mips` / `append_box_mip` pair. This was not in the first draft of
this spec: the gate below asserts the mip behaviour, and a gate cannot reach a
static function inside a backend translation unit, so leaving it there would
have made the central claim of this change untestable. It is pure pixel
arithmetic with no graphics dependency, so `math` is where it belongs anyway.

When `srgb` is set, RGB channels are decoded to linear, averaged, and
re-encoded; **alpha is averaged directly**, because the format does not
transform it.

### Shaders

`common.hlsli` gains `srgb_encode()` and `srgb_decode()`, using the same
piecewise curve as the C++ side, in both scalar and `float3` overloads. The file
already documents four conventions that decide shader *portability*; colour
space is filed alongside them as a *correctness* convention rather than added to
that list, since all three target APIs handle sRGB the same way.

One implementation trap: `linear` is an HLSL interpolation modifier and cannot
name a parameter.

`tonemap.hlsl` and `tonemap_aces.hlsl` apply `srgb_encode` to the tonemapped
result as their final operation. Nothing else in the frame changes: not the
swapchain, not any RTV, not the AA passes, not the overlay, not the debug lines.

### Sandbox

`load_albedo_texture` (`main.cpp:4298`) creates its texture as
`RGBA8_UNORM_SRGB`. It is the only 8-bit colour texture in the tree, called
from one site.

The sky and IBL need no change: `ibl.cpp`'s `bake_radiance()` synthesises
linear radiance directly into `RGBA16` floats, with no PNG involved.

## Gate

`run_color_space_gate()` in `main.cpp`, asserting four things against
independently derived numbers. Written before the implementation, watched to
fail first.

1. **Transfer-function anchors.** `srgb_to_linear(0.5) ≈ 0.214041`, and
   round-trip identity across several samples.
2. **The piecewise toe, discriminating against `pow`.** `linear_to_srgb(0.001)`
   must be `12.92 × 0.001 = 0.01292`. A `pow(1/2.2)` approximation yields
   `0.0195`. Only the piecewise curve passes.
3. **Linear mip averaging.** A 2×2 image of two black and two white texels.
   Averaging encoded bytes gives **127**; averaging in linear gives 0.5 linear,
   which encodes to **188**. The gate asserts 188 for RGB and **127** for
   alpha, proving alpha was left alone.
4. **The HLSL curve matches the C++ one, end to end.** A compute shader
   (`srgb_gate.hlsl`, new) evaluates `srgb_encode` on both anchors and writes
   the results to a UAV; the gate copies to a readback buffer and compares on
   the CPU. This reuses the pattern already proven by the RHI impl gate
   (`main.cpp:1358-1385`) and is what stops the two implementations of the
   curve drifting apart.

Assertion 4 is the load-bearing one. The curve necessarily exists twice, in C++
and in HLSL — the same situation as `static_assert(sizeof(InstanceData) == 144)`
keeping the instance layout honest. Gating it is cheaper than trusting it.

## Out of scope

- **Exposure control.** Narkowicz's note calls for an exposure multiply as well
  as the gamma correction, but exposure is a separate feature and not needed
  for colour-space correctness.
- **Retuning the sandbox's light constants.** The image will change. `sun` and
  `ambient` were tuned by eye against the uncorrected pipeline and are scene
  authoring, not engine behaviour. Leaving them fixed keeps this change
  verifiable: if the result looks wrong afterwards, the pipeline and the tuning
  are separately attributable. Retuning is later art direction.
- **Texture → buffer readback in the RHI.** Would allow asserting the real
  uploaded mip chain rather than the mip builder's output. New RHI surface;
  separate work.

## Expected visible outcome

Midtones darken and saturate; specular separates more cleanly from diffuse.
Because the input and output errors were partially cancelling, this will not
look like the same image made correct — it will look different, and possibly
worse in places until the light constants are retuned. The gates prove the
pipeline; only a human eye judges the tuning.

## Files

Twelve, across five packages plus docs:

| File | Change |
|---|---|
| `packages/math/include/engine/math/srgb.hpp` | new |
| `packages/math/src/srgb.cpp` | new |
| `packages/math/CMakeLists.txt` | register the new files |
| `packages/rhi/include/engine/rhi/resources.hpp` | `RGBA8_UNORM_SRGB` |
| `packages/rhi-d3d12/src/device_d3d12.cpp` | `to_dxgi`, `can_mips`, linear averaging |
| `packages/rhi-d3d12/CMakeLists.txt` | `engine::math` |
| `packages/renderer/src/render_graph.cpp` | `format_name` case |
| `packages/sandbox/content/shaders/common.hlsli` | `srgb_encode` / `srgb_decode` |
| `packages/sandbox/content/shaders/tonemap.hlsl` | apply the encode |
| `packages/sandbox/content/shaders/tonemap_aces.hlsl` | apply the encode |
| `packages/sandbox/content/shaders/srgb_gate.hlsl` | new |
| `packages/sandbox/src/main.cpp` | albedo format, the gate, its call site |

`docs/ROADMAP.md`'s LOC audit figure moves with any line-count change — a
coupling the `roadmap-audit` invariant enforces on every commit that touches
source.
