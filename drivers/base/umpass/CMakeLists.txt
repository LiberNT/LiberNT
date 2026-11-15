
add_library(umpass MODULE umpass.c umpass.rc)
set_module_type(umpass kernelmodedriver)
add_importlibs(umpass ntoskrnl hal)
add_cd_file(TARGET umpass DESTINATION reactos/system32/drivers FOR all)
