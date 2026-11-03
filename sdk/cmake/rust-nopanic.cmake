# PROJECT:     LiberNT Build System
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Fail the build when a Rust static library can still reach a panic
# COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>

execute_process(
    COMMAND "${NM}" -A -u "${LIBRARY}"
    OUTPUT_VARIABLE _symbols
    RESULT_VARIABLE _result)
if(_result)
    message(FATAL_ERROR "Cannot list the symbols of ${LIBRARY}")
endif()

string(REPLACE "\n" ";" _symbols "${_symbols}")
set(_panics)
foreach(_line IN LISTS _symbols)
    if(_line MATCHES ":${CRATE}-[^:]*:.*(panic|_fail)")
        list(APPEND _panics "${_line}")
    endif()
endforeach()

if(_panics)
    list(JOIN _panics "\n" _panics)
    message(FATAL_ERROR "${CRATE} can panic; return an error instead:\n${_panics}")
endif()

file(TOUCH "${STAMP}")
