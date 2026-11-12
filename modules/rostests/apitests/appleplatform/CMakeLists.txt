
add_executable(appleplatform_apitest AppleCpufreq.c AppleFpwm.c AppleLinkPower.c testlist.c)
set_module_type(appleplatform_apitest win32cui)
add_importlibs(appleplatform_apitest msvcrt kernel32 ntdll)
add_rostests_file(TARGET appleplatform_apitest)
