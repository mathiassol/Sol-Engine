cmake_minimum_required(VERSION 3.24)

engine_add_package(rhi-vulkan
    SOURCES
        src/instance_vulkan.cpp
        src/device_vulkan.cpp
        src/pipeline_vulkan.cpp
        src/commands_vulkan.cpp
        src/rhi_vulkan.cpp
        third_party/volk.c
    PUBLIC_DEPS engine::rhi engine::math
)

target_include_directories(rhi-vulkan PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

# No find_package(Vulkan) and no vulkan-1.lib. volk resolves every entry point
# at runtime from the loader the GPU driver installs, so this package builds on
# a machine with no Vulkan SDK - which is what lets CI compile it on every push
# instead of leaving it to rot behind an option nobody turns on. See
# third_party/README.md.
#
# VK_NO_PROTOTYPES is what makes that true: without it the headers declare the
# API directly and the link needs an import library.
target_compile_definitions(rhi-vulkan PRIVATE
    VK_NO_PROTOTYPES
    VOLK_VULKAN_H_PATH="vulkan_headers_sol.h"
    # volk guards its Win32 entry points on this define, not on whether the
    # header is present - without it vkCreateWin32SurfaceKHR is simply not
    # declared, and the only symptom is one unresolved identifier.
    VK_USE_PLATFORM_WIN32_KHR
)
