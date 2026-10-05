
add_executable(dbglog dbglog.c dbglog.rc)
set_module_type(dbglog win32cui UNICODE)
add_importlibs(dbglog advapi32 msvcrt kernel32)
add_cd_file(TARGET dbglog DESTINATION reactos/system32 FOR all)
