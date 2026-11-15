
add_definitions(-D__WINESRC__)
spec2def(adsnt.dll adsnt.spec)

add_library(adsnt MODULE
    adsnt.c
    adsnt.rc
    ${CMAKE_CURRENT_BINARY_DIR}/adsnt.def)

set_module_type(adsnt win32dll)
target_link_libraries(adsnt uuid wine wine_dll_canunload wine_dll_register)
add_importlibs(adsnt netapi32 oleaut32 ole32 advapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET adsnt DESTINATION reactos/system32 FOR all)
