cmake_minimum_required(VERSION 3.24)

engine_add_package(renderer
    SOURCES
        src/render_graph.cpp
        src/standard_frame.cpp
        src/extract.cpp
        src/ibl.cpp
    PUBLIC_DEPS engine::rhi engine::core engine::math
)
