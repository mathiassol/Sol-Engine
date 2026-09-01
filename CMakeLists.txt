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
            "not mention this project. Clone somewhere shorter, or enable LongPathsEnabled.")
    endif()
    unset(ENGINE_SOURCE_PATH_LENGTH)
endif()

project(Engine
    VERSION 0.1.0
    DESCRIPTION "Modular game engine"
    LANGUAGES CXX
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
if(ENGINE_RHI_D3D12 AND WIN32)
    add_subdirectory(packages/shaders-dxc)
    add_subdirectory(packages/rhi-d3d12)
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
