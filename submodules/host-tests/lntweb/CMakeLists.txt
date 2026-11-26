# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Web view page script host test (QuickJS, linkedom and the browser bootstrap)
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

set(QUICKJS_DIR ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/3rdparty/quickjs)
add_executable(lntweb_script
    test.c
    ${QUICKJS_DIR}/dtoa.c
    ${QUICKJS_DIR}/libregexp.c
    ${QUICKJS_DIR}/libunicode.c
    ${QUICKJS_DIR}/quickjs.c)
target_include_directories(lntweb_script PRIVATE ${QUICKJS_DIR})
target_compile_definitions(lntweb_script PRIVATE _GNU_SOURCE)
set_source_files_properties(${QUICKJS_DIR}/dtoa.c ${QUICKJS_DIR}/libregexp.c ${QUICKJS_DIR}/libunicode.c
    ${QUICKJS_DIR}/quickjs.c PROPERTIES COMPILE_OPTIONS -w)
set_source_files_properties(test.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
find_library(MATH_LIBRARY m)
if(MATH_LIBRARY)
    target_link_libraries(lntweb_script PRIVATE ${MATH_LIBRARY})
endif()
find_package(Threads REQUIRED)
target_link_libraries(lntweb_script PRIVATE Threads::Threads)
add_test(NAME lntweb.script COMMAND lntweb_script
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/3rdparty/linkedom/linkedom.js
    ${REACTOS_HOST_SOURCE_ROOT}/dll/win32/lntweb/bootstrap.js)
