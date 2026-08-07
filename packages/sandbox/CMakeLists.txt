cmake_minimum_required(VERSION 3.24)

add_executable(sandbox src/main.cpp)

target_link_libraries(sandbox PRIVATE
    engine::engine
    engine::assets-filesystem
    engine::assets-obj
    engine::assets-gpu
    engine::debug-draw
    engine::math
)

if(TARGET engine::platform-win32)
    target_link_libraries(sandbox PRIVATE engine::platform-win32)
    target_compile_definitions(sandbox PRIVATE ENGINE_HAS_WIN32_PLATFORM)
endif()

if(TARGET engine::rhi-d3d12)
    target_link_libraries(sandbox PRIVATE engine::rhi-d3d12 engine::shaders-dxc)
    target_compile_definitions(sandbox PRIVATE ENGINE_HAS_D3D12)
endif()
