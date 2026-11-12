
add_library(applepd MODULE applepd.c applepd.rc)
set_module_type(applepd kernelmodedriver)
target_link_libraries(applepd applertkit fdtdrv)
add_importlibs(applepd ntoskrnl hal)
add_cd_file(TARGET applepd DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(applepd applepd.inf)
