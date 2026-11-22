/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later),
 *              with portions under the BSD license in COPYING.ARM
 * PURPOSE:     i386 extended state save and restore around context switches
 * COPYRIGHT:   Copyright ReactOS Portable Systems Group
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

VOID
NTAPI
KiSwapProcess(
    _In_ PKPROCESS NewProcess,
    _In_ PKPROCESS OldProcess)
{
    PKIPCR Pcr = (PKIPCR)KeGetPcr();
    LONG SetMember;

    SetMember = (LONG)Pcr->SetMember;
    InterlockedXor((PLONG)KiProcessActiveProcessors(NewProcess), SetMember);
    InterlockedXor((PLONG)KiProcessActiveProcessors(OldProcess), SetMember);

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
