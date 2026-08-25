cmake_minimum_required(VERSION 3.24)

engine_add_package(physics-cpu
    SOURCES src/physics_cpu.cpp
    PUBLIC_DEPS engine::physics
)
