list(APPEND SOURCE
    blockgrp.c
    checksum.c
    chunk.c
    close.c
    cow.c
    create.c
    decompress.c
    device.c
    dirctrl.c
    driver.c
    extent.c
    fastio.c
    fileinfo.c
    fsctl.c
    inode.c
    misc.c
    modify.c
    read.c
    reserve.c
    space.c
    trans.c
    tree.c
    volinfo.c
    volume.c
    write.c
    btrfsp.h)

add_library(btrfs MODULE ${SOURCE} btrfs.rc)
target_include_directories(btrfs PRIVATE ${REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib)
target_compile_definitions(btrfs PRIVATE Z_SOLO)
set_module_type(btrfs kernelmodedriver)
target_link_libraries(btrfs zlib_solo zstd_decompress memcmp ${PSEH_LIB})
add_importlibs(btrfs ntoskrnl hal)
add_pch(btrfs btrfsp.h SOURCE)
add_cd_file(TARGET btrfs DESTINATION reactos/system32/drivers FOR all)
add_registry_inf(btrfs_reg.inf)
