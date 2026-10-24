# PROJECT:     LiberNT build system
# LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
# PURPOSE:     Turn MSVC /sourceDependencies output into a Ninja depfile
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

file(READ "${JSON}" _json)
string(JSON _source GET "${_json}" Data Source)
string(JSON _count LENGTH "${_json}" Data Includes)
set(_deps "${_source}")
if(_count GREATER 0)
    math(EXPR _last "${_count} - 1")
    foreach(_index RANGE ${_last})
        string(JSON _include GET "${_json}" Data Includes ${_index})
        list(APPEND _deps "${_include}")
    endforeach()
endif()

file(TO_CMAKE_PATH "${OUTPUT}" _target)
string(REPLACE " " "\\ " _target "${_target}")
set(_text "${_target}:")
foreach(_dep IN LISTS _deps)
    file(TO_CMAKE_PATH "${_dep}" _dep)
    string(REPLACE " " "\\ " _dep "${_dep}")
    string(REPLACE "#" "\\#" _dep "${_dep}")
    string(APPEND _text " \\\n  ${_dep}")
endforeach()
file(WRITE "${DEPFILE}" "${_text}\n")
