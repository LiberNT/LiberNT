
add_library(lxlaunch STATIC lxlaunch.c resolv.c)
add_dependencies(lxlaunch psdk)
add_importlibs(lxlaunch advapi32 iphlpapi msvcrt kernel32 ntdll)
