cmake_minimum_required(VERSION 3.24)

engine_add_package(assets-png-wic
    SOURCES src/image_loader_png.cpp
    PUBLIC_DEPS engine::assets
    PRIVATE_DEPS windowscodecs ole32
)
