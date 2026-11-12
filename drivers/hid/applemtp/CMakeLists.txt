
add_library(applemtp MODULE applemtp.c applemtp.rc)
set_module_type(applemtp kernelmodedriver)
target_link_libraries(applemtp applertkit fdtdrv)
add_importlibs(applemtp hidclass ntoskrnl hal)
add_cd_file(TARGET applemtp DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(applemtp applemtp.inf)
