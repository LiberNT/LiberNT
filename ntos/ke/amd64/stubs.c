/*
 */

#include <ntoskrnl.h>






PVOID
NTAPI
{
    PKIPCR Pcr;








    }






}

static
VOID
{
}

DECLSPEC_NORETURN
VOID
KiIdleLoop(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();

    {
        _enable();
        YieldProcessor();
        YieldProcessor();
        _disable();

        {
            HalClearSoftwareInterrupt(DISPATCH_LEVEL);
            KiRetireDpcList(Prcb);
        }

        _disable();
        KiAcquirePrcbLock(Prcb);


        if (NewThread == Prcb->IdleThread)
        {
            ASSERT(Prcb->CurrentThread == NewThread);
            NewThread->State = Running;
            KiReleasePrcbLock(Prcb);
        }
        {
        }
        else
        {
        }
    }
}

VOID
NTAPI
{


    __writecr3(NewProcess->DirectoryTableBase);

}

NTSTATUS
NTAPI
{
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
{
    return STATUS_NOT_IMPLEMENTED;
}
