cmake_minimum_required(VERSION 3.24)

engine_add_package(scene
    SOURCES src/world.cpp src/scene_file.cpp src/prefab.cpp
    PUBLIC_DEPS engine::core engine::math engine::assets
)
