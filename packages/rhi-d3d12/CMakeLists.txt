cmake_minimum_required(VERSION 3.24)

engine_add_package(rhi-d3d12
    SOURCES
        src/device_d3d12.cpp
        src/rhi_d3d12.cpp
    PUBLIC_DEPS engine::rhi engine::math
    PRIVATE_DEPS d3d12 dxgi d3dcompiler
)

target_include_directories(rhi-d3d12 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
