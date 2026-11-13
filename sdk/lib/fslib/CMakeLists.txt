
if(REACTOS_BOOTSTRAP_KERNEL)
    # Kernel-only bring-up: bootntfs builds from this target's sources.
    add_subdirectory(ntfslib_new)
    return()
endif()

add_subdirectory(btrfslib)
add_subdirectory(cdfslib)
add_subdirectory(ext2lib)
add_subdirectory(ntfslib)
add_subdirectory(ntfslib_new)
add_subdirectory(vfatlib)
add_subdirectory(vfatxlib)
add_subdirectory(volresize)
