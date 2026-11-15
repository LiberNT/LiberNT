# SPDX-License-Identifier: GPL-3.0-or-later
# Resolve the job count when the build runs, not when CMake configures it.
if(MESA_PYTHON)
    set(_nested_build_python "${MESA_PYTHON}")
else()
    find_program(NESTED_BUILD_PYTHON_EXECUTABLE NAMES python3 python REQUIRED)
    set(_nested_build_python "${NESTED_BUILD_PYTHON_EXECUTABLE}")
endif()
set(REACTOS_NESTED_BUILD
    "${_nested_build_python}"
    "${CMAKE_CURRENT_LIST_DIR}/build-with-parallel.py"
    "${CMAKE_COMMAND}" --build)

# A third-party CMake build compiles through ccache when the tree does.
if(ENABLE_CCACHE)
    set(REACTOS_NESTED_COMPILER_LAUNCHER
        -DCMAKE_C_COMPILER_LAUNCHER:STRING=ccache
        -DCMAKE_CXX_COMPILER_LAUNCHER:STRING=ccache)
else()
    set(REACTOS_NESTED_COMPILER_LAUNCHER
        -DCMAKE_C_COMPILER_LAUNCHER:STRING=
        -DCMAKE_CXX_COMPILER_LAUNCHER:STRING=)
endif()
