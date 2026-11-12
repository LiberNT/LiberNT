
#include <ksarm.h>


    TEXTAREA

    LEAF_ENTRY KeSwitchKernelStack
    __assertfail
    bx lr
    LEAF_END KeSwitchKernelStack

    /* TODO: run KiRunStackCallout on the given stack */
    LEAF_ENTRY KiCallOnKernelStack
    __assertfail
    bx lr
    LEAF_END KiCallOnKernelStack

    LEAF_ENTRY KiPassiveRelease
    __assertfail
    bx lr
    LEAF_END KiPassiveRelease

    END
/* EOF */
