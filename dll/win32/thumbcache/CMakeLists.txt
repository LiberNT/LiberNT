
spec2def(thumbcache.dll thumbcache.spec)

list(APPEND SOURCE
    thumbcache.c
    ${CMAKE_CURRENT_BINARY_DIR}/thumbcache.def)

add_library(thumbcache MODULE ${SOURCE})
add_idl_reg_scripts(thumbcache registry classes.idl)
set_module_type(thumbcache win32dll)
target_link_libraries(thumbcache uuid wine wine_dll_register)
add_importlibs(thumbcache ole32 user32 gdi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET thumbcache DESTINATION reactos/system32 FOR all)
