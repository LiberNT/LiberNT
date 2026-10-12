
add_library(condrv MODULE
    condrv.c
    condrv.rc)

set_module_type(condrv kernelmodedriver)
target_link_libraries(condrv ${PSEH_LIB})
add_importlibs(condrv ntoskrnl hal)
add_cd_file(TARGET condrv DESTINATION reactos/system32/drivers NO_CAB FOR all)
