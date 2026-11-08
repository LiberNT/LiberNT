
spec2def(difxapi.dll difxapi.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    difxapi.c
    ${CMAKE_CURRENT_BINARY_DIR}/difxapi_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/difxapi.def)

add_library(difxapi MODULE ${SOURCE} difxapi.rc)
set_module_type(difxapi win32dll UNICODE)
target_link_libraries(difxapi wine)
add_delay_importlibs(difxapi newdev)
add_importlibs(difxapi setupapi wintrust crypt32 advapi32 msvcrt kernel32 ntdll)
if(ARCH STREQUAL "i386" OR ARCH STREQUAL "amd64")
    add_cd_file(TARGET difxapi DESTINATION reactos/system32 FOR all)
endif()
