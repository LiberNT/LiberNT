/*
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

VOID
NTAPI
{
    PKIPCR Pcr = (PKIPCR)KeGetPcr();
    LONG SetMember;

    SetMember = (LONG)Pcr->SetMember;
    InterlockedXor((PLONG)&NewProcess->ActiveProcessors, SetMember);
    InterlockedXor((PLONG)&OldProcess->ActiveProcessors, SetMember);

    if (NewProcess->LdtDescriptor.LimitLow != OldProcess->LdtDescriptor.LimitLow)
    {
        if (NewProcess->LdtDescriptor.LimitLow)
        {
            KeSetGdtSelector(KGDT_LDT,
                             ((PULONG)&NewProcess->LdtDescriptor)[0],
                             ((PULONG)&NewProcess->LdtDescriptor)[1]);
            Ke386SetLocalDescriptorTable(KGDT_LDT);
        }
        else
        {
            Ke386SetLocalDescriptorTable(0);
        }
    }

    __writecr3(KPROCESS_DTB0(NewProcess));

    Ke386SetGs(0);

    Pcr->TSS->IoMapBase = NewProcess->IopmOffset;
}
