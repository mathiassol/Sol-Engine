# Prints where the Windows SDK DXC runtime lives, or nothing if it is absent.
#
#   cmake -P tools/report-dxc.cmake
#
# Exists so tools/check-prereqs.ps1 can answer "is the Windows SDK installed"
# using the *same* search the build uses, instead of a second copy of the path
# logic that would drift away from cmake/EngineDxc.cmake. A duplicated search
# that disagrees with the build is worse than no check: it reports success on a
# machine where configure then fails.
#
# Run in script mode, so CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION is unset and
# engine_locate_dxc falls to its glob branch. That is the general case anyway.
cmake_minimum_required(VERSION 3.24)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../cmake")
include(EngineDxc)

engine_locate_dxc()

# NOTICE goes to stderr like every other message() level, but unlike WARNING it
# carries no "CMake Warning" banner, so the caller can read the value cleanly.
message(NOTICE "ENGINE_DXC_BIN_DIR=${ENGINE_DXC_BIN_DIR}")
