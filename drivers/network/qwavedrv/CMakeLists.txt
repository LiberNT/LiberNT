
add_library(qwavedrv MODULE qwavedrv.c qwavedrv.rc)
set_module_type(qwavedrv kernelmodedriver)
add_importlibs(qwavedrv ntoskrnl hal)
add_cd_file(TARGET qwavedrv DESTINATION reactos/system32/drivers FOR all)
