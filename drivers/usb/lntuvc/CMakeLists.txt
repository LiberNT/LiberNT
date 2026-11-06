
list(APPEND SOURCE
    control.c
    descriptor.c
    filter.c
    guid.c
    pin.c
    still.c
    lntuvc.c)

add_library(lntuvc MODULE
    ${SOURCE}
    lntuvc.rc)

target_link_libraries(lntuvc
    libcntpr
    ${PSEH_LIB})

set_module_type(lntuvc kernelmodedriver)
add_importlibs(lntuvc ks ntoskrnl hal usbd)
add_cd_file(TARGET lntuvc DESTINATION reactos/system32/drivers NO_CAB FOR all)
