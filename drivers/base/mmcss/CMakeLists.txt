
add_library(mmcss MODULE mmcss.c mmcss.rc)
set_module_type(mmcss kernelmodedriver)
add_importlibs(mmcss ntoskrnl hal)
add_cd_file(TARGET mmcss DESTINATION reactos/system32/drivers FOR all)
add_registry_inf(mmcss_reg.inf)
