
include_directories(${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/sqlite)

list(APPEND SOURCE
    sqlite.c
    testlist.c)

add_executable(winsqlite3_apitest ${SOURCE})
set_module_type(winsqlite3_apitest win32cui)
if(ARCH STREQUAL "i386")
    # call the API and pass callbacks as __stdcall, like a caller built with the SDK winsqlite3.h
    if(MSVC)
        target_compile_options(winsqlite3_apitest PRIVATE /Gz)
    else()
        target_compile_options(winsqlite3_apitest PRIVATE -mrtd)
    endif()
endif()
add_importlibs(winsqlite3_apitest winsqlite3 msvcrt kernel32)
add_rostests_file(TARGET winsqlite3_apitest)
