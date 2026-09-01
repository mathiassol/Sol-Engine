# Global defaults for all Engine targets.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Only the Ninja and Makefile generators honour this; the Visual Studio
# generator ignores it and writes no compile_commands.json at all. So the
# `vs2026` preset produces no compile database and the `ninja` preset does —
# that is the reason the ninja preset exists, and what clangd wants pointing at.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Static CRT, so a player needs no Visual C++ redistributable.
#
# Measured 1 Sep 2026 on Release game.exe. With the default /MD it imported
# MSVCP140.dll, VCRUNTIME140.dll, VCRUNTIME140_1.dll and eight api-ms-win-crt-*
# entries. The api-ms-win-crt-* set is the Universal CRT and is inbox on Windows
# 10 and later; the other three are the redistributable and are not. Static
# linking removed all eleven, leaving only ole32, VERSION, USER32, XINPUT1_4,
# d3d12, dxgi, KERNEL32 and dxcompiler.dll. Cost: 1.0 MB -> 1.3 MB, and
# game.exe --gates still reported 74 (pass), 0 FAIL.
#
# Safe because the engine ships no DLLs of its own. The one non-system DLL,
# dxcompiler.dll, is reached through COM — it allocates and releases its own
# blobs — so no CRT object crosses the boundary. Shader compile and hot reload
# are covered by the gates that passed above.
#
# No cmake_policy(SET CMP0091 NEW) line is needed: NEW is the default at
# cmake_minimum_required(3.24). Do not add one.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

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
