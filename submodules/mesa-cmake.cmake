# SPDX-License-Identifier: GPL-3.0-or-later
# Common arguments for the Mesa CMake build against the ReactOS SDK.
# Homebrew's bison is keg-only; Apple's /usr/bin/bison is too old for Mesa.
set(_mesa_bison_hints)
if(CMAKE_HOST_APPLE)
    list(APPEND _mesa_bison_hints /opt/homebrew/opt/bison/bin /usr/local/opt/bison/bin)
endif()
set(_mesa_feed "${REACTOS_SOURCE_DIR}/submodules/mesa/CMakeLists.txt")
file(STRINGS "${_mesa_feed}" _mesa_interface REGEX "MESA_LIBERNT_SOURCE_DIR" LIMIT_COUNT 1)
if(NOT _mesa_interface)
    message(STATUS "The mesa feed predates this source tree; running scripts/feeds update mesa")
    execute_process(COMMAND sh "${REACTOS_SOURCE_DIR}/scripts/feeds" update mesa)
    file(STRINGS "${_mesa_feed}" _mesa_interface REGEX "MESA_LIBERNT_SOURCE_DIR" LIMIT_COUNT 1)
    if(NOT _mesa_interface)
        message(FATAL_ERROR "The mesa feed in submodules/mesa predates this source tree and was not updated. "
            "Run scripts/feeds update mesa. It leaves a feed with local changes untouched: move those onto the feed's branch first.")
    endif()
endif()
find_program(MESA_BISON NAMES bison HINTS ${_mesa_bison_hints} REQUIRED)
find_program(MESA_FLEX NAMES flex REQUIRED)
find_program(MESA_NINJA NAMES ninja REQUIRED)
find_program(MESA_PYTHON NAMES python3 python REQUIRED)
set(MESA_CMAKE_ARGS
    -DCMAKE_TOOLCHAIN_FILE:FILEPATH=${REACTOS_SOURCE_DIR}/submodules/reactos-sdk.cmake
    -DCMAKE_MAKE_PROGRAM:FILEPATH=${MESA_NINJA}
    -DCMAKE_BUILD_TYPE:STRING=Release
    -DREACTOS_CLANG_LLVM_MINGW_ROOT:PATH=${REACTOS_CLANG_LLVM_MINGW_ROOT}
    -DMESA_LIBERNT_SDK:BOOL=ON
    -DPython3_EXECUTABLE:FILEPATH=${MESA_PYTHON}
    -DBISON_EXECUTABLE:FILEPATH=${MESA_BISON}
    -DFLEX_EXECUTABLE:FILEPATH=${MESA_FLEX})
