
list(APPEND SOURCE
    core/e4core.c
    core/e4resize.c
    core/e4stage.c
    close.c
    create.c
    device.c
    dirctrl.c
    driver.c
    ea.c
    fastio.c
    fcb.c
    fileinfo.c
    fsctl.c
    misc.c
    read.c
    volinfo.c
    volume.c
    write.c
    ext4p.h)

add_library(ext4 MODULE ${SOURCE} ext4.rc)
set_module_type(ext4 kernelmodedriver)
target_link_libraries(ext4 lwext4 memcmp ${PSEH_LIB})
add_importlibs(ext4 ntoskrnl hal)
add_cd_file(TARGET ext4 DESTINATION reactos/system32/drivers FOR all)
add_registry_inf(ext4_reg.inf)
