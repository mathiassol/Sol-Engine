cmake_minimum_required(VERSION 3.24)

engine_add_package(assets-obj
    SOURCES src/mesh_loader_obj.cpp
    PUBLIC_DEPS engine::assets
)
