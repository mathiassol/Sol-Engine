# Vendored third-party headers

Following the `packages/assets-gltf/third_party/cgltf.h` precedent: vendored
into the package that uses them, on that package's private include path, with
no package manager. The tree has neither vcpkg nor `FetchContent` and this does
not introduce one.

Copied from Vulkan SDK **1.4.357.0** on 2 Sep 2026. Both are at version
**357** (`VK_HEADER_VERSION`, `VOLK_HEADER_VERSION`) and must be bumped
together — volk's loader tables are generated against a specific header
revision.

| Path | Upstream | Licence |
|------|----------|---------|
| `vulkan/vulkan_core.h`, `vulkan/vk_platform.h` | [KhronosGroup/Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | Apache-2.0 OR MIT (SPDX header in the file) |
| `vk_video/*` (12 files) | KhronosGroup/Vulkan-Headers | Apache-2.0 OR MIT |
| `volk.h`, `volk.c` | [zeux/volk](https://github.com/zeux/volk) | MIT (notice at the end of `volk.h`) |

Total ≈2 MB, all text.

## Why the build needs no Vulkan SDK

`volk` resolves every entry point at runtime from `vulkan-1.dll`, which the GPU
driver installs — so there is no `find_package(Vulkan)`, no `vulkan-1.lib` at
link time, and CI compiles `rhi-vulkan` on a runner with no SDK. That is the
whole point of vendoring rather than depending: a backend behind an option that
nothing ever compiles is a backend that rots.

The SDK is still a **dev-time** install, for two runtime things it alone
provides: `VK_LAYER_KHRONOS_validation` (the Vulkan counterpart of the D3D12
debug layer) and a `dxcompiler.dll` built with `-DENABLE_SPIRV_CODEGEN=ON`. The
copy in the Windows SDK cannot emit SPIR-V at all.

## Deliberately not vendored

- **`vulkan.hpp` and its family** (`vulkan_structs.hpp` at 8.9 MB,
  `vulkan_profiles.hpp` at 3.2 MB, `vulkan_funcs.hpp`, `vulkan_raii.hpp`) —
  16 MB of C++ RAII bindings. This engine has its own ownership conventions and
  uses the C API directly.
- **`vk_enum_string_helper.h`** (817 KB) — stringifies every enum in the API.
  `src/vulkan_common.hpp` has a hand-written `to_string(VkResult)` covering the
  results this backend can produce, which fits on a screen.
- **`vulkan.h`** — it pulls in every platform's surface header behind `#ifdef`s.
  `volk.h` includes `vulkan_core.h` directly, which is all an offscreen device
  needs. `vulkan_win32.h` joins this directory when presentation does.

## Do not reformat

`third_party` is excluded from `Get-PackageSources` in
`tools/check-invariants.ps1`, so `format-hygiene`, the graphics-API isolation
scan and the ROADMAP LOC audit all skip these files. Reformatting them would
make the next version bump an unreadable diff for no gain.

Git does normalise the line endings: `core.autocrlf` is `true`, so these arrive
from the SDK as CRLF and are stored LF — exactly what already happened to
`cgltf.h`. That is the repository's convention rather than an edit, and it is
noted here so the next version bump is not mistaken for a reformat.
