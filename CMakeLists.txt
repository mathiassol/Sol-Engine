# 3.24 is what the CMake code in this repository actually needs. README asks for
# 4.2+ because that is where the "Visual Studio 18 2026" generator was added, and
# that generator is the documented build. Another generator works on older CMake;
# these two numbers answer different questions and both are correct.
cmake_minimum_required(VERSION 3.24)

# Windows MAX_PATH. This build generates paths up to 119 characters below the
# source directory, and CMake's try-compile scratch directories go deeper still,
# so a source path much past 140 characters pushes MSBuild over the
# 260-character limit. What the reader then sees is
# `MSB4018 ... exceeds the OS max path limit` from inside a generated
# cmTC_*.vcxproj, followed by "No CMAKE_CXX_COMPILER could be found" — two errors
# that name nothing about this project and send them looking in the wrong place.
# Measured 1 Sep 2026: clean at a 42-character root, dead at 145.
#
# This runs BEFORE project(), deliberately. The try-compile that blows up *is*
# project()'s compiler check, so a guard placed after it never prints — which is
# how the first version of this block failed its own control test. That is also
# why the test is CMAKE_HOST_WIN32 and not WIN32: WIN32 is not set until
# project() has run.
#
# A warning, not a FATAL_ERROR: a machine with long paths enabled configures fine
# and must not be blocked.
if(CMAKE_HOST_WIN32)
    string(LENGTH "${CMAKE_CURRENT_SOURCE_DIR}" ENGINE_SOURCE_PATH_LENGTH)
    if(ENGINE_SOURCE_PATH_LENGTH GREATER 140)
        message(WARNING
            "Source path is ${ENGINE_SOURCE_PATH_LENGTH} characters, past the ~140 this build "
            "needs to stay inside Windows' 260-character MAX_PATH limit. Configure or build is "
            "likely to fail with an MSB4018 \"exceeds the OS max path limit\" error that does "
            "not mention this project. Clone somewhere shorter, or enable LongPathsEnabled. "
            "tools/check-prereqs.ps1 reports this alongside every other prerequisite.")
    endif()
    unset(ENGINE_SOURCE_PATH_LENGTH)
endif()

# C is here for exactly one file: packages/rhi-vulkan/third_party/volk.c, the
# Vulkan entry-point loader. Without it CMake silently declines to compile the
# .c - no warning at configure, no warning at build - and the only symptom is
# every vk* symbol unresolved at link.
project(Engine
    VERSION 0.1.0
    DESCRIPTION "Modular game engine"
    LANGUAGES C CXX
)

if(WIN32)
    enable_language(RC)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(EngineDefaults)
include(PackageHelpers)

# ── Layer 0: Foundation ──────────────────────────────────────────────────────
add_subdirectory(packages/core)
add_subdirectory(packages/math)
add_subdirectory(packages/reflect)

# ── Layer 1: Abstractions (interfaces) ───────────────────────────────────────
add_subdirectory(packages/platform)
add_subdirectory(packages/rhi)
# Backend-agnostic shader-compiler contract. Belongs here with the other
# interfaces, not behind ENGINE_RHI_D3D12 — debug-draw links it unconditionally,
# so gating it made -DENGINE_RHI_D3D12=OFF fail at generate time.
add_subdirectory(packages/shaders)
add_subdirectory(packages/assets)
add_subdirectory(packages/audio)
add_subdirectory(packages/physics)

# ── Layer 2: Implementations (swappable) ─────────────────────────────────────
option(ENGINE_PLATFORM_WIN32 "Build Win32 platform backend" ON)
if(ENGINE_PLATFORM_WIN32 AND WIN32)
    add_subdirectory(packages/platform-win32)
endif()

if(WIN32)
    add_subdirectory(packages/audio-xaudio2)
endif()

add_subdirectory(packages/physics-cpu)

add_subdirectory(packages/assets-filesystem)
add_subdirectory(packages/assets-obj)
add_subdirectory(packages/assets-gltf)
add_subdirectory(packages/assets-gpu)
if(WIN32)
    add_subdirectory(packages/assets-png-wic)
endif()

option(ENGINE_RHI_D3D12 "Build D3D12 RHI backend" ON)
option(ENGINE_RHI_VULKAN "Build Vulkan RHI backend" ON)

# The shader compiler is DXC either way - it emits DXIL for D3D12 and SPIR-V for
# Vulkan from the same HLSL - so it belongs to both backends rather than to
# D3D12. Gating it on D3D12 alone would make a Vulkan-only configure fail to
# link, which is the same class of mistake that once made -DENGINE_RHI_D3D12=OFF
# fail at generate time (see the note above packages/shaders).
if((ENGINE_RHI_D3D12 OR ENGINE_RHI_VULKAN) AND WIN32)
    add_subdirectory(packages/shaders-dxc)
endif()
if(ENGINE_RHI_D3D12 AND WIN32)
    add_subdirectory(packages/rhi-d3d12)
endif()
if(ENGINE_RHI_VULKAN AND WIN32)
    add_subdirectory(packages/rhi-vulkan)
endif()

# ── Layer 3: Systems ─────────────────────────────────────────────────────────
add_subdirectory(packages/renderer)
add_subdirectory(packages/debug-draw)
add_subdirectory(packages/scene)
add_subdirectory(packages/gameplay)

# ── Layer 4: Engine runtime ─────────────────────────────────────────────────
add_subdirectory(packages/engine)

# ── Tools ─────────────────────────────────────────────────────────────────────
if(WIN32)
    add_subdirectory(packages/cook)
endif()

# ── Applications ──────────────────────────────────────────────────────────────
option(ENGINE_BUILD_SANDBOX "Build sandbox test application" ON)
if(ENGINE_BUILD_SANDBOX)
    add_subdirectory(packages/sandbox)
endif()

option(ENGINE_BUILD_GAME "Build player game.exe (install layout)" ON)
if(ENGINE_BUILD_GAME)
    add_subdirectory(packages/game)
endif()

# ── Packaging (Build #8) ──────────────────────────────────────────────────────
#
# CPack wraps the install layout that `cmake --install` already produces - exe,
# content/, debug/, content.pak, the DXC pair, LICENSE. It deliberately does not
# restate what ships: the install() rules are the single definition, and CPack
# reads them. Adding a file here instead of to an install() rule would put it in
# the zip and not in `cmake --install --prefix dist`, which is how the two drift.
#
# ZIP only. Build #8 reads "Zip / installer"; an NSIS or WiX installer buys
# nothing while the game is a folder you unzip, and Build #19 (delta patching,
# Far) is the row that would need one.
#
# Multi-config generator, so the configuration is chosen at pack time:
#     cmake --build --preset release-game
#     cpack --config build/CPackConfig.cmake -C Release
#
# No package preset: packagePresets needs schema version 6, which needs CMake
# 3.25, and raising CMakePresets.json's version would lock out every CMake
# between this project's 3.24 floor and 3.25 from *all* presets - a real cost
# for a saved keystroke. The install() rules are already the single source of
# truth, so nothing is duplicated by spelling the cpack command out.
set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "Sol")
set(CPACK_PACKAGE_VENDOR "Sol Engine")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "Sol-${PROJECT_VERSION}-win64")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/package")
# 1, so unzipping produces one Sol-<version>-win64/ directory rather than
# spilling the exe and three content folders into whatever directory you are in.
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY 1)
set(CPACK_VERBATIM_VARIABLES TRUE)

# MIT, and the binary travels without the repository.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" DESTINATION .)

include(CPack)
