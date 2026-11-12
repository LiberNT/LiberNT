
list(APPEND SOURCE
    scsi.c
    vioblk.c
    virtio.c)

add_library(vioblk MODULE ${SOURCE} vioblk.rc)

set_module_type(vioblk kernelmodedriver)
add_importlibs(vioblk storport ntoskrnl)

add_cd_file(TARGET vioblk DESTINATION reactos/system32/drivers NO_CAB FOR all)

add_driver_inf(vioblk vioblk.inf)
