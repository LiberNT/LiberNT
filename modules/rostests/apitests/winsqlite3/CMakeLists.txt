
include_directories(${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/sqlite)

list(APPEND SOURCE
    sqlite.c
    testlist.c)

add_executable(winsqlite3_apitest ${SOURCE})
set_module_type(winsqlite3_apitest win32cui)
add_importlibs(winsqlite3_apitest winsqlite3 msvcrt kernel32)
add_rostests_file(TARGET winsqlite3_apitest)
