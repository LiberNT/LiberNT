

list(APPEND SOURCE
    usbaudio.c
    pool.c
    filter.c
    pin.c
    guid.c
    uac/uac_feedback.c
    uac/uac_desc_validate.c)

add_library(usbaudio MODULE
    ${SOURCE}
    usbaudio.rc)

target_link_libraries(usbaudio
    libcntpr
    ${PSEH_LIB})

set_module_type(usbaudio kernelmodedriver)
add_importlibs(usbaudio ntoskrnl hal usbd ks)

add_cd_file(TARGET usbaudio DESTINATION reactos/system32/drivers NO_CAB FOR all)
