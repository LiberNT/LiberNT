
list(APPEND SOURCE
    bpf.c
    driver.c
    exec.c
    fdobj.c
    file.c
    fs.c
    futex.c
    ipc.c
    kconfig.c
    lxwin.c
    inet.c
    netlink.c
    mm.c
    mqueue.c
    ns.c
    pipe.c
    poll.c
    process.c
    procfs.c
    signal.c
    socket.c
    sys.c
    sysdev.c
    tty.c
    unistd.c
    vfs.c)

list(APPEND SOURCE ${ARCH}/lxarch.c)

add_library(lxcore MODULE ${SOURCE} lxcore.rc)
target_include_directories(lxcore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/${ARCH})
target_link_libraries(lxcore ${PSEH_LIB})
set_module_type(lxcore kernelmodedriver)
add_importlibs(lxcore ntoskrnl hal ksecdd)
add_cd_file(TARGET lxcore DESTINATION reactos/system32/drivers FOR all)
