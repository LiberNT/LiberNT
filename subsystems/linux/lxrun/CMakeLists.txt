
add_executable(lxrun lxrun.c lxrun.rc)
set_module_type(lxrun win32cui UNICODE)
add_importlibs(lxrun advapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET lxrun DESTINATION reactos/system32 FOR all)
