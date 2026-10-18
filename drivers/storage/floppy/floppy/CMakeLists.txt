
list(APPEND SOURCE
    csqrtns.c
    floppy.c
    hardware.c
    ioctl.c
    readwrite.c
    precomp.h)

add_library(fdc MODULE ${SOURCE} floppy.rc)
target_link_libraries(fdc csq)
add_pch(fdc precomp.h SOURCE)
set_module_type(fdc kernelmodedriver)
add_importlibs(fdc hal ntoskrnl)
add_cd_file(TARGET fdc DESTINATION reactos/system32/drivers NO_CAB FOR all)
