cmake_minimum_required(VERSION 3.24)

engine_add_package(scene-render
    SOURCES
        src/extract.cpp
    PUBLIC_DEPS engine::scene engine::renderer engine::debug-draw engine::assets-gpu
                engine::math
)
