
spec2def(winsqlite3.dll winsqlite3.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    ${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/sqlite/sqlite3.c
    ${CMAKE_CURRENT_BINARY_DIR}/winsqlite3.def)

add_library(winsqlite3 MODULE ${SOURCE})
set_module_type(winsqlite3 win32dll ENTRYPOINT 0)
target_compile_definitions(winsqlite3 PRIVATE
    SQLITE_OS_WIN=1
    SQLITE_ENABLE_COLUMN_METADATA
    SQLITE_ENABLE_RTREE)
if(NOT MSVC)
    target_compile_options(winsqlite3 PRIVATE -w)
endif()
if(ARCH STREQUAL "i386")
    # the SDK header makes every non-variadic API function, callback and VFS method __stdcall
    if(MSVC)
        target_compile_options(winsqlite3 PRIVATE /Gz)
    else()
        target_compile_options(winsqlite3 PRIVATE -mrtd)
    endif()
endif()
add_importlibs(winsqlite3 msvcrt kernel32 ntdll)
add_cd_file(TARGET winsqlite3 DESTINATION reactos/system32 FOR all)
