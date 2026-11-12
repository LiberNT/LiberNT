
spec2def(loaderwait.dll loaderwait.spec)

add_library(loaderwait MODULE loaderwait.c ${CMAKE_CURRENT_BINARY_DIR}/loaderwait.def)
set_module_type(loaderwait win32dll)
add_importlibs(loaderwait msvcrt kernel32 ntdll)
add_rostests_file(TARGET loaderwait)
