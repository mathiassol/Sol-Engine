cmake_minimum_required(VERSION 3.24)

engine_add_package(core
    SOURCES
        src/log.cpp
        src/assert.cpp
        src/clock.cpp
        src/frame.cpp
        src/arena.cpp
        src/profile.cpp
        src/cvar.cpp
)
