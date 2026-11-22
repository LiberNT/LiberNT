/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test TdhGetPropertySize and TdhGetProperty parameter checks and an event without a schema
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <evntcons.h>

/* The tree has no tdh.h; layout and prototypes from the Windows SDK 10.0.16299 tdh.h */
typedef struct _PROPERTY_DATA_DESCRIPTOR
{
    ULONGLONG PropertyName;
    ULONG ArrayIndex;
    ULONG Reserved;
} PROPERTY_DATA_DESCRIPTOR, *PPROPERTY_DATA_DESCRIPTOR;

typedef struct _TDH_CONTEXT *PTDH_CONTEXT;

ULONG WINAPI TdhGetPropertySize(PEVENT_RECORD, ULONG, PTDH_CONTEXT, ULONG, PPROPERTY_DATA_DESCRIPTOR, ULONG *);
ULONG WINAPI TdhGetProperty(PEVENT_RECORD, ULONG, PTDH_CONTEXT, ULONG, PPROPERTY_DATA_DESCRIPTOR, ULONG, PBYTE);

static const GUID UnregisteredProvider = {0x6b0c4fa3, 0x2a31, 0x4b6e, {0x9d, 0x55, 0x1f, 0x7e, 0x30, 0x84, 0x2c, 0x61}};

START_TEST(TdhProperty)
{
    EVENT_RECORD Event;
    PROPERTY_DATA_DESCRIPTOR Descriptor;
    ULONGLONG Context[4];
    BYTE Buffer[64];
    ULONG Size;

    ZeroMemory(&Event, sizeof(Event));
    Event.EventHeader.Size = sizeof(Event.EventHeader);
    Event.EventHeader.ProviderId = UnregisteredProvider;
    Event.EventHeader.EventDescriptor.Id = 1;
    ZeroMemory(&Descriptor, sizeof(Descriptor));
    Descriptor.PropertyName = (ULONGLONG)(ULONG_PTR)L"Name";
    Descriptor.ArrayIndex = ULONG_MAX;

    Size = 0xCCCC;
    ok_long(TdhGetPropertySize(&Event, 0, NULL, 1, &Descriptor, &Size), ERROR_NOT_FOUND);
    ok_long(Size, 0xCCCC);
    ok_long(TdhGetPropertySize(NULL, 0, NULL, 1, &Descriptor, &Size), ERROR_INVALID_PARAMETER);
    ok_long(TdhGetPropertySize(&Event, 0, NULL, 0, &Descriptor, &Size), ERROR_INVALID_PARAMETER);
    ok_long(TdhGetPropertySize(&Event, 0, NULL, 1, NULL, &Size), ERROR_INVALID_PARAMETER);
    ok_long(TdhGetPropertySize(&Event, 0, NULL, 1, &Descriptor, NULL), ERROR_INVALID_PARAMETER);
    ok_long(TdhGetPropertySize(&Event, 1, NULL, 1, &Descriptor, &Size), ERROR_INVALID_PARAMETER);

    ok_long(TdhGetProperty(&Event, 0, NULL, 1, &Descriptor, sizeof(Buffer), Buffer), ERROR_NOT_FOUND);
    ok_long(TdhGetProperty(NULL, 0, NULL, 1, &Descriptor, sizeof(Buffer), Buffer), ERROR_INVALID_PARAMETER);
    ok_long(TdhGetProperty(&Event, 0, NULL, 0, &Descriptor, sizeof(Buffer), Buffer), ERROR_INVALID_PARAMETER);
    ok_long(TdhGetProperty(&Event, 0, NULL, 1, NULL, sizeof(Buffer), Buffer), ERROR_INVALID_PARAMETER);
    ok_long(TdhGetProperty(&Event, 0, NULL, 1, &Descriptor, sizeof(Buffer), NULL), ERROR_INVALID_PARAMETER);
    ok_long(TdhGetProperty(&Event, 0, NULL, 1, &Descriptor, 0, Buffer), ERROR_INVALID_PARAMETER);
    ok_long(TdhGetProperty(&Event, 1, NULL, 1, &Descriptor, sizeof(Buffer), Buffer), ERROR_INVALID_PARAMETER);

    ZeroMemory(Context, sizeof(Context));
    ok_long(TdhGetPropertySize(&Event, 1, (PTDH_CONTEXT)Context, 1, &Descriptor, &Size), ERROR_NOT_FOUND);
}
