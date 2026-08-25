cmake_minimum_required(VERSION 3.24)

engine_add_package(physics
    INTERFACE
    PUBLIC_DEPS engine::core engine::math
)
