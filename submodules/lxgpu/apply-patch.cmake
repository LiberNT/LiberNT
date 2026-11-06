# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif.ing@outlook.com>

foreach(_required PATCH_EXECUTABLE SOURCE_DIR PATCH_FILE)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "Missing ${_required} for the virglrenderer patch step")
    endif()
endforeach()

if(EXISTS "${SOURCE_DIR}/src/venus/vkr_os.h")
    return()
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
