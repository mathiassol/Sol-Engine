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
        "${PROJECT_SOURCE_DIR}/packages/sandbox/src/sandbox_common.cpp"
        "${PROJECT_SOURCE_DIR}/packages/sandbox/src/world_extract.cpp"
        "${PROJECT_SOURCE_DIR}/packages/sandbox/src/gates/gate_registry.cpp"
        "${PROJECT_SOURCE_DIR}/packages/sandbox/src/gates/gates_assets.cpp"
        "${PROJECT_SOURCE_DIR}/packages/sandbox/src/gates/gates_core.cpp"
        "${PROJECT_SOURCE_DIR}/packages/sandbox/src/gates/gates_physics.cpp"
        "${PROJECT_SOURCE_DIR}/packages/sandbox/src/gates/gates_platform.cpp"
        "${PROJECT_SOURCE_DIR}/packages/sandbox/src/gates/gates_renderer.cpp"
        "${PROJECT_SOURCE_DIR}/packages/sandbox/src/gates/gates_rhi.cpp"
        "${PROJECT_SOURCE_DIR}/packages/sandbox/src/gates/gates_scene.cpp"
    )

    target_link_libraries(${TARGET} PRIVATE
        engine::engine
        engine::assets-filesystem
        engine::assets-obj
        engine::assets-gltf
        engine::assets-gpu
        engine::debug-draw
        engine::math
        engine::scene
        engine::gameplay
    )

    # packages/assets-png-wic is added only under if(WIN32), so an unconditional
    # link here made `cmake -B build` fail at generate time on Linux and macOS.
    if(TARGET engine::assets-png-wic)
        target_link_libraries(${TARGET} PRIVATE engine::assets-png-wic)
        target_compile_definitions(${TARGET} PRIVATE ENGINE_HAS_PNG)
    endif()

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

    # Both apps get the content copy, not just the install-layout one.
    #
    # The engine resolves its mounts next to the executable, so `sandbox.exe`
    # reads shaders from <exe_dir>/content. Copying only for `game` meant
    # `cmake --build --target sandbox` left stale HLSL there: shader edits
    # appeared to work because the gates check C++ constants, while the runtime
    # kept compiling the previous shader. That failure is silent and expensive.
    engine_copy_install_content(${TARGET})

    if(APP_INSTALL_LAYOUT)
        target_compile_definitions(${TARGET} PRIVATE ENGINE_GAME_APP)
        # Guarded like the copy rule below it. packages/cook is inside if(WIN32)
        # because it links assets-png-wic (WIC), so off Windows nothing produces
        # this file and an unconditional rule fails at `cmake --install` - later
        # than configure, and therefore quieter.
        if(TARGET content-pak)
            install(FILES "${CMAKE_BINARY_DIR}/cooked/content.pak" DESTINATION .)
        endif()
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
