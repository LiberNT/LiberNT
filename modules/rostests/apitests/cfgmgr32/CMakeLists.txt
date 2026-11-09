add_executable(cfgmgr32_apitest SwDevice.c testlist.c)
set_module_type(cfgmgr32_apitest win32cui)
add_importlibs(cfgmgr32_apitest cfgmgr32 advapi32 msvcrt kernel32)
add_rostests_file(TARGET cfgmgr32_apitest)
