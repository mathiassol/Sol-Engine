cmake_minimum_required(VERSION 3.24)

project(Engine
    VERSION 0.1.0
    DESCRIPTION "Modular game engine"
    LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(EngineDefaults)
include(PackageHelpers)

# ── Layer 0: Foundation ──────────────────────────────────────────────────────
add_subdirectory(packages/core)
add_subdirectory(packages/math)

# ── Layer 1: Abstractions (interfaces) ───────────────────────────────────────
add_subdirectory(packages/platform)
add_subdirectory(packages/rhi)
add_subdirectory(packages/assets)

# ── Layer 2: Implementations (swappable) ─────────────────────────────────────
option(ENGINE_PLATFORM_WIN32 "Build Win32 platform backend" ON)
if(ENGINE_PLATFORM_WIN32 AND WIN32)
    add_subdirectory(packages/platform-win32)
endif()

add_subdirectory(packages/assets-filesystem)
add_subdirectory(packages/assets-obj)
add_subdirectory(packages/assets-gpu)

option(ENGINE_RHI_D3D12 "Build D3D12 RHI backend" ON)
if(ENGINE_RHI_D3D12 AND WIN32)
    add_subdirectory(packages/shaders)
    add_subdirectory(packages/shaders-dxc)
    add_subdirectory(packages/rhi-d3d12)
endif()

# ── Layer 3: Systems ─────────────────────────────────────────────────────────
add_subdirectory(packages/renderer)
add_subdirectory(packages/debug-draw)

# ── Layer 4: Engine runtime ─────────────────────────────────────────────────
add_subdirectory(packages/engine)

# ── Applications ──────────────────────────────────────────────────────────────
option(ENGINE_BUILD_SANDBOX "Build sandbox test application" ON)
if(ENGINE_BUILD_SANDBOX)
    add_subdirectory(packages/sandbox)
endif()
