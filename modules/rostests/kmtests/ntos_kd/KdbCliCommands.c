/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Runs a script of debugger commands at a stop and checks what the debugger logged
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndk/kdfuncs.h>
#include <wdbgexts.h>

#define KDB_TEST_SCRIPT_SIZE 2048
#define KDB_TEST_OUTPUT_SIZE (64 * 1024)
#define KDB_TEST_SCREEN_ROWS 24

#if defined(_M_AMD64)
#define KDB_TEST_FP_REGISTER "xmm5"
#define KDB_TEST_FP_VALUE "0x00112233445566778899aabbccddeeff"
#elif defined(_M_ARM64)
#define KDB_TEST_FP_REGISTER "v5"
#define KDB_TEST_FP_VALUE "0x00112233445566778899aabbccddeeff"
#elif defined(_M_RISCV64)
#define KDB_TEST_FP_REGISTER "f5"
#define KDB_TEST_FP_VALUE "0x0011223344556677"
#else
#define KDB_TEST_FP_REGISTER "st5"
#define KDB_TEST_FP_VALUE "0x00112233445566778899"
#endif

typedef struct _KDB_TEST_LOG
{
    PCHAR *Buffer;
    PULONG Size;
    PCHAR *WritePointer;
    PULONG RolloverCount;
} KDB_TEST_LOG;

ULONG_PTR KdbTestWaiterId;
PVOID KdbTestWaiterThread;
LIST_ENTRY KdbTestListEntry;
CHAR KdbTestScript[KDB_TEST_SCRIPT_SIZE];
ULONG_PTR KdbTestScriptLength;

static KEVENT KdbTestWakeEvent;
static CHAR KdbTestOutput[KDB_TEST_OUTPUT_SIZE];

static
VOID
NTAPI
KdbTestWaitingThread(
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    KeWaitForSingleObject(&KdbTestWakeEvent, Executive, KernelMode, FALSE, NULL);
}

static
BOOLEAN
KdbTestFindLog(
    _Out_ KDB_TEST_LOG *Log)
{
    DBGKD_GET_VERSION64 Version;
    PLIST_ENTRY Head, Entry;
    ULONG Length;
    NTSTATUS Status;

    RtlZeroMemory(&Version, sizeof(Version));
    Status = KdSystemDebugControl(SysDbgQueryVersion, NULL, 0, &Version, sizeof(Version), &Length, KernelMode);
    if (!NT_SUCCESS(Status) || (Version.DebuggerDataList == 0))
        return FALSE;

    Head = (PLIST_ENTRY)(ULONG_PTR)Version.DebuggerDataList;
    for (Entry = Head->Flink; Entry != Head; Entry = Entry->Flink)
    {
        PKDDEBUGGER_DATA64 Data = CONTAINING_RECORD(Entry, KDDEBUGGER_DATA64, Header.List);

        if (Data->Header.OwnerTag != KDBG_TAG)
            continue;

        Log->Buffer = (PCHAR *)(ULONG_PTR)*(PULONG64)&Data->KdPrintCircularBufferPtr;
        Log->Size = (PULONG)(ULONG_PTR)*(PULONG64)&Data->KdPrintBufferSize;
        Log->WritePointer = (PCHAR *)(ULONG_PTR)*(PULONG64)&Data->KdPrintWritePointer;
        Log->RolloverCount = (PULONG)(ULONG_PTR)*(PULONG64)&Data->KdPrintRolloverCount;
        return (Log->Buffer != NULL) && (Log->Size != NULL) && (Log->WritePointer != NULL) && (Log->RolloverCount != NULL);
    }

    return FALSE;
}

static
ULONG
KdbTestReadLog(
    _In_ const KDB_TEST_LOG *Log,
    _In_ ULONG From,
    _In_ ULONG FromRollovers)
{
    ULONG Size = *Log->Size;
    ULONG To = (ULONG)(*Log->WritePointer - *Log->Buffer);
    ULONG Length;
    ULONG Index;

    if (*Log->RolloverCount == FromRollovers)
        Length = (To >= From) ? To - From : 0;
    else
        Length = Size - From + To;

    if (Length > sizeof(KdbTestOutput) - 1)
    {
        From = (From + Length - (sizeof(KdbTestOutput) - 1)) % Size;
        Length = sizeof(KdbTestOutput) - 1;
    }

    for (Index = 0; Index < Length; Index++)
        KdbTestOutput[Index] = (*Log->Buffer)[(From + Index) % Size];

    KdbTestOutput[Length] = ANSI_NULL;
    return Length;
}

static
BOOLEAN
KdbTestFindMsixFunction(
    _Out_ PULONG Device,
    _Out_ PULONG Function)
{
    PCI_COMMON_HEADER Header;
    PCI_SLOT_NUMBER Slot;
    PCI_CAPABILITIES_HEADER Capability;
    UCHAR Offset;
    ULONG Count;

    for (*Device = 0; *Device < PCI_MAX_DEVICES; (*Device)++)
    {
        for (*Function = 0; *Function < PCI_MAX_FUNCTION; (*Function)++)
        {
            Slot.u.AsULONG = 0;
            Slot.u.bits.DeviceNumber = *Device;
            Slot.u.bits.FunctionNumber = *Function;

            if ((HalGetBusDataByOffset(PCIConfiguration, 0, Slot.u.AsULONG, &Header, 0, sizeof(Header)) != sizeof(Header)) ||
                (Header.VendorID == PCI_INVALID_VENDORID))
            {
                break;
            }

            if ((PCI_CONFIGURATION_TYPE(&Header) == PCI_DEVICE_TYPE) && (Header.Status & PCI_STATUS_CAPABILITIES_LIST))
            {
                Offset = Header.u.type0.CapabilitiesPtr;
                for (Count = 0; (Count < 48) && (Offset >= sizeof(Header)); Count++)
                {
                    if (HalGetBusDataByOffset(PCIConfiguration, 0, Slot.u.AsULONG, &Capability, Offset & ~3, sizeof(Capability)) !=
                        sizeof(Capability))
                    {
                        break;
                    }

                    if (Capability.CapabilityID == PCI_CAPABILITY_ID_MSIX)
                        return TRUE;

                    Offset = Capability.Next;
                }
            }

            if ((*Function == 0) && !(Header.HeaderType & PCI_MULTIFUNCTION))
                break;
        }
    }

    return FALSE;
}

static
PCSTR
KdbTestSection(
    _In_ PCSTR Marker,
    _Out_ PULONG Length)
{
    CHAR Line[40];
    PCSTR Start;
    PCSTR End;
    SIZE_T Size;

    RtlStringCbPrintfA(Line, sizeof(Line), "%s\n", Marker);
    Size = strlen(Line);
    Start = strstr(KdbTestOutput, Line);
    if (Start == NULL)
    {
        *Length = 0;
        return NULL;
    }

    /* The start-up file shows each line it runs, so the marker can come twice: as a command, then from echo */
    Start += Size;
    if (strncmp(Start, Line, Size) == 0)
        Start += Size;

    End = strstr(Start, "KDBTEST-");
    if (End == NULL)
        End = Start + strlen(Start);

    *Length = (ULONG)(End - Start);
    return Start;
}

static
BOOLEAN
KdbTestSectionHas(
    _In_ PCSTR Marker,
    _In_ PCSTR Text)
{
    ULONG Length;
    PCSTR Section = KdbTestSection(Marker, &Length);
    PCSTR Found;

    if (Section == NULL)
        return FALSE;

    Found = strstr(Section, Text);
    return (Found != NULL) && (Found + strlen(Text) <= Section + Length);
}

static
VOID
KdbTestCheckFrames(VOID)
{
    ULONG Length;
    PCSTR Section = KdbTestSection("KDBTEST-THREAD", &Length);
    PCSTR End;
    PCSTR Line;
    ULONG Frames = 0;
    ULONG Digits;

    ok(Section != NULL, "The thread section is missing\n");
    if (Section == NULL)
        return;

    End = Section + Length;
    Line = Section;
    while (Line < End)
    {
        PCSTR Next = strchr(Line, '\n');

        if ((Next == NULL) || (Next > End))
            Next = End;

        for (Digits = 0; (Digits < 2 * sizeof(PVOID)) && isxdigit((UCHAR)Line[Digits]); Digits++);

        if ((Digits == 2 * sizeof(PVOID)) && (Line[Digits] == ' '))
        {
            PCSTR Symbol = strchr(Line, '<');
            CHAR Text[64];

            RtlStringCbCopyNA(Text, sizeof(Text), Line, (SIZE_T)(Next - Line));
            Frames++;
            ok((Symbol != NULL) && (Symbol < Next), "Frame %lu of the waiting thread has no symbol: %s\n", Frames, Text);
        }

        Line = Next + 1;
    }

    ok(Frames >= 3, "The waiting thread showed %lu frames\n", Frames);
    ok(KdbTestSectionHas("KDBTEST-THREAD", "KdbTestWaitingThread"), "The waiting thread's own routine is not on its stack\n");
}

START_TEST(KdbCliCommands)
{
    KDB_TEST_LOG Log;
    LARGE_INTEGER Delay;
    PKTHREAD Waiter;
    CHAR Pci[32] = "";
    CHAR Expected[96];
    ULONG Device = 0;
    ULONG Function = 0;
    ULONG From;
    ULONG FromRollovers;
    ULONG Tries;
    ULONG Length;
    PCSTR Section;
    BOOLEAN HaveMsix;
    BOOLEAN HaveLog;
    NTSTATUS Status;

    KeInitializeEvent(&KdbTestWakeEvent, NotificationEvent, FALSE);
    Waiter = KmtStartThread(KdbTestWaitingThread, NULL);
    ok(Waiter != NULL, "The waiting thread did not start\n");
    if (Waiter == NULL)
        return;

    Delay.QuadPart = -10LL * 1000;
    for (Tries = 0; (Tries < 100) && IsListEmpty(&KdbTestWakeEvent.Header.WaitListHead); Tries++)
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    ok(!IsListEmpty(&KdbTestWakeEvent.Header.WaitListHead), "The thread never waited\n");

    KdbTestWaiterThread = Waiter;
    KdbTestWaiterId = (ULONG_PTR)PsGetThreadId((PETHREAD)Waiter);
    InitializeListHead(&KdbTestListEntry);

    HaveMsix = KdbTestFindMsixFunction(&Device, &Function);
    if (HaveMsix)
        RtlStringCbPrintfA(Pci, sizeof(Pci), "!pci 0:%lx.%lx\r\n", Device, Function);

    Status = RtlStringCbPrintfA(KdbTestScript,
                               sizeof(KdbTestScript),
                               "echo KDBTEST-NUMBER\r\n"
                               "? 0x10000000000000000\r\n"
                               "echo KDBTEST-DT\r\n"
                               "dt LIST_ENTRY\r\n"
                               "dt LIST_ENTRY KdbTestListEntry\r\n"
                               "echo KDBTEST-FP\r\n"
                               "setfpreg " KDB_TEST_FP_REGISTER " " KDB_TEST_FP_VALUE "\r\n"
                               "fpregs " KDB_TEST_FP_REGISTER "\r\n"
                               "echo KDBTEST-PCI\r\n"
                               "%s"
                               "echo KDBTEST-THREAD\r\n"
                               "thread *KdbTestWaiterId\r\n"
                               "bt\r\n"
                               "cpu current\r\n"
                               "echo KDBTEST-HELP\r\n"
                               "help\r\n"
                               "echo KDBTEST-END\r\n",
                               Pci);
    ok_eq_hex(Status, STATUS_SUCCESS);
    KdbTestScriptLength = strlen(KdbTestScript);

    HaveLog = KdbTestFindLog(&Log);
    ok(HaveLog, "The debugger's log is not on the debugger data list\n");
    From = HaveLog ? (ULONG)(*Log.WritePointer - *Log.Buffer) : 0;
    FromRollovers = HaveLog ? *Log.RolloverCount : 0;

    /* The debugger's start-up file runs KdbTestScript here */
    DbgBreakPoint();

    KeSetEvent(&KdbTestWakeEvent, IO_NO_INCREMENT, FALSE);
    KmtFinishThread(Waiter, NULL);

    if (!HaveLog)
        return;

    Length = KdbTestReadLog(&Log, From, FromRollovers);
    ok(Length != 0, "The debugger logged nothing at the stop\n");
    ok(strstr(KdbTestOutput, "KDBTEST-END\n") != NULL, "The script did not run to its end\n");

    ok(KdbTestSectionHas("KDBTEST-NUMBER", "does not fit in 64 bits"), "A number too large for 64 bits was not refused\n");

    RtlStringCbPrintfA(Expected, sizeof(Expected), "+0x%03lx Blink", (ULONG)FIELD_OFFSET(LIST_ENTRY, Blink));
    ok(KdbTestSectionHas("KDBTEST-DT", Expected), "dt did not show %s\n", Expected);
    RtlStringCbPrintfA(Expected, sizeof(Expected), "+0x%03lx %-28s %p", (ULONG)FIELD_OFFSET(LIST_ENTRY, Flink), "Flink",
                       KdbTestListEntry.Flink);
    ok(KdbTestSectionHas("KDBTEST-DT", Expected), "dt did not show %s\n", Expected);

    Section = KdbTestSection("KDBTEST-FP", &Length);
    ok((Section != NULL) && (Length != 0), "The floating-point section is missing\n");
    if (Section != NULL)
    {
        PCSTR First = strstr(Section, KDB_TEST_FP_VALUE);
        PCSTR Second = (First != NULL) ? strstr(First + 1, KDB_TEST_FP_VALUE) : NULL;

        ok((First != NULL) && (First < Section + Length), "setfpreg did not show " KDB_TEST_FP_VALUE "\n");
        ok((Second != NULL) && (Second < Section + Length), "fpregs did not read " KDB_TEST_FP_VALUE " back\n");
    }

    if (!skip(HaveMsix, "No function on bus 0 has an MSI-X capability\n"))
    {
        ok(KdbTestSectionHas("KDBTEST-PCI", "MSI-X "), "!pci 0:%lx.%lx did not decode the MSI-X capability\n", Device, Function);
        ok(KdbTestSectionHas("KDBTEST-PCI", " entries, table bar"), "!pci 0:%lx.%lx did not show the MSI-X table\n",
           Device, Function);
    }

    KdbTestCheckFrames();

    Section = KdbTestSection("KDBTEST-HELP", &Length);
    if (Section != NULL)
    {
        ULONG Lines = 0;
        ULONG Index;

        for (Index = 0; Index < Length; Index++)
        {
            if (Section[Index] == '\n')
                Lines++;
        }

        ok(Lines > KDB_TEST_SCREEN_ROWS, "help printed %lu lines, no more than a screen\n", Lines);
    }
    else
    {
        ok(FALSE, "The help section is missing\n");
    }
}
