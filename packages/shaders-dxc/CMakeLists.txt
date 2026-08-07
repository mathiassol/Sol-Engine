cmake_minimum_required(VERSION 3.24)

engine_add_package(shaders-dxc
    SOURCES
        src/shader_compiler_dxc.cpp
        src/shader_hot_reload_dxc.cpp
    PUBLIC_DEPS engine::shaders
    PRIVATE_DEPS d3dcompiler
)
