
list(APPEND SOURCE
    driver.c
    exec.c
    fdobj.c
    file.c
    fs.c
    mm.c
    pipe.c
    poll.c
    process.c
    procfs.c
    signal.c
    socket.c
    sys.c
    tty.c
    vfs.c)

if(ARCH STREQUAL "arm64")
    list(APPEND SOURCE arm64/lxarch.c)
endif()

add_library(lxcore MODULE ${SOURCE} lxcore.rc)
target_link_libraries(lxcore ${PSEH_LIB})
set_module_type(lxcore kernelmodedriver)
add_importlibs(lxcore ntoskrnl hal ksecdd)
add_cd_file(TARGET lxcore DESTINATION reactos/system32/drivers FOR all)
