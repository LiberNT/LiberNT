
spec2def(kernel32_werhandler.dll werhandler.spec)

add_library(kernel32_werhandler MODULE
    werhandler.c
    ${CMAKE_CURRENT_BINARY_DIR}/kernel32_werhandler.def)
set_module_type(kernel32_werhandler win32dll ENTRYPOINT DllMain 12)
add_importlibs(kernel32_werhandler msvcrt kernel32 ntdll)
add_dependencies(kernel32_werhandler psdk)
add_rostests_file(TARGET kernel32_werhandler)
