
add_library(lxlaunch STATIC lxlaunch.c)
add_dependencies(lxlaunch psdk)
add_importlibs(lxlaunch advapi32 msvcrt kernel32 ntdll)
