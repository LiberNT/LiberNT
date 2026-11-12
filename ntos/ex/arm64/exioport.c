/*
 * PROJECT:     LiberNT Kernel (ARM64)
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Register and port access as calls a driver can be linked against
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/* These are the out-of-line forms of what the headers inline, so the macros
   must not be in the way of the definitions. */
#define NO_PORT_MACROS
#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * A device register is reached through a mapping the caller already holds, and
 * every access is ordered against the ones around it, because this processor
 * does not order them on its own.
 */
UCHAR
NTAPI
READ_REGISTER_UCHAR(
    _In_ PUCHAR Register)
{
    UCHAR Value = *(volatile UCHAR * const)Register;

    ARM64_DMB(SY);

    return Value;
}

USHORT
NTAPI
READ_REGISTER_USHORT(
    _In_ PUSHORT Register)
{
    USHORT Value = *(volatile USHORT * const)Register;

    ARM64_DMB(SY);

    return Value;
}

ULONG
NTAPI
READ_REGISTER_ULONG(
    _In_ PULONG Register)
{
    ULONG Value = *(volatile ULONG * const)Register;

    ARM64_DMB(SY);

    return Value;
}

VOID
NTAPI
WRITE_REGISTER_UCHAR(
    _In_ PUCHAR Register,
    _In_ UCHAR Value)
{
    ARM64_DMB(SY);

    *(volatile UCHAR * const)Register = Value;
}

VOID
NTAPI
WRITE_REGISTER_USHORT(
    _In_ PUSHORT Register,
    _In_ USHORT Value)
{
    ARM64_DMB(SY);

    *(volatile USHORT * const)Register = Value;
}

VOID
NTAPI
WRITE_REGISTER_ULONG(
    _In_ PULONG Register,
    _In_ ULONG Value)
{
    ARM64_DMB(SY);

    *(volatile ULONG * const)Register = Value;
}

VOID
NTAPI
READ_REGISTER_BUFFER_UCHAR(
    _In_ PUCHAR Register,
    _Out_writes_all_(Count) PUCHAR Buffer,
    _In_ ULONG Count)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        Buffer[Index] = *(volatile UCHAR * const)&Register[Index];
    }

    ARM64_DMB(SY);
}

VOID
NTAPI
READ_REGISTER_BUFFER_USHORT(
    _In_ PUSHORT Register,
    _Out_writes_all_(Count) PUSHORT Buffer,
    _In_ ULONG Count)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        Buffer[Index] = *(volatile USHORT * const)&Register[Index];
    }

    ARM64_DMB(SY);
}

VOID
NTAPI
READ_REGISTER_BUFFER_ULONG(
    _In_ PULONG Register,
    _Out_writes_all_(Count) PULONG Buffer,
    _In_ ULONG Count)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        Buffer[Index] = *(volatile ULONG * const)&Register[Index];
    }

    ARM64_DMB(SY);
}

VOID
NTAPI
WRITE_REGISTER_BUFFER_UCHAR(
    _In_ PUCHAR Register,
    _In_reads_(Count) PUCHAR Buffer,
    _In_ ULONG Count)
{
    ULONG Index;

    ARM64_DMB(SY);

    for (Index = 0; Index < Count; Index++)
    {
        *(volatile UCHAR * const)&Register[Index] = Buffer[Index];
    }
}

VOID
NTAPI
WRITE_REGISTER_BUFFER_USHORT(
    _In_ PUSHORT Register,
    _In_reads_(Count) PUSHORT Buffer,
    _In_ ULONG Count)
{
    ULONG Index;

    ARM64_DMB(SY);

    for (Index = 0; Index < Count; Index++)
    {
        *(volatile USHORT * const)&Register[Index] = Buffer[Index];
    }
}

VOID
NTAPI
WRITE_REGISTER_BUFFER_ULONG(
    _In_ PULONG Register,
    _In_reads_(Count) PULONG Buffer,
    _In_ ULONG Count)
{
    ULONG Index;

    ARM64_DMB(SY);

    for (Index = 0; Index < Count; Index++)
    {
        *(volatile ULONG * const)&Register[Index] = Buffer[Index];
    }
}

/* EOF */
