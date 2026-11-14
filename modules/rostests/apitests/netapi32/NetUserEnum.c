/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a failed NetUserEnum releases only what it acquired
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <lmcons.h>
#include <lmaccess.h>
#include <lmapibuf.h>
#include <lmerr.h>

#define SEED_COUNT 128

START_TEST(NetUserEnum)
{
    PVOID Seeds[SEED_COUNT];
    HANDLE Heap = GetProcessHeap();
    DWORD Read = 0xDEADBEEF, Total = 0, Resume = 0;
    LPBYTE Buffer = NULL;
    NET_API_STATUS Status;
    SIZE_T Size, Slot;
    ULONG Index;
    PVOID Victim;
    BOOL Valid;

    Victim = HeapAlloc(Heap, 0, 0x40);
    ok(Victim != NULL, "HeapAlloc failed\n");
    if (Victim == NULL)
        return;

    /* Leave freed blocks of every small size that point at a live block, where the enumeration context lands */
    for (Index = 0; Index < SEED_COUNT; Index++)
    {
        Size = 0x20 + (Index / 2 % 32) * 8;
        Seeds[Index] = HeapAlloc(Heap, 0, Size);
        for (Slot = 0; Seeds[Index] != NULL && Slot < Size / sizeof(PVOID); Slot++)
            ((PVOID *)Seeds[Index])[Slot] = Victim;
    }
    for (Index = 0; Index < SEED_COUNT; Index += 2)
    {
        if (Seeds[Index] != NULL)
            HeapFree(Heap, 0, Seeds[Index]);
    }

    Status = NetUserEnum(L"\\x", 1, FILTER_NORMAL_ACCOUNT, &Buffer, MAX_PREFERRED_LENGTH, &Read, &Total, &Resume);
    ok(Status != NERR_Success && Status != ERROR_MORE_DATA, "NetUserEnum returned %lu\n", Status);
    ok_long(Read, 0);
    ok_long(Resume, 0);

    Valid = HeapValidate(Heap, 0, Victim);
    ok(Valid, "NetUserEnum released a block it never allocated\n");

    for (Index = 1; Index < SEED_COUNT; Index += 2)
    {
        if (Seeds[Index] != NULL)
            HeapFree(Heap, 0, Seeds[Index]);
    }
    if (Buffer != NULL)
        NetApiBufferFree(Buffer);
    if (Valid)
        HeapFree(Heap, 0, Victim);
}
