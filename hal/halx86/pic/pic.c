/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * PURPOSE:         HAL PIC Management and Control Code
 * PROGRAMMERS:     ReactOS Portable Systems Group
 *                  Ahmed ARIF (arif.ing@outlook.com)
 */

/* INCLUDES *******************************************************************/

#include <hal.h>

#define NDEBUG
#include <debug.h>

VOID
NTAPI
HalpEndSoftwareInterrupt(IN KIRQL OldIrql,
                         IN PKTRAP_FRAME TrapFrame);

/* GLOBALS ********************************************************************/


KIRQL
NTAPI
KeGetCurrentIrql(VOID)
{
    return PASSIVE_LEVEL;
}

VOID
FASTCALL
KfLowerIrql(
    IN KIRQL OldIrql)
{
}

KIRQL
FASTCALL
KfRaiseIrql(
    IN KIRQL NewIrql)
{
    return NewIrql;
}


NTSTATUS
HalpSecondaryQueryInformation(
    _In_ ULONG BufferSize,
    _Out_ PVOID Buffer,
    _Out_opt_ PULONG ReturnedLength)
{
    UNREFERENCED_PARAMETER(BufferSize);
    UNREFERENCED_PARAMETER(Buffer);
    if (ReturnedLength != NULL)
        *ReturnedLength = 0;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
HalpSecondaryRegisterInterface(
    _In_ ULONG BufferSize,
    _In_ PVOID Buffer)
{
    UNREFERENCED_PARAMETER(BufferSize);
    UNREFERENCED_PARAMETER(Buffer);
    return STATUS_NOT_SUPPORTED;
}
