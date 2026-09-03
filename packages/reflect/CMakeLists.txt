cmake_minimum_required(VERSION 3.24)

engine_add_package(reflect
    SOURCES
        src/field.cpp
    PUBLIC_DEPS engine::core
)
