# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF
#
# dxilconv.dll, the DXBC to DXIL converter that the Direct3D 12 runtime loads
# for shader model 6 drivers, built from the dxc (DirectXShaderCompiler) feed
# against the ReactOS SDK. Upstream sources retain their original licenses.

if(NOT ENABLE_DXILCONV)
    return()
endif()

set(DXILCONV_SOURCE_DIR "${REACTOS_SOURCE_DIR}/submodules/dxc")
if(NOT EXISTS "${DXILCONV_SOURCE_DIR}/CMakeLists.txt")
    message(STATUS "dxilconv: the dxc feed is not checked out; building without it. "
        "Run scripts/feeds update dxc to build it.")
    return()
endif()

if(ARCH STREQUAL "amd64")
    set(_dxilconv_sdk_arch x86_64)
    set(_dxilconv_triple x86_64-w64-mingw32)
    set(_dxilconv_arch_flags -mlong-double-64)
elseif(ARCH STREQUAL "i386")
    set(_dxilconv_sdk_arch i386)
    set(_dxilconv_triple i686-w64-mingw32)
    set(_dxilconv_arch_flags -mlong-double-64)
elseif(ARCH STREQUAL "arm64")
    set(_dxilconv_sdk_arch aarch64)
    set(_dxilconv_triple aarch64-w64-mingw32)
    set(_dxilconv_arch_flags "")
else()
    message(FATAL_ERROR "dxilconv does not support ARCH ${ARCH}. Use -DENABLE_DXILCONV=OFF.")
endif()

find_program(DXILCONV_GIT_EXECUTABLE NAMES git REQUIRED)
find_program(DXILCONV_NINJA NAMES ninja REQUIRED)
find_program(DXILCONV_STRIP NAMES llvm-strip HINTS "${REACTOS_CLANG_LLVM_MINGW_ROOT}/bin" NO_CACHE REQUIRED)
set(_dxilconv_patch "${CMAKE_CURRENT_LIST_DIR}/dxc-reactos-sdk.patch")
execute_process(
    COMMAND "${DXILCONV_GIT_EXECUTABLE}" apply --reverse --check "${_dxilconv_patch}"
    WORKING_DIRECTORY "${DXILCONV_SOURCE_DIR}"
    RESULT_VARIABLE _dxilconv_patch_applied
    OUTPUT_QUIET ERROR_QUIET)
if(NOT _dxilconv_patch_applied EQUAL 0)
    execute_process(
        COMMAND "${DXILCONV_GIT_EXECUTABLE}" apply "${_dxilconv_patch}"
        WORKING_DIRECTORY "${DXILCONV_SOURCE_DIR}"
        RESULT_VARIABLE _dxilconv_patch_result
        OUTPUT_VARIABLE _dxilconv_patch_output
        ERROR_VARIABLE _dxilconv_patch_error)
    if(NOT _dxilconv_patch_result EQUAL 0)
        message(FATAL_ERROR "Failed to apply ${_dxilconv_patch}: ${_dxilconv_patch_output}${_dxilconv_patch_error}")
    endif()
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_dxilconv_patch}")

include("${REACTOS_SOURCE_DIR}/sdk/cmake/nested-build.cmake")
include(ExternalProject)

set(DXILCONV_WORK_DIR "${REACTOS_BINARY_DIR}/submodules/dxilconv")
set(DXILCONV_SDK_LIBDIR "${DXILCONV_WORK_DIR}/reactos-sdk-lib")
set(DXILCONV_HOST_DIR "${DXILCONV_WORK_DIR}/host-build")
set(DXILCONV_BUILD_DIR "${DXILCONV_WORK_DIR}/build")
set(DXILCONV_BUILD_DLL "${DXILCONV_BUILD_DIR}/bin/dxilconv.dll")
set(DXILCONV_DLL "${DXILCONV_WORK_DIR}/dxilconv.dll")
set(DXILCONV_LICENSE "${DXILCONV_WORK_DIR}/DirectXShaderCompiler-LICENSE.TXT")
configure_file("${DXILCONV_SOURCE_DIR}/.clang-format" "${DXILCONV_WORK_DIR}/.clang-format" COPYONLY)
set(_dxilconv_cache "${DXILCONV_SOURCE_DIR}/cmake/caches/PredefinedParams.cmake")
set(_dxilconv_common_args
    -C "${_dxilconv_cache}"
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_MAKE_PROGRAM:FILEPATH=${DXILCONV_NINJA}
    -DCMAKE_C_COMPILER_LAUNCHER=
    -DCMAKE_CXX_COMPILER_LAUNCHER=
    -DENABLE_SPIRV_CODEGEN=OFF
    -DSPIRV_BUILD_TESTS=OFF
    -DHLSL_INCLUDE_TESTS=OFF
    -DLLVM_INCLUDE_TESTS=OFF
    -DCLANG_INCLUDE_TESTS=OFF
    -DLLVM_ENABLE_ASSERTIONS=OFF)

ExternalProject_Add(dxilconv-tablegen
    EXCLUDE_FROM_ALL TRUE
    PREFIX "${DXILCONV_WORK_DIR}/host-prefix"
    SOURCE_DIR "${DXILCONV_SOURCE_DIR}"
    BINARY_DIR "${DXILCONV_HOST_DIR}"
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    PATCH_COMMAND ""
    CMAKE_GENERATOR Ninja
    CMAKE_ARGS
        ${_dxilconv_common_args}
        -DDIRECTX_HEADER_INCLUDE_DIR:PATH=${REACTOS_SOURCE_DIR}/sdk/include/psdk
    BUILD_COMMAND ${CMAKE_COMMAND} -E env CCACHE_DISABLE=1 SCCACHE_DISABLE=1
        ${REACTOS_NESTED_BUILD} <BINARY_DIR> --target llvm-tblgen clang-tblgen
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS "${DXILCONV_HOST_DIR}/bin/llvm-tblgen" "${DXILCONV_HOST_DIR}/bin/clang-tblgen"
    USES_TERMINAL_BUILD TRUE)

set(_dxilconv_sdk_targets
    msvcrtex libucrtbase libmsvcrt libkernel32 libntdll ucrtoldnames libuser32 libgdi32 libwinspool
    libshell32 libole32 liboleaut32 uuid libcomdlg32 libadvapi32 libpsapi)
export_sdk_libraries(dxilconv_sdk_libs "${DXILCONV_SDK_LIBDIR}"
    LIBRARIES ${_dxilconv_sdk_targets}
    DEPENDS ${_dxilconv_sdk_targets} ucrtoldnames_target)
string(JOIN " " _dxilconv_sdk_arch_flags ${_dxilconv_arch_flags})

ExternalProject_Add(dxilconv-build
    EXCLUDE_FROM_ALL TRUE
    DEPENDS dxilconv-tablegen
    PREFIX "${DXILCONV_WORK_DIR}/prefix"
    SOURCE_DIR "${DXILCONV_SOURCE_DIR}"
    BINARY_DIR "${DXILCONV_BUILD_DIR}"
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    PATCH_COMMAND ""
    CMAKE_GENERATOR Ninja
    CMAKE_ARGS
        ${_dxilconv_common_args}
        -DCMAKE_TOOLCHAIN_FILE:FILEPATH=${REACTOS_SOURCE_DIR}/submodules/reactos-sdk.cmake
        -DCMAKE_TRY_COMPILE_TARGET_TYPE:STRING=EXECUTABLE
        -DREACTOS_CLANG_LLVM_MINGW_ROOT:PATH=${REACTOS_CLANG_LLVM_MINGW_ROOT}
        -DREACTOS_SDK_ARCH:STRING=${_dxilconv_sdk_arch}
        -DREACTOS_SDK_TRIPLE:STRING=${_dxilconv_triple}
        "-DREACTOS_SDK_ARCH_FLAGS:STRING=${_dxilconv_sdk_arch_flags}"
        -DREACTOS_SDK_SOURCE_DIR:PATH=${REACTOS_SOURCE_DIR}
        -DREACTOS_SDK_BUILD_DIR:PATH=${REACTOS_BINARY_DIR}
        -DREACTOS_SDK_LIBDIR:PATH=${DXILCONV_SDK_LIBDIR}
        -DREACTOS_SDK_CXX_RUNTIME:PATH=${REACTOS_LIBCXX_ROOT}
        -DLLVM_TABLEGEN:FILEPATH=${DXILCONV_HOST_DIR}/bin/llvm-tblgen
        -DCLANG_TABLEGEN:FILEPATH=${DXILCONV_HOST_DIR}/bin/clang-tblgen
        -DD3D12_INCLUDE_DIR:PATH=${REACTOS_BINARY_DIR}/sdk/include/psdk
        -DDXGI_INCLUDE_DIR:PATH=${REACTOS_BINARY_DIR}/sdk/include/psdk
    BUILD_COMMAND ${CMAKE_COMMAND} -E env CCACHE_DISABLE=1 SCCACHE_DISABLE=1
        ${REACTOS_NESTED_BUILD} <BINARY_DIR> --target dxilconv
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS "${DXILCONV_BUILD_DLL}"
    USES_TERMINAL_BUILD TRUE)
ExternalProject_Add_StepDependencies(dxilconv-build configure dxilconv_sdk_libs d3d_idl_headers)

add_custom_command(
    OUTPUT "${DXILCONV_DLL}" "${DXILCONV_LICENSE}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DXILCONV_BUILD_DLL}" "${DXILCONV_DLL}"
    COMMAND ${DXILCONV_STRIP} --strip-debug "${DXILCONV_DLL}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DXILCONV_SOURCE_DIR}/LICENSE.TXT" "${DXILCONV_LICENSE}"
    DEPENDS dxilconv-build "${DXILCONV_BUILD_DLL}"
    VERBATIM)
add_custom_target(dxilconv DEPENDS "${DXILCONV_DLL}" "${DXILCONV_LICENSE}")
add_cd_file(FILE "${DXILCONV_DLL}" TARGET dxilconv DESTINATION reactos/system32 FOR all)
set_property(GLOBAL APPEND PROPERTY CI_SYSTEM_FILES "${DXILCONV_DLL}")
add_cd_file(FILE "${DXILCONV_LICENSE}" TARGET dxilconv DESTINATION reactos/3rdParty FOR all)
