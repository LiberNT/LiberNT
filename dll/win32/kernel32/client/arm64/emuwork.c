/*
 * PROJECT:     LiberNT System Libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Emulator work notifications for processes run by a WoW64 CPU backend
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <k32.h>

PVOID
WINAPI
BasepOpenEmulatorWork(
    _In_ HANDLE Process)
{
    HANDLE Section;
    PVOID WorkList;

    RtlOpenCrossProcessEmulatorWorkConnection(Process, &Section, &WorkList);
    if (Section)
        NtClose(Section);
    return WorkList;
}

VOID
WINAPI
BasepCloseEmulatorWork(
    _In_opt_ PVOID WorkList)
{
    if (WorkList)
        NtUnmapViewOfSection(NtCurrentProcess(), WorkList);
}

VOID
WINAPI
BasepPostEmulatorWork(
    _In_opt_ PVOID WorkList,
    _In_ ULONG Id,
    _In_ PVOID Address,
    _In_ SIZE_T Size,
    _In_ ULONG ArgumentCount,
    _In_ ULONG Argument0,
    _In_ ULONG Argument1)
{
    PCROSS_PROCESS_WORK_LIST List = WorkList;
    PCROSS_PROCESS_WORK_ENTRY Entry;
    PVOID Unused;

    if (!List)
        return;

    Entry = RtlWow64PopCrossProcessWorkFromFreeList(&List->free_list);
    if (!Entry)
        return;

    Entry->id = Id;
    Entry->addr = (ULONG_PTR)Address;
    Entry->size = Size;
    if (ArgumentCount > 0)
        Entry->args[0] = Argument0;
    if (ArgumentCount > 1)
        Entry->args[1] = Argument1;
    RtlWow64PushCrossProcessWorkOntoWorkList(&List->work_list, Entry, &Unused);
}
