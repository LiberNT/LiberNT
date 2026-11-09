
add_executable(msi_apitest installend.c reboot.c testlist.c)
set_module_type(msi_apitest win32cui)
add_importlibs(msi_apitest msi advapi32 user32 msvcrt kernel32)
add_rostests_file(TARGET msi_apitest)
