
add_library(appledwc3 MODULE appledwc3.c appledwc3.rc)
set_module_type(appledwc3 kernelmodedriver)
target_link_libraries(appledwc3 applertkit fdtdrv)
add_importlibs(appledwc3 ntoskrnl hal)
add_cd_file(TARGET appledwc3 DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(appledwc3 appledwc3.inf)
