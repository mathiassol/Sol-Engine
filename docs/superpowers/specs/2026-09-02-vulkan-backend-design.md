# A second GPU backend — `rhi-vulkan`, offscreen first

Date: 2 Sep 2026
Status: implemented

ENGINE_MAP **RHI #12** (`rhi-vulkan`) and its stated prerequisite **Shaders #5**
(SPIR-V compile path). Both were **Far**; the 1 Sep contract pass (RHI #15
reversed-Z, #9 storage textures, #18 MSAA) was done specifically to get the
API-neutral contract changes in while they still cost one implementation instead
of two. This is the row that pass was clearing the way for.

RHI #12's *Finish first* was corrected on 1 Sep to name Shaders #5 only —
Platform #9 was never a technical prerequisite, because Vulkan runs on Windows
and doing the second backend there first validates the contract against one new
variable instead of two.

---

## What the research settled before any design

Five measurements, because each one would otherwise have been a guess that
shaped the design.

**1. The DXC this engine ships cannot emit SPIR-V.** A probe against the exact
`dxcompiler.dll` the build copies next to `sandbox.exe` (Windows SDK
10.0.26100, DXC 1.8.2502.11):

```
dxil   Compile() hr=0x00000000 status=0x00000000 bytes=2832 first_word=0x43425844
spirv  Compile() hr=0x00000000 status=0x80070057 bytes=0
       diagnostics: SPIR-V CodeGen not available. Please recompile with -DENABLE_SPIRV_CODEGEN=ON.
```

The DLL's *strings* were inconclusive — `fvk-` and `SPIR-V` are both present,
because the option table is compiled in whether or not the backend is — which is
why the compiler was asked rather than the binary. LunarG documents the same
split: the Visual Studio DXC has Vulkan codegen disabled, the Vulkan SDK's has
DirectX disabled. They are two different builds.

**2. Two same-named DXC builds coexist in one process.** The worry was the
loader's base-name dedup silently handing back the already-loaded module, which
would return a working compiler that quietly cannot do SPIR-V — the worst
possible shape. Measured false for a full-path load:

```
implicitly linked dxcompiler.dll module = 00007FF876B10000
LoadLibrary .\copy\dxcompiler.dll   -> 00007FF865940000  distinct
LoadLibrary dxcompiler_spirv.dll     -> 00007FF864B90000  distinct
RESULT: dedup_by_base_name=no renamed_is_distinct=yes renamed_compiles=yes
```

Windows dedups by resolved path, not base name. So no rename, no copy step: the
SPIR-V DXC loads from wherever the SDK put it.

**3. Every shader in the tree already compiles to SPIR-V.** All 23 real entry
points, through the SDK's DXC, with the register shifts below, first try:

```
  forward.hlsl               ps_main    20312 bytes  SPIR-V
  taa.hlsl                   ps_main    11984 bytes  SPIR-V
  bloom_downsample.hlsl      ps_main    17620 bytes  SPIR-V
  ... 20 more ...
RESULT: spirv_ok=23 failed=0 of 23
```

This is the single biggest de-risking. The shader half of a second backend is
usually where a project discovers its HLSL is not portable. Here it is not a
risk at all, and that was worth knowing before designing around it.

**4. Coordinate systems cost nothing.** Depth is [0,1] in both APIs, so
reversed-Z carries over untouched — the whole of RHI #15 transfers for free.
NDC Y is flipped, and a **negative viewport height** (`VK_KHR_maintenance1`,
core since Vulkan 1.1) fixes it in the backend, so the shaders stay
byte-identical between APIs. `-fvk-invert-y` is deliberately **not** used: it
would change the shaders, which is the thing worth protecting.

**5. This machine.** Vulkan 1.4 loader (driver-installed), RTX 3070 Ti at
1.4.351, SDK 1.4.357.0 at `C:\VulkanSDK\1.4.357.0` — which ships the SPIR-V
DXC, the validation layer, volk *and* VMA.

## Why offscreen, and why that is the whole first pass

The obvious slice is a triangle in a window. It is the wrong one here, and the
reason is worth writing down.

`--rhi vulkan` would put the sandbox on the Vulkan device, and the sandbox
immediately runs 82 gates and a render graph needing samplers, compute,
structured buffers, mip chains and cube maps. A slice has none of those, so a
windowed slice produces a wall of *not implemented* and cannot run the engine.
**Presentation buys nothing usable until parity.**

Worse, a gate that only runs behind a flag is a gate that rots. So:

> The Vulkan backend is verified by a gate that stands up its own **offscreen**
> device inside the ordinary `--gates` run, on the D3D12 build, and asserts the
> **same number** the D3D12 MSAA gate already measures.

Two backends, one measurement, compared on every single run. The swapchain,
surface, acquire/present sync and `--rhi` selection all land with parity, when
there is something to present.

## Contract changes

Two, both of which are gaps rather than features. Both implemented on **both**
backends, so the contract never has a mode only one backend supports.

### Offscreen devices

`DeviceDesc::window_handle` is already a `void*` defaulting to `nullptr`, and
that state is currently undefined — D3D12 would fail or crash. It gains a
meaning:

- `window_handle == nullptr` → an offscreen device. No swapchain, no
  presentation, everything else identical.
- `IDevice::offscreen()` is added so a caller can ask.
- `swapchain()` on an offscreen device is a programming error, not a
  recoverable one: `ENGINE_ASSERT_MSG` by name. A null-object swapchain whose
  `present()` quietly does nothing is exactly the trap this engine is built to
  avoid, so it is not that.

Beyond Vulkan this is real capability: GPU gates that need no window.

### `read_texture`

`IDevice::read_buffer` exists; its texture twin does not. Writing the MSAA gate
on 1 Sep worked around the gap with a compute shader and an SRV — which is a
lot of machinery to read four numbers, and it would have dragged compute,
descriptor sets and SRV binding into this slice for the same reason.

```cpp
// Copies a texture's mip 0 to CPU memory, tightly packed. Blocks: it submits
// and waits. For gates and tools, never for a frame.
virtual bool read_texture(ITexture& texture, void* out, usize size) = 0;
```

Returns false and logs by name when the size is wrong or the format is not one
it can pack, rather than reading a partial image into a buffer the caller
believes is full.

## Packages

### `packages/rhi-vulkan` (new)

`engine::rhi::vulkan::create_rhi()`, mirroring `engine::rhi::d3d12::create_rhi()`
exactly — one factory function per implementation package.

Vendored under `third_party/`, following the `cgltf.h` precedent (one directory,
private include path, no package manager — the tree has neither vcpkg nor
`FetchContent` and this does not introduce one):

| File | Size | Licence |
|------|------|---------|
| `vulkan/vulkan_core.h` | 1.35 MB | Apache-2.0 |
| `vulkan/vk_platform.h` | 4 KB | Apache-2.0 |
| `vk_video/*` (12 files) | 92 KB | Apache-2.0 |
| `volk.h` + `volk.c` | 530 KB | MIT |

≈2 MB, all text. `vulkan.hpp` and friends are **not** vendored — the C++
bindings are 16 MB of the SDK's headers and this engine has its own conventions.
`vk_enum_string_helper.h` (817 KB) is not vendored either; a hand-written
`to_string(VkResult)` over the results that actually occur is smaller than the
generated one and matches the house aligned-`case` style.

`volk` rather than a hand-written loader: a hand-maintained entry-point table
means every new Vulkan call needs a table edit, and the failure mode of
forgetting one is a null-pointer call. That is a class of mistake worth 530 KB.
`VOLK_VULKAN_H_PATH` points it at the vendored header.

The build depends on nothing installed, so **CI compiles the Vulkan backend on
every push** and it cannot rot — which is the whole reason for vendoring rather
than `find_package(Vulkan)`. The SDK stays a *runtime* dev-time install, for
the validation layer and the SPIR-V compiler.

`ENGINE_RHI_VULKAN` defaults **ON** on Windows, for the same reason.

### `packages/shaders-dxc` (extended, not forked)

`ShaderTarget::Spirv` already exists on `ShaderCompileDesc` and is rejected at
exactly one place. DXC emitting SPIR-V is still DXC, so a second package would
break one-implementation-per-package for no gain.

- The Windows SDK DXC stays implicitly linked and serves DXIL, unchanged.
- On the first SPIR-V request, a second DXC is resolved by absolute path —
  `%VULKAN_SDK%\Bin\dxcompiler.dll`, overridable with `ENGINE_DXC_SPIRV` — and
  loaded with `LoadLibraryExW`.
- It is **probed once** with a trivial `-spirv` compile. If the probe reports
  `SPIR-V CodeGen not available`, it refuses and names what to install.
- There is deliberately **no fallback to DXIL**. Handing a Vulkan backend a
  DXIL blob is a corruption, not a degradation.

Flags, from the measurement above:

```
-spirv
-fvk-t-shift 16 0   -fvk-u-shift 32 0   -fvk-s-shift 48 0
-fvk-t-shift 16 1   -fvk-u-shift 32 1   -fvk-s-shift 48 1
-fvk-use-dx-layout
```

The shifts exist because the default HLSL→SPIR-V mapping sends
`register(xN, spaceM)` to set M binding N **ignoring the register type** — so
`b0`, `t0`, `u0` and `s0` all collide at set 0 binding 0. Disjoint ranges (b at
0, t at 16, u at 32, s at 48) fix it for the tree's actual surface: `b0`,
`t0`–`t6`, `s0`–`s2`, `u0`–`u1`, and one `t0, space1`. `-fvk-use-dx-layout`
keeps cbuffer packing matching the C++ structs.

The CMake gate moves from `if(ENGINE_RHI_D3D12 AND WIN32)` to
`if((ENGINE_RHI_D3D12 OR ENGINE_RHI_VULKAN) AND WIN32)` — a Vulkan-only build
still needs a shader compiler.

**The disk cache already keys on the target.** This was flagged as a likely
correctness bug — two targets from one source is a new axis, and serving a
cached DXIL blob to a SPIR-V request would be a corruption. Checking rather
than assuming found `cache_key` in `shader_cache_dxc.cpp` already folds
`desc.target` alongside the entry point, profile, include graph and debug flag.
So the SPIR-V path inherits disk caching for free, and there is nothing to fix.
Recorded because a plan that "fixed" it would have been changing correct code.

## The Vulkan device

Deliberately small — it is the surface one triangle and one readback need, no
more.

- **volk** initialised once; instance from `vkGetInstanceProcAddr`.
- **Instance**: `VK_LAYER_KHRONOS_validation` when it is present *and*
  `ENGINE_GPU_DEBUG=1`, with a debug-utils messenger routed into
  `engine::log` on `LogChannel::Render` — the same treatment the D3D12 debug
  layer gets, so the same rule applies: any validation message is a
  build-breaking bug.
- **Physical device**: prefer discrete, skip CPU/software devices the way the
  D3D12 backend skips `DXGI_ADAPTER_FLAG_SOFTWARE`. Log the chosen device name
  and API version.
- **Device**: one graphics-capable queue. Dynamic rendering (core 1.3; this
  machine is 1.4) so `begin_render_pass(RenderPassInfo)` maps to
  `vkCmdBeginRendering` and needs no `VkRenderPass`/`VkFramebuffer` cache —
  which is also what makes `RenderPassInfo::resolve` map to
  `VkRenderingAttachmentInfo::resolveImageView` later.
- **Memory**: a small explicit helper — find memory type, allocate, bind.
  VMA is bundled with the SDK and is the right answer once the resource count
  justifies it; for a handful of allocations it is not, and the helper is
  needed either way.
- **Depth**: reversed-Z carries over. **Y**: negative viewport height.
- **Pipelines**: `VkDescriptorSetLayout` synthesised from the five counts in
  `resources.hpp`'s binding contract, exactly as its table describes. The
  counts contract stays; this pass is what *tests* whether it is sufficient.
  A2 (bind groups) stays open, and if the counts model proves insufficient the
  failure will be concrete and documented rather than predicted.

**The virtuals it does not implement yet still have to exist.** Each logs once,
by name, the first time it is called. A stub that silently returns is the
failure mode the whole engine is built around avoiding, and a second-backend
author reading a silent stub cannot tell it apart from a working one.

## The gates

Three, in `gates/gates_rhi.cpp`, all classified `Gpu`. The design here changed
while the plan was being written, and the reasons are worth keeping.

### `run_backend_parity_gate` — one function, two backends

Not a Vulkan gate. It takes an `IDevice&`, a `ShaderTarget` and an API label, so
**the same function runs twice**: once against an offscreen D3D12 device, once
against an offscreen Vulkan one. A divergence is then a backend difference and
cannot be a difference in the test.

It draws `backend_parity_gate.hlsl` — **new, not `msaa_gate.hlsl`.** Two reasons
the MSAA shader was wrong for this:

- It uses **no resources**, so the gate would have exercised none of the
  descriptor plumbing — and translating `resources.hpp`'s five counts into a
  `VkDescriptorSetLayout` is exactly what a second backend has to get right.
  The parity shader carries a `cbuffer` at `b0` whose tint the readback checks
  byte-for-byte, so the whole chain from `uniform_buffer_count` through the
  set layout to the shader read is covered.
- Adding a `cbuffer` to `msaa_gate.hlsl` would have broken the MSAA gate, whose
  pipeline declares `uniform_buffer_count = 0`.

Asserted:

1. `offscreen() == true`.
2. The bytecode's magic word matches the requested target — `0x07230203` for
   SPIR-V, `0x43425844` for DXIL — so a backend can never be handed the other
   API's bytecode.
3. Texel (8, 8) is byte-exactly the cbuffer's tint: `51, 153, 204, 255`. Those
   values are exactly representable in UNORM8 with no `.5` rounding tie, so
   this is an equality rather than a tolerance.
4. Texel (55, 55) — its mirror across the diagonal — is still the clear colour.
   **This is the Y-direction check.** A backend that gets the flip wrong draws
   the other half of the target, and these two probes catch that where a
   coverage count would average it away.
5. The covered-texel count, returned to the caller rather than asserted against
   a constant.

### `Backend agreement gate` — the comparison

At the call site, because it needs both counts. It reports `d3d12_lit`,
`vulkan_lit` and their spread, and fails if they differ by more than 64 — one
diagonal's worth.

The earlier plan was to demand the same **2,016** from both. That was wrong:
2,016 is what D3D12's fill rule produces for a 45° edge through pixel centres,
and while both APIs specify that a shared edge is covered exactly once, they do
not specify it identically enough to demand equality on a knife-edge case. So
the deterministic assertions are the interior and exterior probes, which cannot
be ambiguous, and the coverage count is compared with a stated tolerance and
both numbers printed. A gate that reports the two numbers is more useful than
one that only says "equal".

### `run_spirv_gate` and `run_vulkan_device_gate`

`run_spirv_gate` covers Shaders #5 on its own: SPIR-V comes back with the right
magic word, it is **not** the DXIL blob for the same shader, DXIL still works
from the same compiler instance — which is what proves the two DLLs coexist —
and the disk cache round-trips the SPIR-V rather than serving the DXIL.

`run_vulkan_device_gate` covers device creation separately, because everything
after it is meaningless if the instance, the physical-device choice or dynamic
rendering were not what was asked for.

All three written before their implementations and watched failing. Two are
falsified on purpose afterwards: the shader's triangle is flipped to the other
half, and the Vulkan viewport height is made positive — each must turn its gate
red at the probes.

**Environmental skip.** Compiled in but no Vulkan device (no driver), or no
SPIR-V compiler (no SDK), is an environment fact and not a defect. The gate logs
`(skip)` with the specific reason — which install is missing — and returns true.
It logs `(skip)`, never `(pass)`, so the pass count cannot silently absorb it.
Anything else that fails is a `FAIL`.

Where `ENGINE_RHI_VULKAN` was not compiled at all, the call site is `#ifdef`'d
out entirely, the same shape as `ENGINE_HAS_D3D12`.

## Out of scope, deliberately

- **Presentation** — surface, swapchain, acquire/present sync, resize, and the
  `--rhi` runtime selection. Lands with parity, for the reason argued above.
- **Parity for the other 81 gates.** This pass proves the contract survives a
  second API; the next one makes the engine run on it.
- **VMA.** Bundled and available; not justified by a handful of allocations.
- **Bind groups (A2).** Stays open by design — the counts contract is what this
  backend is here to test.
- **`vulkan.hpp`.** The C++ bindings are not vendored and not used.

## Risks

| Risk | Standing |
|------|----------|
| HLSL does not survive SPIR-V | **Retired.** 23/23 measured. |
| Two DXC builds collide in-process | **Retired.** Measured distinct. |
| Reversed-Z or clip space needs shader changes | **Retired.** [0,1] both; Y is a viewport sign. |
| Disk cache serves DXIL to a SPIR-V request | **Retired.** `cache_key` already folds `desc.target`. |
| The counts binding contract proves insufficient | Open **on purpose** — this is the pass that finds out. |
| Descriptor-set lifetime under 3-frame flight | Open. The slice does one submit and waits, so it is deferred honestly rather than solved badly. |
