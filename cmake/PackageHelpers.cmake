# engine_add_package — standard layout for a modular engine package.
#
# Usage:
#   engine_add_package(core
#       SOURCES src/log.cpp
#       HEADERS include/engine/core/log.hpp
#       PUBLIC_DEPS other_pkg
#   )
function(engine_add_package NAME)
    set(options INTERFACE)
    set(oneValueArgs)
    set(multiValueArgs SOURCES HEADERS PUBLIC_DEPS PRIVATE_DEPS)
    cmake_parse_arguments(PKG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(PKG_INTERFACE)
        add_library(${NAME} INTERFACE)
        if(PKG_HEADERS)
            target_sources(${NAME} INTERFACE ${PKG_HEADERS})
        endif()
        if(PKG_PUBLIC_DEPS)
            target_link_libraries(${NAME} INTERFACE ${PKG_PUBLIC_DEPS})
        endif()
        target_include_directories(${NAME} INTERFACE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        )
    else()
        add_library(${NAME} STATIC ${PKG_SOURCES} ${PKG_HEADERS})
        target_include_directories(${NAME} PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        )
        if(PKG_PUBLIC_DEPS)
            target_link_libraries(${NAME} PUBLIC ${PKG_PUBLIC_DEPS})
        endif()
        if(PKG_PRIVATE_DEPS)
            target_link_libraries(${NAME} PRIVATE ${PKG_PRIVATE_DEPS})
        endif()
    endif()

    add_library(engine::${NAME} ALIAS ${NAME})
endfunction()
