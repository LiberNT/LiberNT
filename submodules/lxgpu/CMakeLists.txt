# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif.ing@outlook.com>
# Nested build of the GPU renderer of the Linux subsystem with the LiberNT SDK: libepoxy and virglrenderer
# (MIT, upstream release sources; virglrenderer carries virglrenderer-windows.patch) as static libraries
# linked into lxgpu.exe.

cmake_minimum_required(VERSION 3.24)
project(lxgpu C)

foreach(_required EPOXY_SOURCE_DIR VIRGL_SOURCE_DIR LXGPU_SOURCE_DIR LXGPU_INCLUDE_DIR LXGPU_PYTHON LXGPU_CPU)
    if(NOT ${_required})
        message(FATAL_ERROR "${_required} must be set")
    endif()
endforeach()

set(_gen "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${_gen}/epoxy/include/epoxy" "${_gen}/epoxy/src" "${_gen}/virgl")

file(WRITE "${_gen}/epoxy/src/config.h"
    "#pragma once\n"
    "#define ENABLE_EGL 0\n"
    "#define ENABLE_GLX 0\n"
    "#define ENABLE_X11 0\n"
    "#define EPOXY_PUBLIC extern\n"
    "#define HAVE_KHRPLATFORM_H\n")

set(_epoxy_generated)
foreach(_api gl wgl)
    add_custom_command(
        OUTPUT "${_gen}/epoxy/include/epoxy/${_api}_generated.h"
        COMMAND "${LXGPU_PYTHON}" "${EPOXY_SOURCE_DIR}/src/gen_dispatch.py" --header --no-source
                "--outputdir=${_gen}/epoxy/include/epoxy" "${EPOXY_SOURCE_DIR}/registry/${_api}.xml"
        DEPENDS "${EPOXY_SOURCE_DIR}/src/gen_dispatch.py" "${EPOXY_SOURCE_DIR}/registry/${_api}.xml"
        VERBATIM)
    add_custom_command(
        OUTPUT "${_gen}/epoxy/src/${_api}_generated_dispatch.c"
        COMMAND "${LXGPU_PYTHON}" "${EPOXY_SOURCE_DIR}/src/gen_dispatch.py" --source --no-header
                "--outputdir=${_gen}/epoxy/src" "${EPOXY_SOURCE_DIR}/registry/${_api}.xml"
        DEPENDS "${EPOXY_SOURCE_DIR}/src/gen_dispatch.py" "${EPOXY_SOURCE_DIR}/registry/${_api}.xml"
        VERBATIM)
    list(APPEND _epoxy_generated "${_gen}/epoxy/include/epoxy/${_api}_generated.h"
                                 "${_gen}/epoxy/src/${_api}_generated_dispatch.c")
endforeach()

add_library(epoxy STATIC
    "${EPOXY_SOURCE_DIR}/src/dispatch_common.c"
    "${EPOXY_SOURCE_DIR}/src/dispatch_wgl.c"
    ${_epoxy_generated})
target_include_directories(epoxy
    PUBLIC "${EPOXY_SOURCE_DIR}/include" "${_gen}/epoxy/include"
    PRIVATE "${_gen}/epoxy/include/epoxy" "${EPOXY_SOURCE_DIR}/src" "${_gen}/epoxy/src")
target_compile_options(epoxy PRIVATE -std=gnu99 -O2 -fno-strict-aliasing -Wno-int-conversion)
set_source_files_properties("${EPOXY_SOURCE_DIR}/src/dispatch_wgl.c" PROPERTIES COMPILE_DEFINITIONS DllMain=epoxy_dll_main)

if(LXGPU_CPU STREQUAL "x86_64")
    set(_pipe_arch "#define PIPE_ARCH_X86_64\n")
elseif(LXGPU_CPU STREQUAL "aarch64")
    set(_pipe_arch "#define PIPE_ARCH_AARCH64\n")
else()
    set(_pipe_arch "")
endif()
file(WRITE "${_gen}/virgl/config.h"
    "#define VERSION \"1.1.1\"\n"
    "#define VIRGL_RENDERER_UNSTABLE_APIS 1\n"
    "#define HAVE___BUILTIN_BSWAP32 1\n"
    "#define HAVE___BUILTIN_BSWAP64 1\n"
    "#define HAVE___BUILTIN_CLZ 1\n"
    "#define HAVE___BUILTIN_CLZLL 1\n"
    "#define HAVE___BUILTIN_EXPECT 1\n"
    "#define HAVE___BUILTIN_FFS 1\n"
    "#define HAVE___BUILTIN_FFSLL 1\n"
    "#define HAVE___BUILTIN_POPCOUNT 1\n"
    "#define HAVE___BUILTIN_POPCOUNTLL 1\n"
    "#define HAVE___BUILTIN_TYPES_COMPATIBLE_P 1\n"
    "#define HAVE___BUILTIN_UNREACHABLE 1\n"
    "#define HAVE_FUNC_ATTRIBUTE_CONST 1\n"
    "#define HAVE_FUNC_ATTRIBUTE_FLATTEN 1\n"
    "#define HAVE_FUNC_ATTRIBUTE_FORMAT 1\n"
    "#define HAVE_FUNC_ATTRIBUTE_MALLOC 1\n"
    "#define HAVE_FUNC_ATTRIBUTE_NORETURN 1\n"
    "#define HAVE_FUNC_ATTRIBUTE_PACKED 1\n"
    "#define HAVE_FUNC_ATTRIBUTE_PURE 1\n"
    "#define HAVE_FUNC_ATTRIBUTE_RETURNS_NONNULL 1\n"
    "#define HAVE_FUNC_ATTRIBUTE_UNUSED 1\n"
    "#define HAVE_FUNC_ATTRIBUTE_WARN_UNUSED_RESULT 1\n"
    "#define HAVE_FUNC_ATTRIBUTE_WEAK 1\n"
    "#define HAVE_TIMESPEC_GET 1\n"
    "#define ENABLE_VENUS 1\n"
    "#define CHECK_GL_ERRORS 1\n"
    "#define UTIL_ARCH_LITTLE_ENDIAN 1\n"
    "#define UTIL_ARCH_BIG_ENDIAN 0\n"
    "${_pipe_arch}")

set(VIRGL_MAJOR_VERSION 1)
set(VIRGL_MINOR_VERSION 1)
set(VIRGL_MICRO_VERSION 1)
configure_file("${VIRGL_SOURCE_DIR}/src/virgl-version.h.meson" "${_gen}/virgl/virgl-version.h" @ONLY)

add_custom_command(
    OUTPUT "${_gen}/virgl/u_format_table.c"
    COMMAND "${LXGPU_PYTHON}" -B "${VIRGL_SOURCE_DIR}/src/gallium/auxiliary/util/u_format_table.py"
            "${VIRGL_SOURCE_DIR}/src/gallium/auxiliary/util/u_format.yaml" > "${_gen}/virgl/u_format_table.c"
    DEPENDS "${VIRGL_SOURCE_DIR}/src/gallium/auxiliary/util/u_format_table.py"
            "${VIRGL_SOURCE_DIR}/src/gallium/auxiliary/util/u_format_parse.py"
            "${VIRGL_SOURCE_DIR}/src/gallium/auxiliary/util/u_format.yaml")

set(_virgl "${VIRGL_SOURCE_DIR}/src")
set(_venus_generated)
foreach(_object buffer command_buffer descriptor_set device_memory image pipeline query_pool queue render_pass)
    list(APPEND _venus_generated "${_gen}/venus/vkr_${_object}_gen.h")
endforeach()
add_custom_command(
    OUTPUT ${_venus_generated}
    COMMAND "${LXGPU_PYTHON}" "${_virgl}/venus/vkr_device_object.py" -o "${_gen}/venus" "${_virgl}/venus/vkr_device_object.json"
    DEPENDS "${_virgl}/venus/vkr_device_object.py" "${_virgl}/venus/vkr_device_object.json"
    VERBATIM)
set(_venus_sources)
foreach(_file acceleration_structure allocator buffer command_buffer common context cs descriptor_set device
              device_memory host_copy image instance physical_device pipeline query_pool queue render_pass renderer
              ring transport)
    list(APPEND _venus_sources "${_virgl}/venus/vkr_${_file}.c")
endforeach()

add_library(virglrenderer STATIC
    ${_venus_sources}
    ${_venus_generated}
    "${_virgl}/gallium/auxiliary/cso_cache/cso_cache.c"
    "${_virgl}/gallium/auxiliary/cso_cache/cso_hash.c"
    "${_virgl}/gallium/auxiliary/tgsi/tgsi_build.c"
    "${_virgl}/gallium/auxiliary/tgsi/tgsi_dump.c"
    "${_virgl}/gallium/auxiliary/tgsi/tgsi_info.c"
    "${_virgl}/gallium/auxiliary/tgsi/tgsi_iterate.c"
    "${_virgl}/gallium/auxiliary/tgsi/tgsi_parse.c"
    "${_virgl}/gallium/auxiliary/tgsi/tgsi_sanity.c"
    "${_virgl}/gallium/auxiliary/tgsi/tgsi_scan.c"
    "${_virgl}/gallium/auxiliary/tgsi/tgsi_strings.c"
    "${_virgl}/gallium/auxiliary/tgsi/tgsi_text.c"
    "${_virgl}/gallium/auxiliary/tgsi/tgsi_util.c"
    "${_virgl}/gallium/auxiliary/util/u_debug_describe.c"
    "${_virgl}/gallium/auxiliary/util/u_format.c"
    "${_virgl}/gallium/auxiliary/util/u_hash_table.c"
    "${_virgl}/gallium/auxiliary/util/u_texture.c"
    "${_gen}/virgl/u_format_table.c"
    "${_virgl}/iov.c"
    "${_virgl}/mesa/util/bitscan.c"
    "${_virgl}/mesa/util/hash_table.c"
    "${_virgl}/mesa/util/os_file.c"
    "${_virgl}/mesa/util/os_misc.c"
    "${_virgl}/mesa/util/ralloc.c"
    "${_virgl}/mesa/util/u_cpu_detect.c"
    "${_virgl}/mesa/util/u_debug.c"
    "${_virgl}/mesa/util/u_math.c"
    "${_virgl}/mman_win32.c"
    "${_virgl}/virgl_context.c"
    "${_virgl}/virgl_fence.c"
    "${_virgl}/virgl_resource.c"
    "${_virgl}/virgl_util.c"
    "${_virgl}/virglrenderer.c"
    "${_virgl}/vrend_blitter.c"
    "${_virgl}/vrend_debug.c"
    "${_virgl}/vrend_decode.c"
    "${_virgl}/vrend_formats.c"
    "${_virgl}/vrend_object.c"
    "${_virgl}/vrend_renderer.c"
    "${_virgl}/vrend_shader.c"
    "${_virgl}/vrend_tweaks.c"
    "${_virgl}/vrend_winsys.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat.c")
target_include_directories(virglrenderer
    PUBLIC "${_virgl}" "${_gen}/virgl"
    PRIVATE "${_virgl}/gallium/include" "${_virgl}/gallium/auxiliary" "${_virgl}/gallium/auxiliary/util"
            "${VIRGL_SOURCE_DIR}" "${_virgl}/venus" "${_virgl}/drm" "${_virgl}/drm/drm-uapi" "${_virgl}/mesa"
            "${_virgl}/mesa/compat" "${_virgl}/mesa/pipe" "${_virgl}/mesa/util" "${_virgl}/venus/venus-protocol"
            "${_gen}/venus")
target_compile_options(virglrenderer PRIVATE -std=gnu11 -O2 -fno-strict-aliasing -Wno-deprecated-declarations
                                             -imacros "${_gen}/virgl/config.h"
                                             -include "${CMAKE_CURRENT_SOURCE_DIR}/compat.h")
target_compile_definitions(virglrenderer PRIVATE NDEBUG HAVE_CONFIG_H=1)
target_link_libraries(virglrenderer PUBLIC epoxy)

add_executable(lxgpu "${LXGPU_SOURCE_DIR}/lxgpu.c")
target_include_directories(lxgpu PRIVATE "${LXGPU_INCLUDE_DIR}" "${_virgl}/venus")
target_compile_options(lxgpu PRIVATE -O2)
target_link_libraries(lxgpu PRIVATE virglrenderer epoxy opengl32 winmm shell32 user32 gdi32)
target_link_options(lxgpu PRIVATE -Wl,--subsystem,windows)
install(TARGETS lxgpu RUNTIME DESTINATION .)
