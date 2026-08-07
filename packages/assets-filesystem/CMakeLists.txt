cmake_minimum_required(VERSION 3.24)

engine_add_package(assets-filesystem
    SOURCES src/asset_loader_filesystem.cpp
    PUBLIC_DEPS engine::assets engine::platform
)
