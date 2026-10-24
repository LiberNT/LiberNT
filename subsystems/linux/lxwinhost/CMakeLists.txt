
add_executable(lxwinhost lxwinhost.c iconcache.c lxwinhost.rc)
set_module_type(lxwinhost win32gui UNICODE)
add_importlibs(lxwinhost user32 gdi32 shell32 advapi32 bcrypt msvcrt kernel32 ntdll)
add_cd_file(TARGET lxwinhost DESTINATION reactos/system32 FOR all)
