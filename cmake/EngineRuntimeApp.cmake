# Shared sandbox / game executable. Sources live in packages/sandbox;
# game.exe is the player binary (install layout + ENGINE_GAME_APP).

function(engine_copy_install_content TARGET)
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/packages/sandbox/content"
            "$<TARGET_FILE_DIR:${TARGET}>/content"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/packages/debug-draw/content"
            "$<TARGET_FILE_DIR:${TARGET}>/debug"
        COMMENT "Copy install-layout content next to ${TARGET}"
    )
endfunction()

function(engine_add_runtime_app TARGET)
    cmake_parse_arguments(APP "INSTALL_LAYOUT" "" "" ${ARGN})

    add_executable(${TARGET}
        "${PROJECT_SOURCE_DIR}/packages/sandbox/src/main.cpp"
        "${PROJECT_SOURCE_DIR}/packages/sandbox/src/world_extract.cpp"
    )

    target_link_libraries(${TARGET} PRIVATE
        engine::engine
        engine::assets-filesystem
        engine::assets-obj
        engine::assets-gltf
        engine::assets-gpu
        engine::assets-png-wic
        engine::debug-draw
        engine::math
        engine::scene
        engine::gameplay
    )

    if(TARGET engine::audio-xaudio2)
        target_link_libraries(${TARGET} PRIVATE engine::audio-xaudio2)
        target_compile_definitions(${TARGET} PRIVATE ENGINE_HAS_XAUDIO2)
    endif()

    if(TARGET engine::physics-cpu)
        target_link_libraries(${TARGET} PRIVATE engine::physics-cpu)
        target_compile_definitions(${TARGET} PRIVATE ENGINE_HAS_PHYSICS_CPU)
    endif()

    if(TARGET engine::platform-win32)
        target_link_libraries(${TARGET} PRIVATE engine::platform-win32)
        target_compile_definitions(${TARGET} PRIVATE ENGINE_HAS_WIN32_PLATFORM)
    endif()

    if(TARGET engine::rhi-d3d12)
        target_link_libraries(${TARGET} PRIVATE engine::rhi-d3d12 engine::shaders-dxc)
        target_compile_definitions(${TARGET} PRIVATE ENGINE_HAS_D3D12)
        include(EngineDxc)
        engine_copy_dxc_runtime(${TARGET})
    endif()

    if(APP_INSTALL_LAYOUT)
        target_compile_definitions(${TARGET} PRIVATE ENGINE_GAME_APP)
        engine_copy_install_content(${TARGET})
        install(FILES "${CMAKE_BINARY_DIR}/cooked/content.pak" DESTINATION .)
    endif()

    if(TARGET content-pak)
        add_dependencies(${TARGET} content-pak)
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_BINARY_DIR}/cooked/content.pak"
                "$<TARGET_FILE_DIR:${TARGET}>/content.pak"
            COMMENT "Copy content.pak next to ${TARGET}"
        )
    endif()

    target_compile_definitions(${TARGET} PRIVATE
        ENGINE_APP_FILE_VERSION="${PROJECT_VERSION}"
    )

    if(WIN32)
        file(TO_CMAKE_PATH
            "${PROJECT_SOURCE_DIR}/packages/game/resources/resource.h" ENGINE_RC_RESOURCE_H)
        if(APP_INSTALL_LAYOUT)
            set(ENGINE_RC_FILE_DESCRIPTION "Sol")
            set(ENGINE_RC_INTERNAL_NAME "game")
            set(ENGINE_RC_ORIGINAL_FILENAME "game.exe")
            file(TO_CMAKE_PATH
                "${PROJECT_SOURCE_DIR}/packages/game/resources/sol.ico" _sol_ico)
            set(ENGINE_RC_ICON_LINE "IDI_APP_ICON ICON \"${_sol_ico}\"")
        else()
            set(ENGINE_RC_FILE_DESCRIPTION "Engine Sandbox")
            set(ENGINE_RC_INTERNAL_NAME "sandbox")
            set(ENGINE_RC_ORIGINAL_FILENAME "sandbox.exe")
            set(ENGINE_RC_ICON_LINE "")
        endif()
        configure_file(
            "${PROJECT_SOURCE_DIR}/packages/game/resources/app.rc.in"
            "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.rc"
            @ONLY
        )
        target_sources(${TARGET} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.rc")
    endif()
endfunction()
