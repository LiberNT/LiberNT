
add_library(hidi2c MODULE hidi2c.c hidi2c.rc)
set_module_type(hidi2c kernelmodedriver)
add_importlibs(hidi2c hidclass ntoskrnl hal)
add_cd_file(TARGET hidi2c DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(hidi2c hidi2c.inf)
