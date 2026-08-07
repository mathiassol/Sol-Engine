cmake_minimum_required(VERSION 3.24)

engine_add_package(renderer
    SOURCES src/render_graph.cpp
    PUBLIC_DEPS engine::rhi
)
