/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What KdSystemDebugControl serves a kernel-mode caller while a debugger is attached
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndk/kdfuncs.h>
#include <wdbgexts.h>

static KBUGCHECK_CALLBACK_RECORD CallbackRecord;
static UCHAR CallbackBuffer[8];

static
NTSTATUS
BusData(
    _In_ SYSDBG_COMMAND Command,
    _In_ ULONG Offset,
    _Inout_updates_bytes_(Size) PVOID Buffer,
    _In_ ULONG Size,
    _Out_ PULONG Length)
{
    SYSDBG_BUS_DATA Request;

    RtlZeroMemory(&Request, sizeof(Request));
    Request.Address = Offset;
    Request.Buffer = Buffer;
    Request.Request = Size;
    Request.BusDataType = PCIConfiguration;
    Request.BusNumber = 0;
    Request.SlotNumber = 0;

    *Length = 0xDEADBEEF;
    return KdSystemDebugControl(Command, &Request, sizeof(Request), NULL, 0, Length, KernelMode);
}

static
VOID
TestBusData(VOID)
{
    const ULONG LineOffset = FIELD_OFFSET(PCI_COMMON_CONFIG, u.type0.InterruptLine);
    ULONG Expected = 0, Value = 0, Length, HalLength;
    UCHAR Line = 0;
    NTSTATUS Status;

    HalLength = HalGetBusDataByOffset(PCIConfiguration, 0, 0, &Expected, 0, sizeof(Expected));
    if (skip(HalLength == sizeof(Expected) && (USHORT)Expected != PCI_INVALID_VENDORID,
             "No PCI function answers at 0:0.0\n"))
    {
        return;
    }

    Status = BusData(SysDbgReadBusData, 0, &Value, sizeof(Value), &Length);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Length, HalLength);
    ok_eq_hex(Value, Expected);

    HalLength = HalGetBusDataByOffset(PCIConfiguration, 0, 0, &Line, LineOffset, sizeof(Line));
    ok_eq_ulong(HalLength, (ULONG)sizeof(Line));
    HalLength = HalSetBusDataByOffset(PCIConfiguration, 0, 0, &Line, LineOffset, sizeof(Line));
    if (skip(HalLength == sizeof(Line), "The HAL does not write the interrupt line of 0:0.0\n"))
        return;

    Status = BusData(SysDbgWriteBusData, LineOffset, &Line, sizeof(Line), &Length);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Length, (ULONG)sizeof(Line));
}

static
VOID
TestCheckLowMemory(VOID)
{
    ULONG Length;
    NTSTATUS Status;

    /* Nothing below 4 GB carries the /NOLOWMEM pattern, so no corruption can be found */
    Status = KdSystemDebugControl(SysDbgCheckLowMemory, NULL, 0, NULL, 0, &Length, KernelMode);
    ok_eq_hex(Status, STATUS_SUCCESS);
}

static
VOID
NTAPI
BugCheckCallback(
    _In_ PVOID Buffer,
    _In_ ULONG Length)
{
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Length);
}

static
PKDDEBUGGER_DATA64
FindDebuggerData(VOID)
{
    DBGKD_GET_VERSION64 Version;
    PLIST_ENTRY Head, Entry;
    ULONG Length;
    NTSTATUS Status;

    RtlZeroMemory(&Version, sizeof(Version));
    Status = KdSystemDebugControl(SysDbgQueryVersion, NULL, 0, &Version, sizeof(Version), &Length, KernelMode);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status) || (Version.DebuggerDataList == 0))
        return NULL;

    Head = (PLIST_ENTRY)(ULONG_PTR)Version.DebuggerDataList;
    for (Entry = Head->Flink; Entry != Head; Entry = Entry->Flink)
    {
        PKDDEBUGGER_DATA64 Data = CONTAINING_RECORD(Entry, KDDEBUGGER_DATA64, Header.List);

        if (Data->Header.OwnerTag == KDBG_TAG)
            return Data;
    }

    return NULL;
}

static
VOID
TestBugCheckCallbackList(VOID)
{
    PKDDEBUGGER_DATA64 Data = FindDebuggerData();
    PLIST_ENTRY Head, Entry;
    BOOLEAN Found = FALSE;

    ok(Data != NULL, "No KDBG_TAG block on the debugger data list\n");
    if (Data == NULL)
        return;

    KeInitializeCallbackRecord(&CallbackRecord);
    if (skip(KeRegisterBugCheckCallback(&CallbackRecord,
                                        BugCheckCallback,
                                        CallbackBuffer,
                                        sizeof(CallbackBuffer),
                                        (PUCHAR)"KdDebuggerPresent"),
             "The bug check callback was not registered\n"))
    {
        return;
    }

    Head = (PLIST_ENTRY)(ULONG_PTR)Data->KeBugCheckCallbackListHead;
    ok(Head != NULL, "KeBugCheckCallbackListHead is NULL\n");
    for (Entry = (Head != NULL) ? Head->Flink : NULL; (Entry != NULL) && (Entry != Head); Entry = Entry->Flink)
    {
        if (Entry == &CallbackRecord.Entry)
        {
            Found = TRUE;
            break;
        }
    }
    ok(Found, "The registered record is not on the list KeBugCheckCallbackListHead names\n");

    ok_bool_true(KeDeregisterBugCheckCallback(&CallbackRecord), "KeDeregisterBugCheckCallback returned");
}

static
VOID
TestVersionKernelBase(VOID)
{
    PRTL_PROCESS_MODULES Modules;
    DBGKD_GET_VERSION64 Version;
    ULONG Length = 0;
    NTSTATUS Status;

    RtlZeroMemory(&Version, sizeof(Version));
    Status = KdSystemDebugControl(SysDbgQueryVersion, NULL, 0, &Version, sizeof(Version), &Length, KernelMode);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = ZwQuerySystemInformation(SystemModuleInformation, NULL, 0, &Length);
    ok_eq_hex(Status, STATUS_INFO_LENGTH_MISMATCH);

    Modules = ExAllocatePoolWithTag(PagedPool, Length, 'vDdK');
    ok(Modules != NULL, "No memory for the module list\n");
    if (Modules == NULL)
        return;

    Status = ZwQuerySystemInformation(SystemModuleInformation, Modules, Length, &Length);
    ok_eq_hex(Status, STATUS_SUCCESS);

    if (NT_SUCCESS(Status) && (Modules->NumberOfModules != 0))
        ok_eq_ulonglong(Version.KernBase, (ULONG64)(ULONG_PTR)Modules->Modules[0].ImageBase);

    ExFreePoolWithTag(Modules, 'vDdK');
}

START_TEST(KdDebuggerPresent)
{
    BOOLEAN NotPresent;

    if (skip(KD_DEBUGGER_ENABLED, "The kernel debugger is not enabled\n"))
        return;

    /* KD_DEBUGGER_NOT_PRESENT is documented as writable by any kernel-mode binary */
    NotPresent = KD_DEBUGGER_NOT_PRESENT;
    KD_DEBUGGER_NOT_PRESENT = FALSE;

    TestBusData();
    TestCheckLowMemory();
    TestBugCheckCallbackList();
    TestVersionKernelBase();

    KD_DEBUGGER_NOT_PRESENT = NotPresent;
}

/* EOF */
