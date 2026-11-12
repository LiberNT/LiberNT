
add_library(applebatt MODULE applebatt.c applebatt.rc)
set_module_type(applebatt kernelmodedriver)
add_importlibs(applebatt battc ntoskrnl hal)
add_cd_file(TARGET applebatt DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(applebatt applebatt.inf)
