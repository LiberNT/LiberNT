##
## PROJECT:     LiberNT Boot Loader
## LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
## PURPOSE:     Replace the boot files of a live medium file list and pack the system into a WIM
## COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
##

file(STRINGS "${LIST}" _entries)
set(_output "")

if(WIM)
    file(REMOVE_RECURSE "${STAGE}")
    file(MAKE_DIRECTORY "${STAGE}")
endif()

foreach(_entry IN LISTS _entries)
    string(TOLOWER "${_entry}" _lower)
    if(_lower MATCHES "^/?(loader/|efi/|freeldr\\.ini=)")
        continue()
    endif()

    if(NOT WIM OR _lower MATCHES "^/?(autorun\\.inf|icon\\.ico|readme\\.txt)=" OR NOT _entry MATCHES "^/?([^=]+)=(.*)$")
        string(APPEND _output "${_entry}\n")
        continue()
    endif()

    set(_destination "${STAGE}/${CMAKE_MATCH_1}")
    set(_source "${CMAKE_MATCH_2}")
    if(IS_DIRECTORY "${_source}")
        file(MAKE_DIRECTORY "${_destination}")
    elseif(EXISTS "${_source}")
        get_filename_component(_parent "${_destination}" DIRECTORY)
        file(MAKE_DIRECTORY "${_parent}")
        file(CREATE_LINK "${_source}" "${_destination}" COPY_ON_ERROR)
    endif()
endforeach()

if(WIM)
    include(ProcessorCount)
    ProcessorCount(_threads)
    if(_threads LESS 1)
        set(_threads 1)
    endif()

    execute_process(
        COMMAND "${WIMAGE}" capture "${STAGE}" "${WIM}" "LiberNT" --compress=xpress --boot --threads=${_threads}
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _log
        ERROR_VARIABLE _log)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "The system image could not be captured:\n${_log}")
    endif()

    file(REMOVE_RECURSE "${STAGE}")
    string(APPEND _output "boot.wim=${WIM}\n")
endif()

string(APPEND _output "efi/efisys.bin=${EFISYS}\n")
file(WRITE "${LIST}" "${_output}")
