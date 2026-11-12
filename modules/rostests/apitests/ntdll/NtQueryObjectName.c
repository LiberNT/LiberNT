/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the names NtQueryObject reports for a temporary object and a desktop
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef HDESK (WINAPI *PFN_GET_THREAD_DESKTOP)(DWORD);

static void ok_object_name(HANDLE Handle, PCWSTR Expected)
{
    struct { OBJECT_NAME_INFORMATION Info; WCHAR Buffer[MAX_PATH]; } Name;
    ULONG Length = 0;
    NTSTATUS Status;

    RtlZeroMemory(&Name, sizeof(Name));
    Status = NtQueryObject(Handle, ObjectNameInformation, &Name, sizeof(Name), &Length);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(Name.Info.Name.Length == wcslen(Expected) * sizeof(WCHAR) &&
       !wcsncmp(Name.Info.Name.Buffer, Expected, Name.Info.Name.Length / sizeof(WCHAR)),
       "got %wZ, expected %S\n", &Name.Info.Name, Expected);
    ok(Length >= sizeof(OBJECT_NAME_INFORMATION) + Name.Info.Name.Length, "got length %lu\n", Length);
}

static void test_temporary_object(void)
{
    WCHAR Path[MAX_PATH];
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES Attributes;
    HANDLE Event;
    NTSTATUS Status;

    _swprintf(Path, L"\\Sessions\\%lu\\BaseNamedObjects\\LiberNTNtQueryObjectName",
              NtCurrentPeb()->SessionId);
    RtlInitUnicodeString(&Name, Path);
    InitializeObjectAttributes(&Attributes, &Name, 0, NULL, NULL);
    Status = NtCreateEvent(&Event, EVENT_ALL_ACCESS, &Attributes, NotificationEvent, FALSE);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    ok_object_name(Event, Path);

    /* The name of a temporary object only leaves the namespace with its last handle */
    Status = NtMakeTemporaryObject(Event);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok_object_name(Event, Path);

    NtClose(Event);
    Status = NtOpenEvent(&Event, EVENT_ALL_ACCESS, &Attributes);
    ok_ntstatus(Status, STATUS_OBJECT_NAME_NOT_FOUND);
    if (NT_SUCCESS(Status))
        NtClose(Event);
}

static void test_desktop(void)
{
    PFN_GET_THREAD_DESKTOP pGetThreadDesktop;
    HMODULE User32;
    HDESK Desktop;

    User32 = LoadLibraryW(L"user32.dll");
    pGetThreadDesktop = (PFN_GET_THREAD_DESKTOP)GetProcAddress(User32, "GetThreadDesktop");
    if (!pGetThreadDesktop)
    {
        skip("GetThreadDesktop is not available\n");
        return;
    }

    /* A desktop is named by its window station, not by a directory */
    Desktop = pGetThreadDesktop(GetCurrentThreadId());
    ok(Desktop != NULL, "GetThreadDesktop failed %lu\n", GetLastError());
    if (Desktop)
        ok_object_name(Desktop, L"\\Default");
}

START_TEST(NtQueryObjectName)
{
    test_temporary_object();
    test_desktop();
}
