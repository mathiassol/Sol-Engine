cmake_minimum_required(VERSION 3.24)

engine_add_package(assets
    SOURCES src/cooked.cpp src/pak.cpp
    PUBLIC_DEPS engine::core engine::math
)
