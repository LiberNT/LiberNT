# PROJECT:     LiberNT Build System
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the AMD64 guest runtime of the AMD64 emulator in a nested tree
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

include("${REACTOS_SOURCE_DIR}/sdk/cmake/emu_amd64_targets.cmake")

set(EMU_AMD64_BINARY_DIR "${REACTOS_BINARY_DIR}/_emu_amd64")
set(EMU_AMD64_TARGETS ${EMU_AMD64_MODULES} ${EMU_AMD64_EXECUTABLES})

foreach(_target IN LISTS EMU_AMD64_TARGETS)
    if(NOT TARGET "${_target}")
        message(FATAL_ERROR "emu_amd64_targets.cmake lists '${_target}', which is not a target in this tree")
    endif()
endforeach()

function(_emu_amd64_get_target_file _target _output)
    get_target_property(_binary_dir "${_target}" BINARY_DIR)
    get_target_property(_type "${_target}" TYPE)
    get_target_property(_output_name "${_target}" OUTPUT_NAME)
    get_target_property(_prefix "${_target}" PREFIX)
    get_target_property(_suffix "${_target}" SUFFIX)

    if(NOT _output_name OR _output_name MATCHES "-NOTFOUND$")
        set(_output_name "${_target}")
    endif()
    if(NOT _prefix OR _prefix MATCHES "-NOTFOUND$")
        if(_type STREQUAL "EXECUTABLE")
            set(_prefix "${CMAKE_EXECUTABLE_PREFIX}")
        else()
            set(_prefix "${CMAKE_SHARED_MODULE_PREFIX}")
        endif()
    endif()
    if(NOT _suffix OR _suffix MATCHES "-NOTFOUND$")
        if(_type STREQUAL "EXECUTABLE")
            set(_suffix "${CMAKE_EXECUTABLE_SUFFIX}")
        else()
            set(_suffix "${CMAKE_SHARED_MODULE_SUFFIX}")
        endif()
    endif()

    file(RELATIVE_PATH _relative_dir "${REACTOS_BINARY_DIR}" "${_binary_dir}")
    set(${_output} "${EMU_AMD64_BINARY_DIR}/${_relative_dir}/${_prefix}${_output_name}${_suffix}" PARENT_SCOPE)
endfunction()

set(EMU_AMD64_FILES)
foreach(_target IN LISTS EMU_AMD64_TARGETS)
    _emu_amd64_get_target_file("${_target}" _file)
    list(APPEND EMU_AMD64_FILES "${_file}")
endforeach()

get_filename_component(_emu_amd64_toolchain "${CMAKE_TOOLCHAIN_FILE}" ABSOLUTE BASE_DIR "${REACTOS_SOURCE_DIR}")
set(_emu_amd64_cmake_args
    -DARCH:STRING=amd64
    -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
    -DCMAKE_TOOLCHAIN_FILE:FILEPATH=${_emu_amd64_toolchain}
    -DDBG:BOOL=${DBG}
    -DENABLE_ROSTESTS:BOOL=OFF
    -DENABLE_WOW64:BOOL=OFF
    -DNVS:BOOL=ON
    -DKD_DEBUGGER:STRING=NONE
    -DMESA_GALLIUM_FROM_SOURCE:BOOL=OFF
    -DOPTIMIZE:STRING=${OPTIMIZE}
    -DPCH:BOOL=${PCH}
    -DREACTOS_CLANG_LLVM_MINGW_ROOT:PATH=${REACTOS_CLANG_LLVM_MINGW_ROOT}
    -DREACTOS_GRAPHICS_DRIVER_MODEL:STRING=${REACTOS_GRAPHICS_DRIVER_MODEL}
    -DREACTOS_TARGET_NT:STRING=${REACTOS_TARGET_NT}
    -DREACTOS_WDDM_LEVEL:STRING=${REACTOS_WDDM_LEVEL}
    -DROSCONFIG_PROFILE:STRING=generic
    -DROSCONFIG_SKIP_OVERRIDES:BOOL=ON
    -DSEPARATE_DBG:BOOL=${SEPARATE_DBG}
    -DUSE_DUMMY_PSEH:BOOL=${USE_DUMMY_PSEH}
    -DWITH_DEBUG_SYMBOLS:BOOL=${WITH_DEBUG_SYMBOLS})

string(JOIN "\n" _emu_amd64_configure_signature ${_emu_amd64_cmake_args})
set(_emu_amd64_signature_file "${REACTOS_BINARY_DIR}/CMakeFiles/emu-amd64-configure.txt")
set(_emu_amd64_configure_stamp "${REACTOS_BINARY_DIR}/CMakeFiles/emu-amd64-configure.stamp")
file(GENERATE OUTPUT "${_emu_amd64_signature_file}" CONTENT "${_emu_amd64_configure_signature}\n")

find_program(NESTED_BUILD_PYTHON_EXECUTABLE NAMES python3 python REQUIRED)
add_custom_command(
    OUTPUT "${_emu_amd64_configure_stamp}"
    COMMAND ${CMAKE_COMMAND} -S "${REACTOS_SOURCE_DIR}" -B "${EMU_AMD64_BINARY_DIR}" -G "${CMAKE_GENERATOR}" ${_emu_amd64_cmake_args}
    COMMAND "${NESTED_BUILD_PYTHON_EXECUTABLE}"
        "${REACTOS_SOURCE_DIR}/sdk/tools/cxx-runtime/build-cxx-runtime.py" --reactos-build "${EMU_AMD64_BINARY_DIR}"
    COMMAND ${CMAKE_COMMAND} -E touch "${_emu_amd64_configure_stamp}"
    BYPRODUCTS "${EMU_AMD64_BINARY_DIR}/build.ninja"
    DEPENDS "${_emu_amd64_signature_file}"
    COMMENT "Configuring the AMD64 guest runtime"
    VERBATIM)

add_custom_target(emu_amd64_configure DEPENDS "${_emu_amd64_configure_stamp}")

if(CMAKE_GENERATOR STREQUAL "Ninja")
    set(_emu_amd64_heal COMMAND ${CMAKE_MAKE_PROGRAM} -C "${EMU_AMD64_BINARY_DIR}" -t recompact)
else()
    set(_emu_amd64_heal)
endif()

include("${REACTOS_SOURCE_DIR}/sdk/cmake/nested-build.cmake")

list(LENGTH EMU_AMD64_TARGETS _emu_amd64_target_count)
add_custom_target(emu_amd64 ALL
    ${_emu_amd64_heal}
    COMMAND ${REACTOS_NESTED_BUILD} "${EMU_AMD64_BINARY_DIR}" --target ${EMU_AMD64_TARGETS}
    BYPRODUCTS ${EMU_AMD64_FILES}
    COMMENT "Building ${_emu_amd64_target_count} AMD64 guest runtime targets"
    VERBATIM)
add_dependencies(emu_amd64 emu_amd64_configure)

add_cd_file(TARGET emu_amd64 FILE ${EMU_AMD64_FILES} DESTINATION reactos/SystemAMD64 FOR all)

message(STATUS "AMD64 emulation: building ${_emu_amd64_target_count} AMD64 guest targets into SystemAMD64")
