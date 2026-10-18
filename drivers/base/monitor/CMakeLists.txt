
add_library(monitor MODULE monitor.c wmi.c brightness.c monitor.rc)
set_module_type(monitor kernelmodedriver)
add_importlibs(monitor wmilib ntoskrnl hal)
add_cd_file(TARGET monitor DESTINATION reactos/system32/drivers NO_CAB FOR all)
