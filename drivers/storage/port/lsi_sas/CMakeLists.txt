list(APPEND SOURCE
    io.c
    ioc.c
    lsi_sas.c)

add_library(lsi_sas MODULE ${SOURCE} lsi_sas.rc)

set_module_type(lsi_sas kernelmodedriver)
add_importlibs(lsi_sas storport ntoskrnl hal)

add_cd_file(TARGET lsi_sas DESTINATION reactos/system32/drivers NO_CAB FOR all)

add_driver_inf(lsi_sas lsi_sas.inf)
