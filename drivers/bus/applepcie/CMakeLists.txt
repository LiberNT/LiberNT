
add_library(applepcie MODULE applepcie.c applepcie.rc)
set_module_type(applepcie kernelmodedriver)
target_link_libraries(applepcie applertkit fdtdrv)
add_importlibs(applepcie ntoskrnl hal)
add_cd_file(TARGET applepcie DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(applepcie applepcie.inf)
