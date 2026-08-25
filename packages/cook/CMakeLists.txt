cmake_minimum_required(VERSION 3.24)

add_executable(cook src/main.cpp)
target_link_libraries(cook PRIVATE
    engine::core
    engine::math
    engine::assets
    engine::assets-obj
    engine::assets-gltf
    engine::assets-png
)

set(ENGINE_COOKED_PAK "${CMAKE_BINARY_DIR}/cooked/content.pak")
add_custom_command(
    OUTPUT "${ENGINE_COOKED_PAK}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/cooked"
    COMMAND $<TARGET_FILE:cook>
        --content "${PROJECT_SOURCE_DIR}/packages/sandbox/content"
        --out "${ENGINE_COOKED_PAK}"
    DEPENDS
        cook
        "${PROJECT_SOURCE_DIR}/packages/sandbox/content/meshes/cube.obj"
        "${PROJECT_SOURCE_DIR}/packages/sandbox/content/meshes/cartoon_husky.gltf"
        "${PROJECT_SOURCE_DIR}/packages/sandbox/content/meshes/cartoon_husky.bin"
        "${PROJECT_SOURCE_DIR}/packages/sandbox/content/textures/albedo.png"
        "${PROJECT_SOURCE_DIR}/packages/sandbox/content/textures/husky/Cartoon_Husky_Albedo1.png"
    COMMENT "Cook SOLP content.pak"
    VERBATIM
)
add_custom_target(content-pak ALL DEPENDS "${ENGINE_COOKED_PAK}")
add_custom_command(TARGET content-pak POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ENGINE_COOKED_PAK}"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/content.pak"
    COMMENT "Copy content.pak next to runtime binaries"
)
