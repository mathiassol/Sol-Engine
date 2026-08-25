cmake_minimum_required(VERSION 3.24)

engine_add_package(debug-draw
    SOURCES
        src/frame_stats.cpp
        src/stats_overlay.cpp
        src/debug_lines.cpp
    PUBLIC_DEPS engine::core engine::math engine::rhi engine::shaders
)
