# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>

if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang" OR MSVC)
    message(FATAL_ERROR "The Mesa Gallium source build requires the RosBE Clang toolchain; use MESA_GALLIUM_FROM_SOURCE=OFF for the packaged ICDs.")
endif()

set(MESA_SOURCE_DIR "${REACTOS_SOURCE_DIR}/submodules/mesa")
if(NOT EXISTS "${MESA_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "The vendored Mesa source is missing at ${MESA_SOURCE_DIR}")
endif()
# Parallelism is inherited at build time; discard the old fixed job limit.
unset(MESA_BUILD_JOBS CACHE)

if(ARCH STREQUAL "riscv64" AND NOT SPACEMIT_K1_SUPPORT)
    return()
endif()

set(_mesa_arch ${ARCH})
set(_mesa_arch_flags)
set(_mesa_features)
set(_mesa_support_targets)
set(_mesa_d3d_umd_name)
if(ARCH STREQUAL "amd64")
    set(_mesa_processor x86_64)
    set(_mesa_triple x86_64-w64-mingw32)
    set(_mesa_arch_flags -mlong-double-64)
elseif(ARCH STREQUAL "i386")
    set(_mesa_processor i386)
    set(_mesa_triple i686-w64-mingw32)
    set(_mesa_arch_flags -mlong-double-64)
    set(_mesa_support_targets vc4kmt zlib)
    set(_mesa_d3d_umd_name rpi5vc4d3d)
elseif(ARCH STREQUAL "arm64")
    set(_mesa_processor aarch64)
    set(_mesa_triple aarch64-w64-mingw32)
    if(ARM64EC_RUNTIME)
        set(_mesa_arch arm64ec)
        set(_mesa_processor arm64ec)
        set(_mesa_triple arm64ec-w64-mingw32)
    else()
        set(_mesa_d3d_umd_name rpi5vc4d3d)
    endif()
    set(_mesa_support_targets rpi3vc4kmt vc4kmt zlib)
elseif(ARCH STREQUAL "riscv64")
    set(_mesa_processor riscv64)
    set(_mesa_triple riscv64-w64-mingw32)
    set(_mesa_arch_flags -march=${OARCH} -mabi=lp64 -mcmodel=medany -mno-relax)
    set(_mesa_features -DMESA_IMAGINATION:BOOL=ON -DMESA_ZINK:BOOL=ON)
    set(_mesa_support_targets pvrkmt)
    set(_mesa_d3d_umd_name d3d10zink)
else()
    message(FATAL_ERROR "Mesa Gallium requires an i386, amd64, arm64 or riscv64 target.")
endif()
string(JOIN " " _mesa_arch_flags ${_mesa_arch_flags})

set(_mesa_lavapipe_enabled OFF)
if(ENABLE_MESA_LLVMPIPE AND NOT ARM64EC_RUNTIME)
    set(_mesa_lavapipe_enabled ON)
endif()
if(ENABLE_MESA_LLVMPIPE)
    set(MESA_LLVM_ROOT "" CACHE PATH "Optional prebuilt Windows static LLVM installation for the selected architecture")
    if(MESA_LLVM_ROOT)
        get_filename_component(MESA_LLVM_PREFIX "${MESA_LLVM_ROOT}" ABSOLUTE)
    else()
        set(MESA_LLVM_PREFIX "${REACTOS_BINARY_DIR}/submodules/mesa-llvmpipe/llvm-install")
    endif()
    list(APPEND _mesa_features
        -DMESA_LLVMPIPE:BOOL=ON
        -DMESA_LAVAPIPE:BOOL=${_mesa_lavapipe_enabled}
        -DMESA_LLVM_ROOT:PATH=${MESA_LLVM_PREFIX}
        -DMESA_DIRECTX_HEADERS_ROOT:PATH=${REACTOS_BINARY_DIR}/submodules/mesa-llvmpipe/directx-headers-install)
endif()

include("${REACTOS_SOURCE_DIR}/submodules/mesa-cmake.cmake")
execute_process(COMMAND "${MESA_PYTHON}" -c "import mako, packaging, yaml"
    RESULT_VARIABLE _mesa_python_status ERROR_VARIABLE _mesa_python_error)
if(NOT _mesa_python_status EQUAL 0)
    message(FATAL_ERROR "Mesa needs Python mako, packaging and PyYAML modules for ${MESA_PYTHON}: ${_mesa_python_error}")
endif()
include("${REACTOS_SOURCE_DIR}/sdk/cmake/nested-build.cmake")
include(ExternalProject)

set(MESA_WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/mesa-source")
set(MESA_BINARY_DIR "${MESA_WORK_DIR}/cmake-build")
set(MESA_SDK_LIBDIR "${MESA_WORK_DIR}/reactos-sdk-lib")

set(_mesa_sdk_targets
    msvcrtex libucrtbase libmsvcrt libkernel32 libntdll ucrtoldnames libuser32 libgdi32
    libadvapi32 libole32 liboleaut32 libshell32 uuid libpsapi libws2_32 libversion libsetupapi libkernelbase libsynchronization libwinspool libcomdlg32)
if(ARM64EC_RUNTIME)
    list(APPEND _mesa_sdk_targets chpe)
endif()
export_sdk_libraries(mesa_reactos_sdk_libs "${MESA_SDK_LIBDIR}"
    LIBRARIES ${_mesa_sdk_targets}
    DEPENDS ${_mesa_sdk_targets} ucrtoldnames_target ${_mesa_support_targets})

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(_mesa_assertions OFF)
else()
    set(_mesa_assertions ON)
endif()

set(MESA_WGL_DLL "${MESA_BINARY_DIR}/src/gallium/targets/wgl/mesa_gallium.dll")
set(_mesa_build_targets mesa_gallium)
set(_mesa_build_byproducts "${MESA_WGL_DLL}")
if(_mesa_d3d_umd_name)
    set(MESA_D3D_UMD_DLL "${MESA_BINARY_DIR}/src/gallium/targets/d3d10umd/${_mesa_d3d_umd_name}.dll")
    list(APPEND _mesa_build_byproducts "${MESA_D3D_UMD_DLL}")
    if(ARCH STREQUAL "riscv64")
        list(APPEND _mesa_build_targets d3d10zink)
    else()
        list(APPEND _mesa_build_targets mesa_d3d)
    endif()
endif()
if(ARCH STREQUAL "riscv64")
    set(MESA_PVR_DLL "${MESA_BINARY_DIR}/src/imagination/vulkan/vulkan_powervr_mesa.dll")
    set(MESA_PVR_MANIFEST "${MESA_BINARY_DIR}/src/imagination/vulkan/powervr_mesa_icd.json")
    list(APPEND _mesa_build_targets vulkan_powervr_mesa powervr_icd)
    list(APPEND _mesa_build_byproducts "${MESA_PVR_DLL}" "${MESA_PVR_MANIFEST}")
endif()
if(_mesa_lavapipe_enabled)
    list(APPEND _mesa_build_targets vulkan_lvp lvp_icd)
    list(APPEND _mesa_build_byproducts
        "${MESA_BINARY_DIR}/src/gallium/targets/lavapipe/vulkan_lvp.dll"
        "${MESA_BINARY_DIR}/src/gallium/targets/lavapipe/lvp_icd.json")
endif()

ExternalProject_Add(mesa_gallium_build
    EXCLUDE_FROM_ALL TRUE
    PREFIX "${MESA_WORK_DIR}/cmake-prefix"
    SOURCE_DIR "${MESA_SOURCE_DIR}"
    BINARY_DIR "${MESA_BINARY_DIR}"
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    PATCH_COMMAND ""
    CMAKE_GENERATOR Ninja
    CMAKE_ARGS
        ${MESA_CMAKE_ARGS}
        -DREACTOS_SDK_ARCH:STRING=${_mesa_processor}
        -DREACTOS_SDK_TRIPLE:STRING=${_mesa_triple}
        "-DREACTOS_SDK_ARCH_FLAGS:STRING=${_mesa_arch_flags}"
        -DREACTOS_SDK_SOURCE_DIR:PATH=${REACTOS_SOURCE_DIR}
        -DREACTOS_SDK_BUILD_DIR:PATH=${REACTOS_BINARY_DIR}
        -DREACTOS_SDK_LIBDIR:PATH=${MESA_SDK_LIBDIR}
        -DREACTOS_SDK_CXX_RUNTIME:PATH=${REACTOS_LIBCXX_ROOT}
        -DMESA_REACTOS_SOURCE_DIR:PATH=${REACTOS_SOURCE_DIR}
        -DMESA_REACTOS_BUILD_DIR:PATH=${REACTOS_BINARY_DIR}
        -DMESA_ARCH:STRING=${_mesa_arch}
        -DMESA_WGL_DLL_NAME:STRING=mesa_gallium
        -DMESA_ASSERTIONS:BOOL=${_mesa_assertions}
        ${_mesa_features}
    BUILD_COMMAND ${CMAKE_COMMAND} -E env CCACHE_DISABLE=1 SCCACHE_DISABLE=1
        ${REACTOS_NESTED_BUILD} <BINARY_DIR> --target ${_mesa_build_targets}
    BUILD_ALWAYS TRUE
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS ${_mesa_build_byproducts}
    USES_TERMINAL_BUILD TRUE)
ExternalProject_Add_StepDependencies(mesa_gallium_build configure mesa_reactos_sdk_libs)
if(ENABLE_MESA_LLVMPIPE)
    # These producers are declared later under submodules/. Attach them here,
    # where ExternalProject's configure-step custom command is scoped.
    set(_mesa_configure_dependencies)
    if(_mesa_lavapipe_enabled)
        list(APPEND _mesa_configure_dependencies directx-headers-build)
    endif()
    if(NOT MESA_LLVM_ROOT)
        list(APPEND _mesa_configure_dependencies mesa-llvm)
    endif()
    if(_mesa_configure_dependencies)
        ExternalProject_Add_StepDependencies(mesa_gallium_build configure
            ${_mesa_configure_dependencies})
    endif()
endif()

function(mesa_stage_dll _target _dll _stage_dir)
    get_filename_component(_name "${_dll}" NAME)
    set(_stage "${_stage_dir}/${_name}")
    add_custom_command(
        OUTPUT "${_stage}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_stage_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "${_stage}"
        COMMAND ${CMAKE_STRIP} --strip-debug "${_stage}"
        COMMAND native-symz "${_stage}"
        DEPENDS mesa_gallium_build "${_dll}"
        VERBATIM)
    add_custom_target(${_target} DEPENDS "${_stage}")
    add_cd_file(FILE "${_stage}" TARGET ${_target} DESTINATION reactos/system32 OPTIONAL FOR all)
    add_dependencies(submodules ${_target})
    set_property(GLOBAL APPEND PROPERTY CI_SYSTEM_OPTIONAL_FILES "${_stage}")
    set(_mesa_stage "${_stage}" PARENT_SCOPE)
endfunction()

mesa_stage_dll(mesa_gallium "${MESA_WGL_DLL}" "${CMAKE_CURRENT_BINARY_DIR}/mesa-icd")
if(NOT ARCH STREQUAL "riscv64")
    add_optional_registry_inf(INF mesa-gallium.inf REQUIRES "${_mesa_stage}")
endif()
if(_mesa_d3d_umd_name)
    mesa_stage_dll(mesa_d3d_umd "${MESA_D3D_UMD_DLL}" "${CMAKE_CURRENT_BINARY_DIR}/mesa-d3d")
    add_dependencies(mesa_gallium mesa_d3d_umd)
endif()
if(ARCH STREQUAL "riscv64")
    mesa_stage_dll(mesa_vulkan_powervr "${MESA_PVR_DLL}" "${CMAKE_CURRENT_BINARY_DIR}/mesa-vulkan")
    set(_mesa_pvr_stage "${_mesa_stage}")
    set(_mesa_pvr_manifest "${CMAKE_CURRENT_BINARY_DIR}/mesa-vulkan/powervr_mesa_icd.json")
    add_custom_command(
        OUTPUT "${_mesa_pvr_manifest}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MESA_PVR_MANIFEST}" "${_mesa_pvr_manifest}"
        DEPENDS mesa_gallium_build "${MESA_PVR_MANIFEST}"
        VERBATIM)
    add_custom_target(mesa_vulkan_powervr_manifest DEPENDS "${_mesa_pvr_manifest}")
    add_dependencies(mesa_vulkan_powervr mesa_vulkan_powervr_manifest)
    add_cd_file(FILE "${_mesa_pvr_manifest}" TARGET mesa_vulkan_powervr_manifest
        DESTINATION reactos/system32 OPTIONAL FOR all)
    add_optional_registry_inf(INF powervr-vulkan.inf REQUIRES "${_mesa_pvr_stage}")
endif()
message(STATUS "Mesa Gallium (${_mesa_arch}): ReactOS SDK build from ${MESA_SOURCE_DIR}")
