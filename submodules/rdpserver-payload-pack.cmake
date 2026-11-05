# PROJECT:     LiberNT Build System
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Pack the OpenSSL and FreeRDP libraries of a source build into an RDP server payload
# COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>

foreach(_variable OUTPUT ARCH COMPILER AR LIBRARIES INCLUDES LICENSES)
    if(NOT DEFINED ${_variable})
        message(FATAL_ERROR "rdpserver-payload-pack.cmake needs -D${_variable}=")
    endif()
endforeach()

get_filename_component(_output "${OUTPUT}" ABSOLUTE)
set(_stage "${_output}.stage")
file(REMOVE_RECURSE "${_stage}")
file(MAKE_DIRECTORY "${_stage}/include" "${_stage}/lib" "${_stage}/licenses")

string(REPLACE "|" ";" _includes "${INCLUDES}")
list(REMOVE_DUPLICATES _includes)
foreach(_include ${_includes})
    file(COPY "${_include}/" DESTINATION "${_stage}/include" FILES_MATCHING PATTERN "*.h")
endforeach()

string(REPLACE "|" ";" _libraries "${LIBRARIES}")
set(_names)
foreach(_library ${_libraries})
    string(REPLACE "=" ";" _library "${_library}")
    list(GET _library 0 _name)
    list(GET _library 1 _file)
    execute_process(
        COMMAND "${AR}" qcL "${_stage}/lib/lib${_name}.a" "${_file}"
        RESULT_VARIABLE _result
        ERROR_VARIABLE _error)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "Packing ${_file} failed: ${_error}")
    endif()
    list(APPEND _names "\"${_name}\"")
endforeach()

string(REPLACE "|" ";" _licenses "${LICENSES}")
foreach(_license ${_licenses})
    string(REPLACE "=" ";" _license "${_license}")
    list(GET _license 0 _name)
    list(GET _license 1 _file)
    configure_file("${_file}" "${_stage}/licenses/${_name}" COPYONLY)
endforeach()

string(JOIN ", " _names ${_names})
file(WRITE "${_stage}/payload.json"
    "{\n  \"name\": \"libernt-rdpserver\",\n  \"arch\": \"${ARCH}\",\n  \"compiler\": \"${COMPILER}\",\n"
    "  \"libraries\": [${_names}]\n}\n")

file(REMOVE "${_output}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${_output}" --format=zip include lib licenses payload.json
    WORKING_DIRECTORY "${_stage}"
    RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Writing ${_output} failed")
endif()
file(REMOVE_RECURSE "${_stage}")
