
add_executable(lodctr lodctr_main.c manifest.c)
set_module_type(lodctr win32cui UNICODE)
target_link_libraries(lodctr uuid)
add_importlibs(lodctr loadperf xmllite shlwapi advapi32 msvcrt kernel32)
add_cd_file(TARGET lodctr DESTINATION reactos/system32 FOR all)
