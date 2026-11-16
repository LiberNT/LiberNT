
add_executable(kmtwow64 EXCLUDE_FROM_ALL kmtwow64.c)
set_module_type(kmtwow64 win32cui UNICODE)
add_importlibs(kmtwow64 msvcrt kernel32)
