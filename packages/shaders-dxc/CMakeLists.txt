cmake_minimum_required(VERSION 3.24)

include(EngineDxc)
engine_locate_dxc()

engine_add_package(shaders-dxc
    SOURCES
        src/shader_compiler_dxc.cpp
        src/shader_hot_reload_dxc.cpp
        src/shader_cache_dxc.cpp
    PUBLIC_DEPS engine::shaders
    PRIVATE_DEPS dxcompiler
)
