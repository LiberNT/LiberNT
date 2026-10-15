
add_executable(wininit wininit.c wininit.rc)
target_link_libraries(wininit wine)
set_module_type(wininit win32gui UNICODE)
add_importlibs(wininit user32 msvcrt kernel32 ntdll)
add_cd_file(TARGET wininit DESTINATION reactos/system32 FOR all)
