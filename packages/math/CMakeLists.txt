cmake_minimum_required(VERSION 3.24)

engine_add_package(math
    SOURCES src/mat4.cpp
    PUBLIC_DEPS engine::core
)
