
# We need to define this here, because the target definitions are not
# passed to CL, when preprocessing the asm files on MSVC builds.
# See CORE-19847
add_definitions(-D_MINIHAL_)

list(APPEND MINI_HAL_SOURCE
    ../legacy/bus/bushndlr.c
    ../legacy/bus/cmosbus.c
    ../legacy/bus/isabus.c
    ../legacy/bus/pcibus.c
    ../legacy/bussupp.c
    ../generic/dma.c
    ../generic/drive.c
    ../generic/misc.c
    ../generic/nmi.c
    ../generic/spinlock.c
    ../generic/timer.c
    ../generic/usage.c
    ../pic/pic.c
    ../include/hal.h
    halinit.c
    ../generic/beep.c
    ../generic/clock.c
    ../generic/cmos.c
    ../generic/reboot.c)

add_library(mini_hal ${MINI_HAL_SOURCE})
target_compile_definitions(mini_hal PRIVATE _BLDR_ _NTSYSTEM_)
add_dependencies(mini_hal psdk bugcodes asm)
add_pch(mini_hal ../include/hal.h MINI_HAL_SOURCE)
