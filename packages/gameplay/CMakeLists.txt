cmake_minimum_required(VERSION 3.24)

engine_add_package(gameplay
    SOURCES src/character.cpp src/camera.cpp
    PUBLIC_DEPS engine::core engine::math engine::physics
)
