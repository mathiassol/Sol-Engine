cmake_minimum_required(VERSION 3.24)

engine_add_package(assets-gpu
    SOURCES
        src/mesh_upload.cpp
        src/mesh_store.cpp
        src/texture_store.cpp
    PUBLIC_DEPS engine::assets engine::rhi
)
