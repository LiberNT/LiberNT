/*
 * PROJECT:     LiberNT GDI32
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     D3DKMT entry points that read their descriptor in user mode
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <precomp.h>

NTSTATUS
APIENTRY
D3DKMTOpenResource(
    _Inout_ D3DKMT_OPENRESOURCE *pData)
{
    const volatile UCHAR *Descriptor = (const volatile UCHAR *)pData;

    (VOID)Descriptor[0];
    (VOID)Descriptor[sizeof(*pData) - 1];
    return NtGdiDdDDIOpenResource(pData);
}
