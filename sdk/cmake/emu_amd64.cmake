# PROJECT:     LiberNT Build System
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the AMD64 guest runtime of the AMD64 emulator in a nested tree
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

include("${REACTOS_SOURCE_DIR}/sdk/cmake/emu_amd64_targets.cmake")

set(EMU_AMD64_BINARY_DIR "${REACTOS_BINARY_DIR}/_emu_amd64")
set(EMU_AMD64_TARGETS ${EMU_AMD64_MODULES} ${EMU_AMD64_AUXILIARY_MODULES} ${EMU_AMD64_EXECUTABLES})

set_property(GLOBAL PROPERTY EMU_AMD64_VISITED_TARGETS)
set_property(GLOBAL PROPERTY EMU_AMD64_LINKED_MODULES)

function(_emu_amd64_collect_linked_modules _target)
    get_property(_visited GLOBAL PROPERTY EMU_AMD64_VISITED_TARGETS)
    if(_target IN_LIST _visited)
        return()
    endif()
    set_property(GLOBAL APPEND PROPERTY EMU_AMD64_VISITED_TARGETS "${_target}")

    get_target_property(_type "${_target}" TYPE)
    if(_type STREQUAL "MODULE_LIBRARY" OR _type STREQUAL "SHARED_LIBRARY")
        set_property(GLOBAL APPEND PROPERTY EMU_AMD64_LINKED_MODULES "${_target}")
    endif()

    foreach(_property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(_links "${_target}" "${_property}")
        if(NOT _links OR _links MATCHES "-NOTFOUND$")
            continue()
        endif()

        foreach(_link IN LISTS _links)
            string(REGEX REPLACE "^\\$<LINK_ONLY:([^>]+)>$" "\\1" _dependency "${_link}")
            if(TARGET "${_dependency}")
                _emu_amd64_collect_linked_modules("${_dependency}")
            endif()

            if(_dependency MATCHES "^lib(.+)$")
                set(_module "${CMAKE_MATCH_1}")
                if(NOT TARGET "${_module}")
                    string(REPLACE "." "_" _module "${_module}")
                endif()
                if(TARGET "${_module}")
                    get_target_property(_dependency_type "${_module}" TYPE)
                    if(_dependency_type STREQUAL "MODULE_LIBRARY" OR _dependency_type STREQUAL "SHARED_LIBRARY")
                        _emu_amd64_collect_linked_modules("${_module}")
                    endif()
                endif()
            endif()
        endforeach()
    endforeach()
endfunction()

foreach(_target IN LISTS EMU_AMD64_TARGETS)
    if(NOT TARGET "${_target}")
        message(FATAL_ERROR "emu_amd64_targets.cmake lists '${_target}', which is not a target in this tree")
    endif()
    _emu_amd64_collect_linked_modules("${_target}")
endforeach()

get_property(_emu_amd64_linked_modules GLOBAL PROPERTY EMU_AMD64_LINKED_MODULES)
set(_emu_amd64_missing_modules)
foreach(_module IN LISTS _emu_amd64_linked_modules)
    if(NOT _module IN_LIST EMU_AMD64_TARGETS)
        list(APPEND _emu_amd64_missing_modules "${_module}")
    endif()
endforeach()

if(_emu_amd64_missing_modules)
    list(SORT _emu_amd64_missing_modules)
    list(JOIN _emu_amd64_missing_modules "\n    " _emu_amd64_missing_text)
    message(FATAL_ERROR "AMD64 guest targets import DLLs that are not listed in "
        "sdk/cmake/compat_runtime_targets.cmake. Add these modules to COMPAT_RUNTIME_MODULES:\n"
        "    ${_emu_amd64_missing_text}")
endif()

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
    if(_suffix MATCHES "-NOTFOUND$")
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
foreach(_target IN LISTS EMU_AMD64_MODULES EMU_AMD64_EXECUTABLES)
    _emu_amd64_get_target_file("${_target}" _file)
    list(APPEND EMU_AMD64_FILES "${_file}")
endforeach()

set(EMU_AMD64_ICD_FILE)
if(TARGET mesa_host)
    set(EMU_AMD64_ICD_FILE "${EMU_AMD64_BINARY_DIR}/dll/opengl/glthunk/glthunk.dll")
    list(APPEND EMU_AMD64_TARGETS glthunk)
endif()

set(EMU_AMD64_BUILT_FILES ${EMU_AMD64_FILES} ${EMU_AMD64_ICD_FILE})
foreach(_target IN LISTS EMU_AMD64_AUXILIARY_MODULES)
    _emu_amd64_get_target_file("${_target}" _file)
    list(APPEND EMU_AMD64_BUILT_FILES "${_file}")
endforeach()

set(EMU_AMD64_ALIAS_FILES)
set(EMU_AMD64_ALIAS_SOURCES)
set(EMU_AMD64_ARCH_COMMAND_FILES)
foreach(_alias IN LISTS EMU_AMD64_ALIASES)
    if(NOT _alias MATCHES "^([^=]+)=(.+)$")
        message(FATAL_ERROR "Invalid AMD64 guest alias '${_alias}'; expected target=filename")
    endif()
    set(_alias_target "${CMAKE_MATCH_1}")
    set(_alias_name "${CMAKE_MATCH_2}")
    if(NOT _alias_target IN_LIST EMU_AMD64_TARGETS)
        message(FATAL_ERROR "AMD64 guest alias target '${_alias_target}' is not in the AMD64 target list")
    endif()
    _emu_amd64_get_target_file("${_alias_target}" _alias_source)
    list(APPEND EMU_AMD64_ALIAS_SOURCES "${_alias_source}")
    list(APPEND EMU_AMD64_ALIAS_FILES "${REACTOS_BINARY_DIR}/CMakeFiles/emu-amd64-aliases/${_alias_name}")
    if(_alias_target IN_LIST EMU_AMD64_ARCH_COMMAND_TARGETS)
        list(APPEND EMU_AMD64_ARCH_COMMAND_FILES "${REACTOS_BINARY_DIR}/CMakeFiles/emu-amd64-aliases/${_alias_name}")
    endif()
endforeach()

get_filename_component(_emu_amd64_toolchain "${CMAKE_TOOLCHAIN_FILE}" ABSOLUTE BASE_DIR "${REACTOS_SOURCE_DIR}")
set(_emu_amd64_cmake_args
    -DARCH:STRING=amd64
    -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
    -DCMAKE_TOOLCHAIN_FILE:FILEPATH=${_emu_amd64_toolchain}
    -DDBG:BOOL=${DBG}
    -DENABLE_CCACHE:BOOL=${ENABLE_CCACHE}
    -DENABLE_ROSTESTS:BOOL=OFF
    -DENABLE_WOW64:BOOL=OFF
    -DNVS:BOOL=ON
    -DKD_DEBUGGER:STRING=NONE
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
list(APPEND _emu_amd64_cmake_args -DMESA_GALLIUM_FROM_SOURCE:BOOL=OFF)

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
    USES_TERMINAL
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
    BYPRODUCTS ${EMU_AMD64_BUILT_FILES}
    COMMENT "Building ${_emu_amd64_target_count} AMD64 guest runtime targets"
    USES_TERMINAL
    VERBATIM)
add_dependencies(emu_amd64 emu_amd64_configure)

add_cd_file(TARGET emu_amd64 FILE ${EMU_AMD64_FILES} DESTINATION reactos/SystemAMD64 FOR all)
if(EMU_AMD64_ICD_FILE)
    add_cd_file(TARGET emu_amd64 FILE ${EMU_AMD64_ICD_FILE} DESTINATION reactos/SystemAMD64 NAME_ON_CD mesa_gallium.dll FOR all)
endif()

_emu_amd64_get_target_file(wbemprox _emu_amd64_wbemprox_file)
_emu_amd64_get_target_file(wbemdisp _emu_amd64_wbemdisp_file)
_emu_amd64_get_target_file(wmiutils _emu_amd64_wmiutils_file)
add_cd_file(TARGET emu_amd64 FILE ${_emu_amd64_wbemprox_file} ${_emu_amd64_wbemdisp_file} ${_emu_amd64_wmiutils_file}
    DESTINATION reactos/SystemAMD64/wbem FOR all)

_emu_amd64_get_target_file(comctl32 _emu_amd64_comctl32_file)
_emu_amd64_get_target_file(comctl32_v6 _emu_amd64_comctl32_v6_file)
_emu_amd64_get_target_file(gdiplus _emu_amd64_gdiplus_file)
_emu_amd64_get_target_file(msvcm80 _emu_amd64_msvcm80_file)
_emu_amd64_get_target_file(msvcp80 _emu_amd64_msvcp80_file)
_emu_amd64_get_target_file(msvcr80 _emu_amd64_msvcr80_file)
_emu_amd64_get_target_file(msvcm90 _emu_amd64_msvcm90_file)
_emu_amd64_get_target_file(msvcp90 _emu_amd64_msvcp90_file)
_emu_amd64_get_target_file(msvcr90 _emu_amd64_msvcr90_file)
add_cd_file(TARGET emu_amd64 FILE ${_emu_amd64_comctl32_file} DESTINATION reactos/winsxs/amd64_microsoft.windows.common-controls_6595b64144ccf1df_5.82.2600.2982_none_deadbeef FOR all)
add_cd_file(TARGET emu_amd64 FILE ${_emu_amd64_comctl32_v6_file} DESTINATION reactos/winsxs/amd64_microsoft.windows.common-controls_6595b64144ccf1df_6.0.2600.2982_none_deadbeef FOR all)
add_cd_file(TARGET emu_amd64 FILE ${_emu_amd64_gdiplus_file} DESTINATION reactos/winsxs/amd64_microsoft.windows.gdiplus_6595b64144ccf1df_1.1.7601.23038_none_deadbeef FOR all)
add_cd_file(TARGET emu_amd64 FILE ${_emu_amd64_gdiplus_file} DESTINATION reactos/winsxs/amd64_microsoft.windows.gdiplus_6595b64144ccf1df_1.0.14393.0_none_deadbeef FOR all)
add_cd_file(TARGET emu_amd64 FILE ${_emu_amd64_msvcr80_file} ${_emu_amd64_msvcp80_file} ${_emu_amd64_msvcm80_file} DESTINATION reactos/winsxs/amd64_microsoft.vc80.crt_1fc8b3b9a1e18e3b_8.0.50727.9680_none_deadbeef FOR all)
add_cd_file(TARGET emu_amd64 FILE ${_emu_amd64_msvcr90_file} ${_emu_amd64_msvcp90_file} ${_emu_amd64_msvcm90_file} DESTINATION reactos/winsxs/amd64_microsoft.vc90.crt_1fc8b3b9a1e18e3b_9.0.30729.6161_none_deadbeef FOR all)
add_cd_file(FILE
    ${REACTOS_SOURCE_DIR}/dll/win32/comctl32/amd64_microsoft.windows.common-controls_6595b64144ccf1df_5.82.2600.2982_none_deadbeef.manifest
    ${REACTOS_SOURCE_DIR}/dll/win32/comctl32/amd64_microsoft.windows.common-controls_6595b64144ccf1df_6.0.2600.2982_none_deadbeef.manifest
    ${REACTOS_SOURCE_DIR}/dll/win32/gdiplus/amd64_microsoft.windows.gdiplus_6595b64144ccf1df_1.1.7601.23038_none_deadbeef.manifest
    ${REACTOS_SOURCE_DIR}/dll/win32/gdiplus/amd64_microsoft.windows.gdiplus_6595b64144ccf1df_1.0.14393.0_none_deadbeef.manifest
    ${REACTOS_SOURCE_DIR}/dll/win32/msvcr80/amd64_microsoft.vc80.crt_1fc8b3b9a1e18e3b_8.0.50727.9680_none_deadbeef.manifest
    ${REACTOS_SOURCE_DIR}/dll/win32/msvcr90/amd64_microsoft.vc90.crt_1fc8b3b9a1e18e3b_9.0.30729.6161_none_deadbeef.manifest
    DESTINATION reactos/winsxs/manifests FOR all)

if(EMU_AMD64_ALIAS_FILES)
    list(LENGTH EMU_AMD64_ALIAS_FILES _emu_amd64_alias_count)
    math(EXPR _emu_amd64_alias_last "${_emu_amd64_alias_count} - 1")
    foreach(_alias_index RANGE 0 ${_emu_amd64_alias_last})
        list(GET EMU_AMD64_ALIAS_SOURCES ${_alias_index} _alias_source)
        list(GET EMU_AMD64_ALIAS_FILES ${_alias_index} _alias_file)
        add_custom_command(
            OUTPUT "${_alias_file}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${REACTOS_BINARY_DIR}/CMakeFiles/emu-amd64-aliases"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_alias_source}" "${_alias_file}"
            DEPENDS emu_amd64 "${_alias_source}"
            COMMENT "Creating AMD64 guest alias ${_alias_file}"
            VERBATIM)
    endforeach()
    add_custom_target(emu_amd64_aliases DEPENDS ${EMU_AMD64_ALIAS_FILES})
    add_cd_file(TARGET emu_amd64_aliases FILE ${EMU_AMD64_ALIAS_FILES} DESTINATION reactos/SystemAMD64 FOR all)
    add_cd_file(TARGET emu_amd64_aliases FILE ${EMU_AMD64_ARCH_COMMAND_FILES} DESTINATION reactos/system32 FOR all)
endif()

message(STATUS "AMD64 emulation: building ${_emu_amd64_target_count} AMD64 guest targets into SystemAMD64")
