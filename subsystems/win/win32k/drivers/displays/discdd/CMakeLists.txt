
add_library(discdd MODULE
    discdd.c
    discdd.rc)

set_module_type(discdd kerneldll ENTRYPOINT DrvEnableDriver 12)
target_link_libraries(discdd libcntpr)
add_importlibs(discdd win32k ntoskrnl)
add_dependencies(discdd psdk)
add_cd_file(TARGET discdd DESTINATION reactos/system32 FOR all)
