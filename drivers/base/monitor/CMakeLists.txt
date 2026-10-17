
add_library(monitor MODULE monitor.c monitor.rc)
set_module_type(monitor kernelmodedriver)
add_importlibs(monitor ntoskrnl hal)
add_cd_file(TARGET monitor DESTINATION reactos/system32/drivers NO_CAB FOR all)
