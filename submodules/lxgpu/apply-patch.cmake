# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif.ing@outlook.com>

foreach(_required PATCH_EXECUTABLE SOURCE_DIR PATCH_FILE PATCH_SHA256)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "Missing ${_required} for the virglrenderer patch step")
    endif()
endforeach()

set(_applied "${SOURCE_DIR}/.lxgpu-windows.patch")
if(EXISTS "${_applied}")
    file(SHA256 "${_applied}" _applied_sha256)
    if(_applied_sha256 STREQUAL PATCH_SHA256)
        return()
    endif()
    execute_process(
        COMMAND "${PATCH_EXECUTABLE}" -p1 -R -i "${_applied}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE _revert_result
        OUTPUT_VARIABLE _revert_output
        ERROR_VARIABLE _revert_error)
    if(NOT _revert_result EQUAL 0)
        message(FATAL_ERROR "Failed to revert the previous virglrenderer Windows patch: ${_revert_output}${_revert_error}")
    endif()
    file(REMOVE "${_applied}")
endif()

execute_process(
    COMMAND "${PATCH_EXECUTABLE}" -p1 -i "${PATCH_FILE}"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE _apply_result
    OUTPUT_VARIABLE _apply_output
    ERROR_VARIABLE _apply_error)
if(NOT _apply_result EQUAL 0)
    message(FATAL_ERROR "Failed to apply the virglrenderer Windows patch: ${_apply_output}${_apply_error}")
endif()
file(COPY_FILE "${PATCH_FILE}" "${_applied}")
