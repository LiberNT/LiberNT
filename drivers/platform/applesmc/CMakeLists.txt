
add_library(applesmc MODULE applesmc.c applesmc.rc)
set_module_type(applesmc kernelmodedriver)
target_link_libraries(applesmc applertkit fdtdrv)
add_importlibs(applesmc ntoskrnl hal)
add_cd_file(TARGET applesmc DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(applesmc applesmc.inf)
