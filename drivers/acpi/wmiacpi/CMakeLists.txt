
add_library(wmiacpi MODULE wmiacpi.c wmiacpi.rc)
set_module_type(wmiacpi kernelmodedriver)
add_importlibs(wmiacpi wmilib ntoskrnl hal)
add_cd_file(TARGET wmiacpi DESTINATION reactos/system32/drivers FOR all)
