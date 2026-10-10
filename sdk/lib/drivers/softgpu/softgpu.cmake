#
# PROJECT:     LiberNT software GPU engine
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the WDDM software GPU engine for a display miniport
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

set(SOFTGPU_ENGINE_DIR ${CMAKE_CURRENT_LIST_DIR})
file(RELATIVE_PATH SOFTGPU_ENGINE_SPEC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_LIST_DIR}/softgpu.spec)

function(add_softgpu_engine _target)
    cmake_parse_arguments(_engine "" "" "DEFINITIONS" ${ARGN})

    set(_source
        ${SOFTGPU_ENGINE_DIR}/softgpu.c
        ${SOFTGPU_ENGINE_DIR}/platform_common.c
        ${SOFTGPU_ENGINE_DIR}/vidpn.c
        ${SOFTGPU_ENGINE_DIR}/pointer.c
        ${SOFTGPU_ENGINE_DIR}/dma.c
        ${SOFTGPU_ENGINE_DIR}/wddm2.c)

    add_library(${_target} STATIC ${_source})
    target_include_directories(${_target} PUBLIC ${SOFTGPU_ENGINE_DIR})
    target_compile_definitions(${_target} PUBLIC
        ${_engine_DEFINITIONS}
        REACTOS_WDDM_TARGET_LEVEL=${REACTOS_WDDM_TARGET_LEVEL}
        REACTOS_WDDM_TARGET_INTERFACE_VERSION=${REACTOS_WDDM_TARGET_INTERFACE_VERSION})
    add_pch(${_target} ${SOFTGPU_ENGINE_DIR}/softgpu.h "${_source}")
    add_dependencies(${_target} bugcodes xdk)
endfunction()
