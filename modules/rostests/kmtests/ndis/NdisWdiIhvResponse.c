/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The IHV data a WDI miniport answers is handed back as it came
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndis.h>
#include "../../../../drivers/network/ndis/include/ndis6_wdi_ihv.h"

START_TEST(NdisWdiIhvResponse)
{
    static const UCHAR Reply[] = { 0xA1, 0xB2, 0xC3 };
    UCHAR Output[8];
    ULONG Written;
    ULONG Needed;
    NDIS_STATUS Status;

    RtlFillMemory(Output, sizeof(Output), 0xEE);
    Status = Ndis6WdiIhvCopyResponse(Reply, sizeof(Reply), Output, sizeof(Output), &Written, &Needed);
    ok_eq_hex(Status, NDIS_STATUS_SUCCESS);
    ok_eq_ulong(Written, (ULONG)sizeof(Reply));
    ok_eq_ulong(Needed, 0UL);
    ok(RtlCompareMemory(Output, Reply, sizeof(Reply)) == sizeof(Reply), "the answer is copied as it came\n");
    ok_eq_hex(Output[sizeof(Reply)], 0xEE);

    RtlFillMemory(Output, sizeof(Output), 0xEE);
    Status = Ndis6WdiIhvCopyResponse(Reply, sizeof(Reply), Output, sizeof(Reply) - 1, &Written, &Needed);
    ok_eq_hex(Status, NDIS_STATUS_BUFFER_TOO_SHORT);
    ok_eq_ulong(Written, 0UL);
    ok_eq_ulong(Needed, (ULONG)sizeof(Reply));
    ok_eq_hex(Output[0], 0xEE);

    Status = Ndis6WdiIhvCopyResponse(NULL, 0, NULL, 0, &Written, &Needed);
    ok_eq_hex(Status, NDIS_STATUS_SUCCESS);
    ok_eq_ulong(Written, 0UL);
    ok_eq_ulong(Needed, 0UL);

    Status = Ndis6WdiIhvCopyResponse(Reply, sizeof(Reply), Output, sizeof(Reply), &Written, &Needed);
    ok_eq_hex(Status, NDIS_STATUS_SUCCESS);
    ok_eq_ulong(Written, (ULONG)sizeof(Reply));
}
