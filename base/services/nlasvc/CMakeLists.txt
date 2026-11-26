
add_executable(nlasvc nlasvc.c probe.c nlasvc.rc)
set_module_type(nlasvc win32cui UNICODE)
add_importlibs(nlasvc ws2_32 iphlpapi ole32 advapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET nlasvc DESTINATION reactos/system32 FOR all)
