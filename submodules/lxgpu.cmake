# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif.ing@outlook.com>
# GPU renderer of the Linux subsystem: lxgpu.exe with virglrenderer (OpenGL and the venus Vulkan renderer)
# and libepoxy, MIT, from pinned upstream release sources with the Windows patch below, built with the
# LiberNT SDK and packaged next to lxwinhost.exe, which starts it.

if(ARM64EC_RUNTIME)
    return()
endif()
if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang" OR MSVC)
    # TODO: build the renderer with the GCC toolchain
    message(STATUS "The Linux GPU renderer needs the RosBE Clang toolchain; lxgpu.exe is not built.")
    return()
endif()

set(LXGPU_VIRGLRENDERER_ARCHIVE "https://gitlab.freedesktop.org/virgl/virglrenderer/-/archive/1.1.1/virglrenderer-1.1.1.tar.gz"
    CACHE STRING "virglrenderer 1.1.1 source archive (URL or local file)")
set(LXGPU_EPOXY_ARCHIVE "https://github.com/anholt/libepoxy/archive/refs/tags/1.5.10.tar.gz"
    CACHE STRING "libepoxy 1.5.10 source archive (URL or local file)")

if(ARCH STREQUAL "amd64")
    set(_lxgpu_cpu x86_64)
    set(_lxgpu_arch_flags -mlong-double-64)
elseif(ARCH STREQUAL "arm64")
    set(_lxgpu_cpu aarch64)
    set(_lxgpu_arch_flags)
else()
    set(_lxgpu_cpu riscv64)
    set(_lxgpu_arch_flags)
endif()

find_program(LXGPU_NINJA NAMES ninja REQUIRED)
find_program(LXGPU_PATCH NAMES patch REQUIRED)
find_program(LXGPU_PYTHON NAMES python3 python REQUIRED)
find_program(LXGPU_STRIP NAMES llvm-strip HINTS "${REACTOS_CLANG_LLVM_MINGW_ROOT}/bin" NO_CACHE REQUIRED)
execute_process(COMMAND "${LXGPU_PYTHON}" -c "import yaml" RESULT_VARIABLE _lxgpu_python_status
                ERROR_VARIABLE _lxgpu_python_error)
if(NOT _lxgpu_python_status EQUAL 0)
    message(FATAL_ERROR "The Linux GPU renderer needs the Python yaml module for ${LXGPU_PYTHON}: ${_lxgpu_python_error}")
endif()
include("${REACTOS_SOURCE_DIR}/sdk/cmake/nested-build.cmake")
include(ExternalProject)

set(LXGPU_WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/lxgpu")
set(LXGPU_SDK_LIBDIR "${LXGPU_WORK_DIR}/reactos-sdk-lib")
set(LXGPU_VIRGL_SOURCE_DIR "${LXGPU_WORK_DIR}/virglrenderer-source")
set(LXGPU_EPOXY_SOURCE_DIR "${LXGPU_WORK_DIR}/libepoxy-source")
set(LXGPU_BINARY_DIR "${LXGPU_WORK_DIR}/build")
set(LXGPU_EXE "${LXGPU_WORK_DIR}/lxgpu.exe")
set(LXGPU_VIRGL_PATCH "${CMAKE_CURRENT_LIST_DIR}/lxgpu/virglrenderer-windows.patch")
# the hash in the patch command makes ExternalProject patch again when the patch changes
file(SHA256 "${LXGPU_VIRGL_PATCH}" _lxgpu_virgl_patch_sha256)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${LXGPU_VIRGL_PATCH}")
set(LXGPU_LICENSES "${LXGPU_WORK_DIR}/lxgpu-licenses.zip")

set(_lxgpu_sdk_targets
    msvcrtex libucrtbase libmsvcrt libkernel32 libntdll ucrtoldnames libuser32 libgdi32 libwinspool libshell32
    libole32 liboleaut32 uuid libcomdlg32 libadvapi32 libopengl32 libwinmm)
export_sdk_libraries(lxgpu_sdk_libs "${LXGPU_SDK_LIBDIR}"
    LIBRARIES ${_lxgpu_sdk_targets}
    DEPENDS ${_lxgpu_sdk_targets} ucrtoldnames_target)

ExternalProject_Add(lxgpu-virglrenderer-source
    EXCLUDE_FROM_ALL TRUE
    PREFIX "${LXGPU_WORK_DIR}/virglrenderer-prefix"
    URL "${LXGPU_VIRGLRENDERER_ARCHIVE}"
    URL_HASH SHA256=1fc34ffbf2a4f5cf161228f13242e28283bd818901d7daf0027e6ae5fcaeaf9b
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_DIR "${LXGPU_VIRGL_SOURCE_DIR}"
    PATCH_COMMAND ${CMAKE_COMMAND}
        -DPATCH_EXECUTABLE=${LXGPU_PATCH}
        -DSOURCE_DIR=<SOURCE_DIR>
        -DPATCH_FILE=${LXGPU_VIRGL_PATCH}
        -DPATCH_SHA256=${_lxgpu_virgl_patch_sha256}
        -P ${CMAKE_CURRENT_LIST_DIR}/lxgpu/apply-patch.cmake
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND "")

ExternalProject_Add(lxgpu-epoxy-source
    EXCLUDE_FROM_ALL TRUE
    PREFIX "${LXGPU_WORK_DIR}/libepoxy-prefix"
    URL "${LXGPU_EPOXY_ARCHIVE}"
    URL_HASH SHA256=a7ced37f4102b745ac86d6a70a9da399cc139ff168ba6b8002b4d8d43c900c15
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_DIR "${LXGPU_EPOXY_SOURCE_DIR}"
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND "")

string(JOIN " " _lxgpu_arch_flags_string ${_lxgpu_arch_flags})
ExternalProject_Add(lxgpu-build
    EXCLUDE_FROM_ALL TRUE
    DEPENDS lxgpu-virglrenderer-source lxgpu-epoxy-source
    PREFIX "${LXGPU_WORK_DIR}/build-prefix"
    SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/lxgpu"
    BINARY_DIR "${LXGPU_BINARY_DIR}"
    CMAKE_GENERATOR Ninja
    CMAKE_ARGS
        -DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_CURRENT_LIST_DIR}/reactos-sdk.cmake
        -DCMAKE_MAKE_PROGRAM:FILEPATH=${LXGPU_NINJA}
        -DCMAKE_BUILD_TYPE:STRING=Release
        -DREACTOS_CLANG_LLVM_MINGW_ROOT:PATH=${REACTOS_CLANG_LLVM_MINGW_ROOT}
        -DREACTOS_SDK_ARCH:STRING=${_lxgpu_cpu}
        -DREACTOS_SDK_TRIPLE:STRING=${_lxgpu_cpu}-w64-mingw32
        "-DREACTOS_SDK_ARCH_FLAGS:STRING=${_lxgpu_arch_flags_string}"
        -DREACTOS_SDK_SOURCE_DIR:PATH=${REACTOS_SOURCE_DIR}
        -DREACTOS_SDK_BUILD_DIR:PATH=${REACTOS_BINARY_DIR}
        -DREACTOS_SDK_LIBDIR:PATH=${LXGPU_SDK_LIBDIR}
        -DREACTOS_SDK_CXX_RUNTIME:PATH=${REACTOS_LIBCXX_ROOT}
        -DEPOXY_SOURCE_DIR:PATH=${LXGPU_EPOXY_SOURCE_DIR}
        -DVIRGL_SOURCE_DIR:PATH=${LXGPU_VIRGL_SOURCE_DIR}
        -DLXGPU_SOURCE_DIR:PATH=${REACTOS_SOURCE_DIR}/subsystems/linux/lxgpu
        -DLXGPU_INCLUDE_DIR:PATH=${REACTOS_SOURCE_DIR}/subsystems/linux/include
        -DLXGPU_PYTHON:FILEPATH=${LXGPU_PYTHON}
        -DLXGPU_CPU:STRING=${_lxgpu_cpu}
    BUILD_COMMAND ${CMAKE_COMMAND} -E env CCACHE_DISABLE=1 SCCACHE_DISABLE=1 ${REACTOS_NESTED_BUILD} <BINARY_DIR>
    BUILD_ALWAYS TRUE
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS "${LXGPU_BINARY_DIR}/lxgpu.exe"
    USES_TERMINAL_CONFIGURE TRUE
    USES_TERMINAL_BUILD TRUE)
ExternalProject_Add_StepDependencies(lxgpu-build configure lxgpu_sdk_libs)

add_custom_command(
    OUTPUT "${LXGPU_EXE}" "${LXGPU_LICENSES}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LXGPU_BINARY_DIR}/lxgpu.exe" "${LXGPU_EXE}"
    COMMAND ${LXGPU_STRIP} --strip-debug "${LXGPU_EXE}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LXGPU_WORK_DIR}/license-bundle"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LXGPU_VIRGL_SOURCE_DIR}/COPYING"
            "${LXGPU_WORK_DIR}/license-bundle/virglrenderer-COPYING.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LXGPU_EPOXY_SOURCE_DIR}/COPYING"
            "${LXGPU_WORK_DIR}/license-bundle/libepoxy-COPYING.txt"
    COMMAND ${CMAKE_COMMAND} -E chdir "${LXGPU_WORK_DIR}/license-bundle" ${CMAKE_COMMAND} -E tar cf "${LXGPU_LICENSES}"
            --format=zip virglrenderer-COPYING.txt libepoxy-COPYING.txt
    DEPENDS lxgpu-build "${LXGPU_BINARY_DIR}/lxgpu.exe"
    VERBATIM)

add_custom_target(lxgpu ALL DEPENDS "${LXGPU_EXE}" "${LXGPU_LICENSES}")
add_cd_file(FILE "${LXGPU_EXE}" TARGET lxgpu DESTINATION reactos/system32 FOR all)
add_cd_file(FILE "${LXGPU_LICENSES}" TARGET lxgpu DESTINATION reactos/3rdParty FOR all)
