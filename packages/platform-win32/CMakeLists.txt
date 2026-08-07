cmake_minimum_required(VERSION 3.24)

engine_add_package(platform-win32
    SOURCES
        src/platform_win32.cpp
        src/window_win32.cpp
    PUBLIC_DEPS
        engine::platform
    PRIVATE_DEPS
        user32
        gdi32
)
