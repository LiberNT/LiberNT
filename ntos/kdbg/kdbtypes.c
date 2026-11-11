/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The layouts of the kernel's own types the debugger knows without symbols: dt
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kdb.h"
#include "kdbpriv.h"
#define NDEBUG
#include <debug.h>

typedef enum _KDB_FIELD_KIND
{
    KdbFieldValue,
    KdbFieldPointer,
    KdbFieldSymbol,
    KdbFieldUnicode,
    KdbFieldAnsi,
    KdbFieldEmbedded
} KDB_FIELD_KIND;

typedef struct _KDB_TYPE_FIELD
{
    PCSTR Name;
    PCSTR Type;
    ULONG Offset;
    ULONG Size;
    KDB_FIELD_KIND Kind;
} KDB_TYPE_FIELD;

typedef struct _KDB_TYPE
{
    PCSTR Name;
    ULONG Size;
    const KDB_TYPE_FIELD *Fields;
    ULONG FieldCount;
} KDB_TYPE;

#define KDB_FIELD(Struct, Field, Type, Kind) \
    {#Field, Type, FIELD_OFFSET(Struct, Field), RTL_FIELD_SIZE(Struct, Field), Kind}

#define KDB_TYPE_ENTRY(Struct, Fields) \
    {#Struct, sizeof(Struct), Fields, RTL_NUMBER_OF(Fields)}

/* GLOBALS ********************************************************************/

static const KDB_TYPE_FIELD KdbpListEntryFields[] =
{
    KDB_FIELD(LIST_ENTRY, Flink, "LIST_ENTRY *", KdbFieldPointer),
    KDB_FIELD(LIST_ENTRY, Blink, "LIST_ENTRY *", KdbFieldPointer)
};

static const KDB_TYPE_FIELD KdbpUnicodeStringFields[] =
{
    KDB_FIELD(UNICODE_STRING, Length, "USHORT", KdbFieldValue),
    KDB_FIELD(UNICODE_STRING, MaximumLength, "USHORT", KdbFieldValue),
    KDB_FIELD(UNICODE_STRING, Buffer, "PWSTR", KdbFieldPointer)
};

static const KDB_TYPE_FIELD KdbpClientIdFields[] =
{
    KDB_FIELD(CLIENT_ID, UniqueProcess, "HANDLE", KdbFieldPointer),
    KDB_FIELD(CLIENT_ID, UniqueThread, "HANDLE", KdbFieldPointer)
};

static const KDB_TYPE_FIELD KdbpProcessFields[] =
{
    KDB_FIELD(EPROCESS, Pcb, "KPROCESS", KdbFieldEmbedded),
    KDB_FIELD(EPROCESS, CreateTime, "LARGE_INTEGER", KdbFieldValue),
    KDB_FIELD(EPROCESS, ExitTime, "LARGE_INTEGER", KdbFieldValue),
    KDB_FIELD(EPROCESS, UniqueProcessId, "HANDLE", KdbFieldPointer),
    KDB_FIELD(EPROCESS, ActiveProcessLinks, "LIST_ENTRY", KdbFieldEmbedded),
    KDB_FIELD(EPROCESS, ObjectTable, "HANDLE_TABLE *", KdbFieldPointer),
    KDB_FIELD(EPROCESS, Token, "EX_FAST_REF", KdbFieldPointer),
    KDB_FIELD(EPROCESS, Win32Process, "PVOID", KdbFieldPointer),
    KDB_FIELD(EPROCESS, Job, "EJOB *", KdbFieldPointer),
    KDB_FIELD(EPROCESS, SectionBaseAddress, "PVOID", KdbFieldSymbol),
    KDB_FIELD(EPROCESS, InheritedFromUniqueProcessId, "HANDLE", KdbFieldPointer),
    KDB_FIELD(EPROCESS, ImageFileName, "CHAR[]", KdbFieldAnsi),
    KDB_FIELD(EPROCESS, ThreadListHead, "LIST_ENTRY", KdbFieldEmbedded),
    KDB_FIELD(EPROCESS, Peb, "PEB *", KdbFieldPointer),
    KDB_FIELD(EPROCESS, ActiveThreads, "ULONG", KdbFieldValue),
    KDB_FIELD(EPROCESS, ExitStatus, "NTSTATUS", KdbFieldValue)
};

static const KDB_TYPE_FIELD KdbpThreadFields[] =
{
    KDB_FIELD(ETHREAD, Tcb, "KTHREAD", KdbFieldEmbedded),
    KDB_FIELD(ETHREAD, CreateTime, "LARGE_INTEGER", KdbFieldValue),
    KDB_FIELD(ETHREAD, ExitTime, "LARGE_INTEGER", KdbFieldValue),
    KDB_FIELD(ETHREAD, Cid, "CLIENT_ID", KdbFieldEmbedded),
    KDB_FIELD(ETHREAD, StartAddress, "PVOID", KdbFieldSymbol),
    KDB_FIELD(ETHREAD, Win32StartAddress, "PVOID", KdbFieldSymbol),
    KDB_FIELD(ETHREAD, IrpList, "LIST_ENTRY", KdbFieldEmbedded),
    KDB_FIELD(ETHREAD, ThreadListEntry, "LIST_ENTRY", KdbFieldEmbedded)
};

static const KDB_TYPE_FIELD KdbpKernelThreadFields[] =
{
    KDB_FIELD(KTHREAD, Header, "DISPATCHER_HEADER", KdbFieldEmbedded),
    KDB_FIELD(KTHREAD, InitialStack, "PVOID", KdbFieldPointer),
    KDB_FIELD(KTHREAD, StackLimit, "PVOID", KdbFieldPointer),
    KDB_FIELD(KTHREAD, StackBase, "PVOID", KdbFieldPointer),
    KDB_FIELD(KTHREAD, KernelStack, "PVOID", KdbFieldPointer),
    KDB_FIELD(KTHREAD, Teb, "PVOID", KdbFieldPointer),
    KDB_FIELD(KTHREAD, State, "UCHAR", KdbFieldValue),
    KDB_FIELD(KTHREAD, WaitIrql, "KIRQL", KdbFieldValue),
    KDB_FIELD(KTHREAD, WaitMode, "KPROCESSOR_MODE", KdbFieldValue),
    KDB_FIELD(KTHREAD, WaitStatus, "LONG_PTR", KdbFieldValue),
    KDB_FIELD(KTHREAD, WaitBlockList, "KWAIT_BLOCK *", KdbFieldPointer),
    KDB_FIELD(KTHREAD, Priority, "SCHAR", KdbFieldValue),
    KDB_FIELD(KTHREAD, BasePriority, "SCHAR", KdbFieldValue),
    KDB_FIELD(KTHREAD, ApcState, "KAPC_STATE", KdbFieldEmbedded),
    KDB_FIELD(KTHREAD, NextProcessor, "ULONG", KdbFieldValue),
    KDB_FIELD(KTHREAD, Process, "KPROCESS *", KdbFieldPointer),
    KDB_FIELD(KTHREAD, ThreadListEntry, "LIST_ENTRY", KdbFieldEmbedded)
};

static const KDB_TYPE_FIELD KdbpPebFields[] =
{
    KDB_FIELD(PEB, BeingDebugged, "BOOLEAN", KdbFieldValue),
    KDB_FIELD(PEB, ImageBaseAddress, "PVOID", KdbFieldPointer),
    KDB_FIELD(PEB, Ldr, "PEB_LDR_DATA *", KdbFieldPointer),
    KDB_FIELD(PEB, ProcessParameters, "RTL_USER_PROCESS_PARAMETERS *", KdbFieldPointer),
    KDB_FIELD(PEB, ProcessHeap, "PVOID", KdbFieldPointer),
    KDB_FIELD(PEB, NumberOfProcessors, "ULONG", KdbFieldValue),
    KDB_FIELD(PEB, NtGlobalFlag, "ULONG", KdbFieldValue),
    KDB_FIELD(PEB, OSMajorVersion, "ULONG", KdbFieldValue),
    KDB_FIELD(PEB, OSMinorVersion, "ULONG", KdbFieldValue),
    KDB_FIELD(PEB, OSBuildNumber, "USHORT", KdbFieldValue),
    KDB_FIELD(PEB, SessionId, "ULONG", KdbFieldValue)
};

static const KDB_TYPE_FIELD KdbpTebFields[] =
{
    KDB_FIELD(TEB, NtTib, "NT_TIB", KdbFieldEmbedded),
    KDB_FIELD(TEB, EnvironmentPointer, "PVOID", KdbFieldPointer),
    KDB_FIELD(TEB, ClientId, "CLIENT_ID", KdbFieldEmbedded),
    KDB_FIELD(TEB, ThreadLocalStoragePointer, "PVOID", KdbFieldPointer),
    KDB_FIELD(TEB, ProcessEnvironmentBlock, "PEB *", KdbFieldPointer),
    KDB_FIELD(TEB, LastErrorValue, "ULONG", KdbFieldValue),
    KDB_FIELD(TEB, LastStatusValue, "NTSTATUS", KdbFieldValue)
};

static const KDB_TYPE_FIELD KdbpLoaderEntryFields[] =
{
    KDB_FIELD(LDR_DATA_TABLE_ENTRY, InLoadOrderLinks, "LIST_ENTRY", KdbFieldEmbedded),
    KDB_FIELD(LDR_DATA_TABLE_ENTRY, DllBase, "PVOID", KdbFieldPointer),
    KDB_FIELD(LDR_DATA_TABLE_ENTRY, EntryPoint, "PVOID", KdbFieldSymbol),
    KDB_FIELD(LDR_DATA_TABLE_ENTRY, SizeOfImage, "ULONG", KdbFieldValue),
    KDB_FIELD(LDR_DATA_TABLE_ENTRY, FullDllName, "UNICODE_STRING", KdbFieldUnicode),
    KDB_FIELD(LDR_DATA_TABLE_ENTRY, BaseDllName, "UNICODE_STRING", KdbFieldUnicode),
    KDB_FIELD(LDR_DATA_TABLE_ENTRY, Flags, "ULONG", KdbFieldValue),
    KDB_FIELD(LDR_DATA_TABLE_ENTRY, LoadCount, "USHORT", KdbFieldValue)
};

static const KDB_TYPE_FIELD KdbpDriverObjectFields[] =
{
    KDB_FIELD(DRIVER_OBJECT, Type, "CSHORT", KdbFieldValue),
    KDB_FIELD(DRIVER_OBJECT, Size, "CSHORT", KdbFieldValue),
    KDB_FIELD(DRIVER_OBJECT, DeviceObject, "DEVICE_OBJECT *", KdbFieldPointer),
    KDB_FIELD(DRIVER_OBJECT, Flags, "ULONG", KdbFieldValue),
    KDB_FIELD(DRIVER_OBJECT, DriverStart, "PVOID", KdbFieldPointer),
    KDB_FIELD(DRIVER_OBJECT, DriverSize, "ULONG", KdbFieldValue),
    KDB_FIELD(DRIVER_OBJECT, DriverExtension, "DRIVER_EXTENSION *", KdbFieldPointer),
    KDB_FIELD(DRIVER_OBJECT, DriverName, "UNICODE_STRING", KdbFieldUnicode),
    KDB_FIELD(DRIVER_OBJECT, DriverInit, "PDRIVER_INITIALIZE", KdbFieldSymbol),
    KDB_FIELD(DRIVER_OBJECT, DriverStartIo, "PDRIVER_STARTIO", KdbFieldSymbol),
    KDB_FIELD(DRIVER_OBJECT, DriverUnload, "PDRIVER_UNLOAD", KdbFieldSymbol),
    KDB_FIELD(DRIVER_OBJECT, MajorFunction, "PDRIVER_DISPATCH[]", KdbFieldEmbedded)
};

static const KDB_TYPE_FIELD KdbpDeviceObjectFields[] =
{
    KDB_FIELD(DEVICE_OBJECT, Type, "CSHORT", KdbFieldValue),
    KDB_FIELD(DEVICE_OBJECT, Size, "USHORT", KdbFieldValue),
    KDB_FIELD(DEVICE_OBJECT, ReferenceCount, "LONG", KdbFieldValue),
    KDB_FIELD(DEVICE_OBJECT, DriverObject, "DRIVER_OBJECT *", KdbFieldPointer),
    KDB_FIELD(DEVICE_OBJECT, NextDevice, "DEVICE_OBJECT *", KdbFieldPointer),
    KDB_FIELD(DEVICE_OBJECT, AttachedDevice, "DEVICE_OBJECT *", KdbFieldPointer),
    KDB_FIELD(DEVICE_OBJECT, CurrentIrp, "IRP *", KdbFieldPointer),
    KDB_FIELD(DEVICE_OBJECT, Flags, "ULONG", KdbFieldValue),
    KDB_FIELD(DEVICE_OBJECT, Characteristics, "ULONG", KdbFieldValue),
    KDB_FIELD(DEVICE_OBJECT, Vpb, "VPB *", KdbFieldPointer),
    KDB_FIELD(DEVICE_OBJECT, DeviceExtension, "PVOID", KdbFieldPointer),
    KDB_FIELD(DEVICE_OBJECT, DeviceType, "DEVICE_TYPE", KdbFieldValue),
    KDB_FIELD(DEVICE_OBJECT, StackSize, "CCHAR", KdbFieldValue),
    KDB_FIELD(DEVICE_OBJECT, AlignmentRequirement, "ULONG", KdbFieldValue),
    KDB_FIELD(DEVICE_OBJECT, DeviceObjectExtension, "DEVOBJ_EXTENSION *", KdbFieldPointer)
};

static const KDB_TYPE_FIELD KdbpIrpFields[] =
{
    KDB_FIELD(IRP, Type, "CSHORT", KdbFieldValue),
    KDB_FIELD(IRP, Size, "USHORT", KdbFieldValue),
    KDB_FIELD(IRP, MdlAddress, "MDL *", KdbFieldPointer),
    KDB_FIELD(IRP, Flags, "ULONG", KdbFieldValue),
    KDB_FIELD(IRP, AssociatedIrp, "union", KdbFieldPointer),
    KDB_FIELD(IRP, ThreadListEntry, "LIST_ENTRY", KdbFieldEmbedded),
    KDB_FIELD(IRP, IoStatus, "IO_STATUS_BLOCK", KdbFieldEmbedded),
    KDB_FIELD(IRP, RequestorMode, "KPROCESSOR_MODE", KdbFieldValue),
    KDB_FIELD(IRP, PendingReturned, "BOOLEAN", KdbFieldValue),
    KDB_FIELD(IRP, StackCount, "CHAR", KdbFieldValue),
    KDB_FIELD(IRP, CurrentLocation, "CHAR", KdbFieldValue),
    KDB_FIELD(IRP, Cancel, "BOOLEAN", KdbFieldValue),
    KDB_FIELD(IRP, CancelIrql, "KIRQL", KdbFieldValue),
    KDB_FIELD(IRP, UserIosb, "IO_STATUS_BLOCK *", KdbFieldPointer),
    KDB_FIELD(IRP, UserEvent, "KEVENT *", KdbFieldPointer),
    KDB_FIELD(IRP, CancelRoutine, "PDRIVER_CANCEL", KdbFieldSymbol),
    KDB_FIELD(IRP, UserBuffer, "PVOID", KdbFieldPointer),
    KDB_FIELD(IRP, Tail, "union", KdbFieldEmbedded)
};

static const KDB_TYPE_FIELD KdbpStackLocationFields[] =
{
    KDB_FIELD(IO_STACK_LOCATION, MajorFunction, "UCHAR", KdbFieldValue),
    KDB_FIELD(IO_STACK_LOCATION, MinorFunction, "UCHAR", KdbFieldValue),
    KDB_FIELD(IO_STACK_LOCATION, Flags, "UCHAR", KdbFieldValue),
    KDB_FIELD(IO_STACK_LOCATION, Control, "UCHAR", KdbFieldValue),
    KDB_FIELD(IO_STACK_LOCATION, Parameters, "union", KdbFieldEmbedded),
    KDB_FIELD(IO_STACK_LOCATION, DeviceObject, "DEVICE_OBJECT *", KdbFieldPointer),
    KDB_FIELD(IO_STACK_LOCATION, FileObject, "FILE_OBJECT *", KdbFieldPointer),
    KDB_FIELD(IO_STACK_LOCATION, CompletionRoutine, "PIO_COMPLETION_ROUTINE", KdbFieldSymbol),
    KDB_FIELD(IO_STACK_LOCATION, Context, "PVOID", KdbFieldPointer)
};

static const KDB_TYPE_FIELD KdbpFileObjectFields[] =
{
    KDB_FIELD(FILE_OBJECT, Type, "CSHORT", KdbFieldValue),
    KDB_FIELD(FILE_OBJECT, Size, "CSHORT", KdbFieldValue),
    KDB_FIELD(FILE_OBJECT, DeviceObject, "DEVICE_OBJECT *", KdbFieldPointer),
    KDB_FIELD(FILE_OBJECT, Vpb, "VPB *", KdbFieldPointer),
    KDB_FIELD(FILE_OBJECT, FsContext, "PVOID", KdbFieldPointer),
    KDB_FIELD(FILE_OBJECT, FsContext2, "PVOID", KdbFieldPointer),
    KDB_FIELD(FILE_OBJECT, SectionObjectPointer, "SECTION_OBJECT_POINTERS *", KdbFieldPointer),
    KDB_FIELD(FILE_OBJECT, FinalStatus, "NTSTATUS", KdbFieldValue),
    KDB_FIELD(FILE_OBJECT, RelatedFileObject, "FILE_OBJECT *", KdbFieldPointer),
    KDB_FIELD(FILE_OBJECT, Flags, "ULONG", KdbFieldValue),
    KDB_FIELD(FILE_OBJECT, FileName, "UNICODE_STRING", KdbFieldUnicode),
    KDB_FIELD(FILE_OBJECT, CurrentByteOffset, "LARGE_INTEGER", KdbFieldValue)
};

static const KDB_TYPE_FIELD KdbpMdlFields[] =
{
    KDB_FIELD(MDL, Next, "MDL *", KdbFieldPointer),
    KDB_FIELD(MDL, Size, "CSHORT", KdbFieldValue),
    KDB_FIELD(MDL, MdlFlags, "CSHORT", KdbFieldValue),
    KDB_FIELD(MDL, Process, "EPROCESS *", KdbFieldPointer),
    KDB_FIELD(MDL, MappedSystemVa, "PVOID", KdbFieldPointer),
    KDB_FIELD(MDL, StartVa, "PVOID", KdbFieldPointer),
    KDB_FIELD(MDL, ByteCount, "ULONG", KdbFieldValue),
    KDB_FIELD(MDL, ByteOffset, "ULONG", KdbFieldValue)
};

static const KDB_TYPE_FIELD KdbpObjectHeaderFields[] =
{
    KDB_FIELD(OBJECT_HEADER, PointerCount, "LONG_PTR", KdbFieldValue),
    KDB_FIELD(OBJECT_HEADER, HandleCount, "LONG_PTR", KdbFieldValue),
    KDB_FIELD(OBJECT_HEADER, TypeIndex, "UCHAR", KdbFieldValue),
    KDB_FIELD(OBJECT_HEADER, InfoMask, "UCHAR", KdbFieldValue),
    KDB_FIELD(OBJECT_HEADER, Flags, "UCHAR", KdbFieldValue),
    KDB_FIELD(OBJECT_HEADER, SecurityDescriptor, "PVOID", KdbFieldPointer),
    KDB_FIELD(OBJECT_HEADER, Body, "QUAD", KdbFieldEmbedded)
};

static const KDB_TYPE KdbpTypes[] =
{
    KDB_TYPE_ENTRY(LIST_ENTRY, KdbpListEntryFields),
    KDB_TYPE_ENTRY(UNICODE_STRING, KdbpUnicodeStringFields),
    KDB_TYPE_ENTRY(CLIENT_ID, KdbpClientIdFields),
    KDB_TYPE_ENTRY(EPROCESS, KdbpProcessFields),
    KDB_TYPE_ENTRY(ETHREAD, KdbpThreadFields),
    KDB_TYPE_ENTRY(KTHREAD, KdbpKernelThreadFields),
    KDB_TYPE_ENTRY(PEB, KdbpPebFields),
    KDB_TYPE_ENTRY(TEB, KdbpTebFields),
    KDB_TYPE_ENTRY(LDR_DATA_TABLE_ENTRY, KdbpLoaderEntryFields),
    KDB_TYPE_ENTRY(DRIVER_OBJECT, KdbpDriverObjectFields),
    KDB_TYPE_ENTRY(DEVICE_OBJECT, KdbpDeviceObjectFields),
    KDB_TYPE_ENTRY(IRP, KdbpIrpFields),
    KDB_TYPE_ENTRY(IO_STACK_LOCATION, KdbpStackLocationFields),
    KDB_TYPE_ENTRY(FILE_OBJECT, KdbpFileObjectFields),
    KDB_TYPE_ENTRY(MDL, KdbpMdlFields),
    KDB_TYPE_ENTRY(OBJECT_HEADER, KdbpObjectHeaderFields)
};

/* PRIVATE FUNCTIONS **********************************************************/

static
const KDB_TYPE *
KdbpFindType(
    _In_ PCSTR Name)
{
    PCSTR Bang = strchr(Name, '!');
    ULONG Index;

    if (Bang != NULL)
        Name = Bang + 1;

    if (*Name == '_')
        Name++;

    for (Index = 0; Index < RTL_NUMBER_OF(KdbpTypes); Index++)
    {
        if (_stricmp(KdbpTypes[Index].Name, Name) == 0)
            return &KdbpTypes[Index];
    }

    return NULL;
}

static
VOID
KdbpPrintFieldValue(
    _In_ PUCHAR Address,
    _In_ const KDB_TYPE_FIELD *Field)
{
    ULONG64 Value = 0;

    switch (Field->Kind)
    {
        case KdbFieldUnicode:
        {
            UNICODE_STRING String;

            if (!NT_SUCCESS(KdbpSafeReadMemory(&String, Address, sizeof(String))))
            {
                KdbPuts("??");
                return;
            }

            KdbPuts("\"");
            KdbpPrintCountedString(&String);
            KdbPuts("\"");
            return;
        }

        case KdbFieldAnsi:
        {
            CHAR Text[32];
            ULONG Length = min(Field->Size, (ULONG)sizeof(Text) - 1);

            if (!NT_SUCCESS(KdbpSafeReadMemory(Text, Address, Length)))
            {
                KdbPuts("??");
                return;
            }

            Text[Length] = ANSI_NULL;
            KdbpPrint("\"%s\"", Text);
            return;
        }

        case KdbFieldEmbedded:
            KdbpPrint("@ %p", Address);
            return;

        default:
            break;
    }

    if ((Field->Size > sizeof(Value)) || !NT_SUCCESS(KdbpSafeReadMemory(&Value, Address, Field->Size)))
    {
        KdbPuts("??");
        return;
    }

    if (Field->Kind == KdbFieldValue)
    {
        KdbpPrint("0x%I64x", Value);
        return;
    }

    KdbpPrint("%p", (PVOID)(ULONG_PTR)Value);

    if ((Field->Kind == KdbFieldSymbol) && (Value != 0))
    {
        KdbPuts(" ");
        KdbSymPrintAddress((PVOID)(ULONG_PTR)Value, NULL);
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

BOOLEAN
KdbpCmdDisplayType(
    ULONG Argc,
    PCHAR Argv[])
{
    const KDB_TYPE *Type;
    ULONG_PTR Address = 0;
    ULONG Index;

    if (Argc < 2)
    {
        for (Index = 0; Index < RTL_NUMBER_OF(KdbpTypes); Index++)
            KdbpPrint("%s (0x%lx bytes)\n", KdbpTypes[Index].Name, KdbpTypes[Index].Size);

        return TRUE;
    }

    Type = KdbpFindType(Argv[1]);
    if (Type == NULL)
    {
        KdbpPrint("The debugger does not know the layout of %s; dt alone lists those it knows\n", Argv[1]);
        return TRUE;
    }

    if ((Argc > 2) && !KdbpGetAddressExpression(Argv[2], &Address))
        return TRUE;

    if (Address == 0)
        KdbpPrint("%s (0x%lx bytes)\n", Type->Name, Type->Size);
    else
        KdbpPrint("%s at %p\n", Type->Name, (PVOID)Address);

    for (Index = 0; (Index < Type->FieldCount) && !KdbpIsOutputAborted(); Index++)
    {
        const KDB_TYPE_FIELD *Field = &Type->Fields[Index];

        KdbpPrint("  +0x%03lx %-28s ", Field->Offset, Field->Name);

        if (Address == 0)
            KdbpPrint("%s\n", Field->Type);
        else
        {
            KdbpPrintFieldValue((PUCHAR)Address + Field->Offset, Field);
            KdbPuts("\n");
        }
    }

    return TRUE;
}

/* EOF */
