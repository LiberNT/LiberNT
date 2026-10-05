
add_library(ramdrv MODULE ramdrv.c ramdrv.rc)
set_module_type(ramdrv kernelmodedriver)
add_importlibs(ramdrv ntoskrnl hal)
add_cd_file(TARGET ramdrv DESTINATION reactos/system32/drivers FOR all)
