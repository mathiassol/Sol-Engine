cmake_minimum_required(VERSION 3.24)

engine_add_package(assets-gltf
    SOURCES src/mesh_loader_gltf.cpp
    PUBLIC_DEPS engine::assets engine::math
)

target_include_directories(assets-gltf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
