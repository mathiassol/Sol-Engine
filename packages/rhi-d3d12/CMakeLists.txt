cmake_minimum_required(VERSION 3.24)

engine_add_package(rhi-d3d12
    SOURCES
        src/device_d3d12.cpp
        src/rhi_d3d12.cpp
    PUBLIC_DEPS engine::rhi
    PRIVATE_DEPS d3d12 dxgi d3dcompiler
)
