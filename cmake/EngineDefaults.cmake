# Global defaults for all Engine targets.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Warnings
if(MSVC)
    add_compile_options(/W4 /permissive-)
    add_compile_options("$<$<CONFIG:Release>:/Oi>" "$<$<CONFIG:Release>:/Gy>")
    add_link_options("$<$<CONFIG:Release>:/OPT:REF>" "$<$<CONFIG:Release>:/OPT:ICF>" "$<$<CONFIG:Release>:/INCREMENTAL:NO>")
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Output directories (per-config)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
