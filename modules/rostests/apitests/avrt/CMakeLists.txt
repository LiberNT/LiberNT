
add_executable(avrt_apitest AvSetMmThreadCharacteristics.c SystemResponsiveness.c TaskProfiles.c testlist.c)
target_link_libraries(avrt_apitest ${PSEH_LIB})
set_module_type(avrt_apitest win32cui)
add_importlibs(avrt_apitest advapi32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET avrt_apitest)
