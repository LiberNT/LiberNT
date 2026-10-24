
add_executable(lxterm lxterm.c lxterm.rc)
set_module_type(lxterm win32cui UNICODE)
target_link_libraries(lxterm lxlaunch)
add_importlibs(lxterm advapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET lxterm DESTINATION reactos/system32 FOR all)
