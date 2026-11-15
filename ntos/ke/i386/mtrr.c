/*

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

CODE_SEG("INIT")
VOID
NTAPI
KiInitializeMTRR(IN BOOLEAN FinalCpu)
{
    /* FIXME: Support this */
    DPRINT("MTRR support detected but not yet taken advantage of\n");
}

CODE_SEG("INIT")
VOID
NTAPI
KiAmdK6InitializeMTRR(VOID)
{
    /* FIXME: Support this */
    DPRINT("AMD MTRR support detected but not yet taken advantage of\n");
}
