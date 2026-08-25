cmake_minimum_required(VERSION 3.24)

engine_add_package(engine
    SOURCES
        src/engine.cpp
        src/cvar_file.cpp
    PUBLIC_DEPS
        engine::core
        engine::platform
        engine::rhi
        engine::renderer
        engine::assets
        engine::audio
        engine::physics
)
