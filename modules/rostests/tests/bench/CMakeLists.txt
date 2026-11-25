
add_executable(wsperf wsperf.c)
set_module_type(wsperf win32cui)
add_importlibs(wsperf ws2_32 msvcrt kernel32)

add_executable(benchdisk benchdisk.c)
set_module_type(benchdisk win32cui)
add_importlibs(benchdisk msvcrt kernel32)
