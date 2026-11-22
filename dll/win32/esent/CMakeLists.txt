
spec2def(esent.dll esent.spec)

list(APPEND SOURCE
    esent_main.c
    ${CMAKE_CURRENT_BINARY_DIR}/esent_stubs.c)

add_library(esent MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/esent.def)

set_module_type(esent win32dll)
target_link_libraries(esent wine)
add_importlibs(esent msvcrt kernel32 ntdll)
add_cd_file(TARGET esent DESTINATION reactos/system32 FOR all)
