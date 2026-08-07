cmake_minimum_required(VERSION 3.24)

engine_add_package(debug-draw
    SOURCES
        src/frame_stats.cpp
        src/stats_overlay.cpp
    PUBLIC_DEPS engine::core engine::rhi engine::shaders
)
