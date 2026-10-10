spec2def(netsetupshim.dll netsetupshim.spec)

list(APPEND SOURCE
    netsetupshim.c
    ${CMAKE_CURRENT_BINARY_DIR}/netsetupshim.def)

add_library(netsetupshim MODULE ${SOURCE})
set_module_type(netsetupshim win32dll UNICODE)
target_link_libraries(netsetupshim uuid)
add_importlibs(netsetupshim msvcrt kernel32 ntdll)
add_cd_file(TARGET netsetupshim DESTINATION reactos/system32 FOR all)
