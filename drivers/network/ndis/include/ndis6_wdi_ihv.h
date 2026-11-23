/*
 * PROJECT:     LiberNT NDIS library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     IHV request data returned to the sender of an IHV control
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _NDIS6_WDI_IHV_H_
#define _NDIS6_WDI_IHV_H_

FORCEINLINE
NDIS_STATUS
Ndis6WdiIhvCopyResponse(
    _In_reads_bytes_opt_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _Out_writes_bytes_to_opt_(Capacity, *Written) PUCHAR Output,
    _In_ ULONG Capacity,
    _Out_ PULONG Written,
    _Out_ PULONG Needed)
{
    ASSERT(Data != NULL || Length == 0);
    ASSERT(Output != NULL || Capacity == 0);

    *Written = 0;
    if (Length > Capacity)
    {
        *Needed = Length;
        return NDIS_STATUS_BUFFER_TOO_SHORT;
    }

    if (Length != 0)
        RtlCopyMemory(Output, Data, Length);
    *Written = Length;
    *Needed = 0;
    return NDIS_STATUS_SUCCESS;
}

#endif /* _NDIS6_WDI_IHV_H_ */
