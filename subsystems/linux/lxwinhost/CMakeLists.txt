
add_executable(lxwinhost lxwinhost.c lxwinhost.rc)
set_module_type(lxwinhost win32gui UNICODE)
add_importlibs(lxwinhost user32 gdi32 advapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET lxwinhost DESTINATION reactos/system32 FOR all)
