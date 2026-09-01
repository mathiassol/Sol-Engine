cmake_minimum_required(VERSION 3.24)

include(EngineRuntimeApp)
engine_add_runtime_app(game INSTALL_LAYOUT)

install(TARGETS game RUNTIME DESTINATION .)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/packages/sandbox/content/" DESTINATION content)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/packages/debug-draw/content/" DESTINATION debug)

if(ENGINE_RHI_D3D12)
    include(EngineDxc)
    engine_locate_dxc()
    if(NOT ENGINE_DXC_BIN_DIR)
        message(FATAL_ERROR
            "DXC runtime (dxcompiler.dll + dxil.dll) is required next to game.exe. "
            "Install the Windows 10/11 SDK; it ships with Visual Studio's "
            "\"Desktop development with C++\" workload. D3D12 Agility is not shipped. "
            "Run tools/check-prereqs.ps1 to see every prerequisite at once.")
    endif()
    install(FILES
        "${ENGINE_DXC_BIN_DIR}/dxcompiler.dll"
        "${ENGINE_DXC_BIN_DIR}/dxil.dll"
        DESTINATION .
    )
endif()
