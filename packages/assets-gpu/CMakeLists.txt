cmake_minimum_required(VERSION 3.24)

engine_add_package(assets-gpu
    SOURCES src/mesh_upload.cpp
    PUBLIC_DEPS engine::assets engine::rhi
)
