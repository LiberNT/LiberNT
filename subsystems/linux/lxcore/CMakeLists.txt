
list(APPEND SOURCE
    bpf.c
    coredump.c
    cred.c
    driver.c
    exec.c
    fdobj.c
    file.c
    fs.c
    futex.c
    hugetlb.c
    ipc.c
    kconfig.c
    keys.c
    lxwin.c
    loop.c
    inet.c
    klog.c
    netlink.c
    mm.c
    mqueue.c
    ns.c
    pipe.c
    poll.c
    process.c
    ptrace.c
    procfs.c
    seccomp.c
    signal.c
    socket.c
    squashfs.c
    sys.c
    sysdev.c
    tmpfs.c
    tty.c
    unistd.c
    vfs.c)

list(APPEND SOURCE ${ARCH}/lxarch.c ${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/lzma/LzmaDec.c)
include_directories(${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/lzma ${REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib)
if(ARCH STREQUAL "amd64")
    list(APPEND SOURCE amd64/lxsyscall.c)
    add_asm_files(lxcore_asm amd64/lxfpu.S)
endif()

add_library(lxcore MODULE ${SOURCE} ${lxcore_asm} lxcore.rc)
target_include_directories(lxcore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/${ARCH})
target_link_libraries(lxcore zlib_solo ${PSEH_LIB})
set_module_type(lxcore kernelmodedriver)
add_importlibs(lxcore ntoskrnl hal ksecdd)
add_cd_file(TARGET lxcore DESTINATION reactos/system32/drivers FOR all)
