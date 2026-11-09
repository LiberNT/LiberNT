add_executable(nci_apitest NciConnectionName.c testlist.c)
set_module_type(nci_apitest win32cui)
add_importlibs(nci_apitest iphlpapi ole32 msvcrt kernel32)
add_rostests_file(TARGET nci_apitest)
