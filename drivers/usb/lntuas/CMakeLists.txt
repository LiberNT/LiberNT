
list(APPEND SOURCE
    lntuas.c
    uas.c)

add_library(lntuas MODULE
    ${SOURCE}
    lntuas.rc)

set_module_type(lntuas kernelmodedriver)
add_importlibs(lntuas storport ntoskrnl hal usbd)
add_cd_file(TARGET lntuas DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(lntuas lntuas.inf)
