# Locate Windows SDK DXC runtime (dxcompiler.dll + dxil.dll) and expose
# engine_copy_dxc_runtime() so executables can load IDxcCompiler3.
#
# D3D12 Agility (D3D12Core.dll + D3D12SDKVersion) is not shipped. Current
# work stays on inbox OS D3D12 (Feature Level 11_0, Shader Model 6.0).

function(engine_locate_dxc)
    if(ENGINE_DXC_BIN_DIR AND EXISTS "${ENGINE_DXC_BIN_DIR}/dxcompiler.dll"
            AND EXISTS "${ENGINE_DXC_BIN_DIR}/dxil.dll")
        return()
    endif()

    set(_bin "")
    set(_ver "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}")
    set(_roots "")
    if(DEFINED ENV{WindowsSdkDir} AND NOT "$ENV{WindowsSdkDir}" STREQUAL "")
        list(APPEND _roots "$ENV{WindowsSdkDir}")
    endif()
    list(APPEND _roots "C:/Program Files (x86)/Windows Kits/10")

    foreach(_root IN LISTS _roots)
        cmake_path(NORMAL_PATH _root)
        if(_ver AND EXISTS "${_root}/bin/${_ver}/x64/dxcompiler.dll")
            set(_bin "${_root}/bin/${_ver}/x64")
            break()
        endif()
    endforeach()

    if(NOT _bin)
        file(GLOB _candidates
            "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/dxcompiler.dll")
        if(_candidates)
            list(SORT _candidates)
            list(GET _candidates -1 _newest)
            cmake_path(GET _newest PARENT_PATH _bin)
        endif()
    endif()

    if(NOT _bin)
        message(WARNING "DXC runtime not found (dxcompiler.dll). Shader compile will fail at runtime.")
        set(ENGINE_DXC_BIN_DIR "" CACHE INTERNAL "Windows SDK DXC bin directory")
        return()
    endif()

    if(NOT EXISTS "${_bin}/dxil.dll")
        message(WARNING "dxil.dll missing next to dxcompiler.dll in ${_bin}")
        set(ENGINE_DXC_BIN_DIR "" CACHE INTERNAL "Windows SDK DXC bin directory")
        return()
    endif()

    set(ENGINE_DXC_BIN_DIR "${_bin}" CACHE INTERNAL "Windows SDK DXC bin directory")
    message(STATUS "DXC runtime: ${ENGINE_DXC_BIN_DIR}")
endfunction()

function(engine_copy_dxc_runtime TARGET)
    engine_locate_dxc()
    if(NOT ENGINE_DXC_BIN_DIR)
        message(FATAL_ERROR
            "DXC runtime (dxcompiler.dll + dxil.dll) is required next to ${TARGET}. "
            "Install the Windows 10 SDK. D3D12 Agility is not shipped.")
    endif()
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ENGINE_DXC_BIN_DIR}/dxcompiler.dll"
            "${ENGINE_DXC_BIN_DIR}/dxil.dll"
            $<TARGET_FILE_DIR:${TARGET}>
        COMMENT "Copy DXC runtime next to ${TARGET}"
    )
endfunction()
