# PROJECT:     LiberNT Build System
# PURPOSE:     Format the RAM disk system partition at the size of its payload plus the requested free space
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

foreach(_var NTFSIMG LIST PARTITION HIDDEN_SECTORS SECTORS_PER_CLUSTER FREE_MB LABEL)
    if(NOT DEFINED ${_var})
        message(FATAL_ERROR "format_ram_partition.cmake: ${_var} is not set")
    endif()
endforeach()

math(EXPR _cluster "${SECTORS_PER_CLUSTER} * 512")
set(_bytes 0)
set(_dirs)
file(STRINGS "${LIST}" _entries)
foreach(_entry IN LISTS _entries)
    string(FIND "${_entry}" "=" _split)
    if(_split LESS 1)
        continue()
    endif()
    string(SUBSTRING "${_entry}" 0 ${_split} _target)
    math(EXPR _source_start "${_split} + 1")
    string(SUBSTRING "${_entry}" ${_source_start} -1 _source)
    set(_size 0)
    if(EXISTS "${_source}" AND NOT IS_DIRECTORY "${_source}")
        file(SIZE "${_source}" _size)
    endif()
    math(EXPR _bytes "${_bytes} + (${_size} + ${_cluster} - 1) / ${_cluster} * ${_cluster} + 1024")
    get_filename_component(_dir "${_target}" DIRECTORY)
    list(APPEND _dirs "${_dir}")
endforeach()
list(REMOVE_DUPLICATES _dirs)
list(LENGTH _dirs _dir_count)

math(EXPR _mb "(${_bytes} + ${_dir_count} * 8192) / 1048576 + 64 + ${FREE_MB}")
math(EXPR _sectors "${_mb} * 2048")
message(STATUS "RAM disk system partition: ${_mb} MB")

execute_process(
    COMMAND "${NTFSIMG}" --format "${PARTITION}" ${_sectors} ${HIDDEN_SECTORS} ${SECTORS_PER_CLUSTER} "${LABEL}"
    RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "ntfsimg --format failed for ${PARTITION}")
endif()
