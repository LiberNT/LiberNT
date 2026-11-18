
add_executable(unlodctr unlodctr_main.c ../lodctr/manifest.c)
set_module_type(unlodctr win32cui UNICODE)
target_link_libraries(unlodctr uuid)
add_importlibs(unlodctr loadperf xmllite shlwapi advapi32 msvcrt kernel32)
add_cd_file(TARGET unlodctr DESTINATION reactos/system32 FOR all)
