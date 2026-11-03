# PROJECT:     LiberNT Build System
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Compile the LLVM bitcode of a Rust static library into COFF objects
# COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
execute_process(COMMAND "${AR}" x "${INPUT}" WORKING_DIRECTORY "${WORK_DIR}" RESULT_VARIABLE _result)
if(_result)
    message(FATAL_ERROR "Cannot extract ${INPUT}")
endif()

function(replace_line _variable _prefix _value)
    string(FIND "${${_variable}}" "\n${_prefix}\"" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR "No '${_prefix}' line in the IR")
    endif()
    string(LENGTH "\n${_prefix}\"" _length)
    math(EXPR _value_start "${_start} + ${_length}")
    string(SUBSTRING "${${_variable}}" ${_value_start} -1 _rest)
    string(FIND "${_rest}" "\"" _value_length)
    string(SUBSTRING "${${_variable}}" 0 ${_value_start} _head)
    string(SUBSTRING "${_rest}" ${_value_length} -1 _tail)
    set(${_variable} "${_head}${_value}${_tail}" PARENT_SCOPE)
endfunction()

separate_arguments(_flags UNIX_COMMAND "${COFF_FLAGS}")
file(GLOB _members "${WORK_DIR}/*")
set(_objects)
foreach(_member IN LISTS _members)
    file(READ "${_member}" _magic LIMIT 4 HEX)
    if(NOT _magic STREQUAL "4243c0de")
        continue()
    endif()
    execute_process(
        COMMAND "${CLANG}" --target=${IR_TRIPLE} -Wno-override-module -x ir -S -emit-llvm "${_member}" -o "${_member}.ll"
        RESULT_VARIABLE _result)
    if(_result)
        message(FATAL_ERROR "Cannot read the bitcode of ${_member}")
    endif()
    file(READ "${_member}.ll" _ir)
    replace_line(_ir "target datalayout = " "${COFF_LAYOUT}")
    replace_line(_ir "target triple = " "${COFF_TRIPLE}")
    file(WRITE "${_member}.ll" "${_ir}")
    execute_process(
        COMMAND "${CLANG}" --target=${COFF_TRIPLE} ${_flags} -O2 -ffunction-sections -fdata-sections -Wno-override-module -c -x ir "${_member}.ll" -o "${_member}.obj"
        RESULT_VARIABLE _result)
    if(_result)
        message(FATAL_ERROR "Cannot compile ${_member}")
    endif()
    list(APPEND _objects "${_member}.obj")
endforeach()

file(REMOVE "${OUTPUT}")
execute_process(COMMAND "${AR}" rcs "${OUTPUT}" ${_objects} RESULT_VARIABLE _result)
if(_result)
    message(FATAL_ERROR "Cannot create ${OUTPUT}")
endif()
