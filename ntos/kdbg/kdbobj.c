/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The debugger's views of processes, objects, drivers, devices, IRPs and files
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kdb.h"
#include "kdbpriv.h"
#define NDEBUG
#include <debug.h>

#define KDB_MAXIMUM_NAME_CHARS 128

/* PRIVATE FUNCTIONS **********************************************************/

static
BOOLEAN
KdbpRead(
    _In_ PVOID Address,
    _Out_writes_bytes_(Size) PVOID Buffer,
    _In_ ULONG Size,
    _In_ PCSTR What)
{
    if (NT_SUCCESS(KdbpSafeReadMemory(Buffer, Address, Size)))
        return TRUE;

    KdbpPrint("The %s at %p cannot be read\n", What, Address);
    return FALSE;
}

static
BOOLEAN
KdbpAddressArgument(
    _In_ ULONG Argc,
    _In_ PCHAR Argv[],
    _In_opt_ PVOID Default,
    _Out_ PVOID *Address)
{
    ULONG_PTR Value;

    *Address = Default;

    if (Argc < 2)
    {
        if (Default == NULL)
            KdbPuts("An address is needed\n");

        return (Default != NULL);
    }

    if (!KdbpGetAddressExpression(Argv[1], &Value))
        return FALSE;

    *Address = (PVOID)Value;
    return TRUE;
}

VOID
KdbpPrintCountedString(
    _In_ PCUNICODE_STRING String)
{
    WCHAR Char;
    CHAR Text[2];
    USHORT Index;

    for (Index = 0; (Index < String->Length / sizeof(WCHAR)) && (Index < KDB_MAXIMUM_NAME_CHARS); Index++)
    {
        if (!NT_SUCCESS(KdbpSafeReadMemory(&Char, &String->Buffer[Index], sizeof(Char))))
        {
            KdbPuts("?");
            break;
        }

        Text[0] = ((Char >= L' ') && (Char < 0x7F)) ? (CHAR)Char : '.';
        Text[1] = ANSI_NULL;
        KdbPuts(Text);
    }
}

static
VOID
KdbpPrintObjectName(
    _In_ PVOID Object)
{
    OBJECT_HEADER Header;
    OBJECT_HEADER_NAME_INFO NameInfo;
    POBJECT_HEADER_NAME_INFO NameInfoAddress;

    if (!NT_SUCCESS(KdbpSafeReadMemory(&Header, OBJECT_TO_OBJECT_HEADER(Object), sizeof(Header))))
        return;

    NameInfoAddress = ObQueryNameInfo(Object);
    if ((NameInfoAddress == NULL) ||
        !NT_SUCCESS(KdbpSafeReadMemory(&NameInfo, NameInfoAddress, sizeof(NameInfo))) ||
        (NameInfo.Name.Length == 0))
    {
        return;
    }

    KdbpPrintCountedString(&NameInfo.Name);
}

static
VOID
KdbpPrintDriverName(
    _In_opt_ PDRIVER_OBJECT DriverObject)
{
    DRIVER_OBJECT Driver;

    if ((DriverObject == NULL) ||
        !NT_SUCCESS(KdbpSafeReadMemory(&Driver, DriverObject, sizeof(Driver))))
    {
        KdbPuts("(none)");
        return;
    }

    KdbpPrintCountedString(&Driver.DriverName);
}

/* PUBLIC FUNCTIONS ***********************************************************/

BOOLEAN
KdbpCmdProcessDetail(
    ULONG Argc,
    PCHAR Argv[])
{
    static EPROCESS Process;
    PVOID Address;

    if (!KdbpAddressArgument(Argc, Argv, KdbCurrentProcess, &Address) ||
        !KdbpRead(Address, &Process, sizeof(Process), "process"))
    {
        return TRUE;
    }

    KdbpPrint("Process %p  Cid %p  parent %p  %.15s\n",
              Address,
              Process.UniqueProcessId,
              Process.InheritedFromUniqueProcessId,
              Process.ImageFileName);
    KdbpPrint("  Peb %p, %lu active threads, exit status 0x%08lx\n",
              Process.Peb,
              Process.ActiveThreads,
              Process.ExitStatus);

    return TRUE;
}

BOOLEAN
KdbpCmdTeb(
    ULONG Argc,
    PCHAR Argv[])
{
    TEB Teb;
    PVOID Address;

    if (!KdbpAddressArgument(Argc, Argv,
                             (KdbCurrentThread != NULL) ? KdbCurrentThread->Tcb.Teb : NULL,
                             &Address) ||
        !KdbpRead(Address, &Teb, sizeof(Teb), "TEB"))
    {
        return TRUE;
    }

    KdbpPrint("TEB %p  Cid %p.%p\n", Address, Teb.ClientId.UniqueProcess, Teb.ClientId.UniqueThread);
    KdbpPrint("  stack base %p, limit %p, last error %lu, PEB %p\n",
              Teb.NtTib.StackBase,
              Teb.NtTib.StackLimit,
              Teb.LastErrorValue,
              Teb.ProcessEnvironmentBlock);

    return TRUE;
}

BOOLEAN
KdbpCmdPeb(
    ULONG Argc,
    PCHAR Argv[])
{
    RTL_USER_PROCESS_PARAMETERS Parameters;
    PEB Peb;
    PVOID Address;

    if (!KdbpAddressArgument(Argc, Argv,
                             (KdbCurrentProcess != NULL) ? KdbCurrentProcess->Peb : NULL,
                             &Address) ||
        !KdbpRead(Address, &Peb, sizeof(Peb), "PEB"))
    {
        return TRUE;
    }

    KdbpPrint("PEB %p  image base %p, loader data %p, parameters %p, being debugged %u\n",
              Address,
              Peb.ImageBaseAddress,
              Peb.Ldr,
              Peb.ProcessParameters,
              Peb.BeingDebugged);

    if ((Peb.ProcessParameters != NULL) &&
        NT_SUCCESS(KdbpSafeReadMemory(&Parameters, Peb.ProcessParameters, sizeof(Parameters))))
    {
        KdbPuts("  image ");
        KdbpPrintCountedString(&Parameters.ImagePathName);
        KdbPuts("\n  command line ");
        KdbpPrintCountedString(&Parameters.CommandLine);
        KdbPuts("\n");
    }

    return TRUE;
}

BOOLEAN
KdbpCmdObjectHeader(
    ULONG Argc,
    PCHAR Argv[])
{
    OBJECT_HEADER Header;
    OBJECT_TYPE Type;
    POBJECT_TYPE TypeAddress;
    PVOID Object;

    if (!KdbpAddressArgument(Argc, Argv, NULL, &Object) ||
        !KdbpRead(OBJECT_TO_OBJECT_HEADER(Object), &Header, sizeof(Header), "object header"))
    {
        return TRUE;
    }

    KdbpPrint("Object %p  header %p  pointers %Id, handles %Id, type ",
              Object,
              OBJECT_TO_OBJECT_HEADER(Object),
              Header.PointerCount,
              Header.HandleCount);

    TypeAddress = ObGetObjectType(Object);
    if ((TypeAddress != NULL) &&
        NT_SUCCESS(KdbpSafeReadMemory(&Type, TypeAddress, sizeof(Type))))
    {
        KdbpPrintCountedString(&Type.Name);
    }

    KdbPuts(", name ");
    KdbpPrintObjectName(Object);
    KdbPuts("\n");

    return TRUE;
}

BOOLEAN
KdbpCmdDriverObject(
    ULONG Argc,
    PCHAR Argv[])
{
    DRIVER_OBJECT Driver;
    DEVICE_OBJECT Device;
    PDEVICE_OBJECT Next;
    PVOID Address;
    ULONG Index;
    ULONG Last;

    if (!KdbpAddressArgument(Argc, Argv, NULL, &Address) ||
        !KdbpRead(Address, &Driver, sizeof(Driver), "driver object"))
    {
        return TRUE;
    }

    KdbpPrint("Driver %p ", Address);
    KdbpPrintCountedString(&Driver.DriverName);
    KdbpPrint("\n  image %p size 0x%lx, unload %p\n", Driver.DriverStart, Driver.DriverSize, Driver.DriverUnload);

    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; Index = Last + 1)
    {
        for (Last = Index;
             (Last < IRP_MJ_MAXIMUM_FUNCTION) && (Driver.MajorFunction[Last + 1] == Driver.MajorFunction[Index]);
             Last++)
        {
            NOTHING;
        }

        if (Driver.MajorFunction[Index] == NULL)
            continue;

        KdbpPrint("  [%02lx-%02lx] %p ", Index, Last, (PVOID)Driver.MajorFunction[Index]);
        KdbSymPrintAddress((PVOID)Driver.MajorFunction[Index], NULL);
        KdbPuts("\n");
    }

    for (Next = Driver.DeviceObject, Index = 0; (Next != NULL) && (Index < 64); Index++)
    {
        if (!NT_SUCCESS(KdbpSafeReadMemory(&Device, Next, sizeof(Device))))
            break;

        KdbpPrint("  device %p ", Next);
        KdbpPrintObjectName(Next);
        KdbPuts("\n");
        Next = Device.NextDevice;
    }

    return TRUE;
}

BOOLEAN
KdbpCmdDeviceObject(
    ULONG Argc,
    PCHAR Argv[])
{
    DEVICE_OBJECT Device;
    PVOID Address;

    if (!KdbpAddressArgument(Argc, Argv, NULL, &Address) ||
        !KdbpRead(Address, &Device, sizeof(Device), "device object"))
    {
        return TRUE;
    }

    KdbpPrint("Device %p ", Address);
    KdbpPrintObjectName(Address);
    KdbPuts(" of ");
    KdbpPrintDriverName(Device.DriverObject);
    KdbpPrint("\n  type 0x%lx, flags 0x%08lx, characteristics 0x%08lx, stack size %d\n",
              Device.DeviceType,
              Device.Flags,
              Device.Characteristics,
              Device.StackSize);
    KdbpPrint("  extension %p, attached %p, next %p, current IRP %p\n",
              Device.DeviceExtension,
              Device.AttachedDevice,
              Device.NextDevice,
              Device.CurrentIrp);

    return TRUE;
}

BOOLEAN
KdbpCmdDeviceStack(
    ULONG Argc,
    PCHAR Argv[])
{
    DEVOBJ_EXTENSION Extension;
    DEVICE_OBJECT Device;
    PDEVICE_OBJECT Current;
    PVOID Address;
    ULONG Index;

    if (!KdbpAddressArgument(Argc, Argv, NULL, &Address))
        return TRUE;

    Current = Address;
    for (Index = 0; Index < 32; Index++)
    {
        if (!NT_SUCCESS(KdbpSafeReadMemory(&Device, Current, sizeof(Device))) ||
            (Device.AttachedDevice == NULL))
        {
            break;
        }

        Current = Device.AttachedDevice;
    }

    for (Index = 0; (Current != NULL) && (Index < 32); Index++)
    {
        if (!NT_SUCCESS(KdbpSafeReadMemory(&Device, Current, sizeof(Device))))
            break;

        KdbpPrint("%s%p ", (Current == Address) ? ">" : " ", Current);
        KdbpPrintDriverName(Device.DriverObject);
        KdbPuts(" ");
        KdbpPrintObjectName(Current);
        KdbPuts("\n");

        if ((Device.DeviceObjectExtension == NULL) ||
            !NT_SUCCESS(KdbpSafeReadMemory(&Extension, Device.DeviceObjectExtension, sizeof(Extension))))
        {
            break;
        }

        Current = Extension.AttachedTo;
    }

    return TRUE;
}

BOOLEAN
KdbpCmdIrp(
    ULONG Argc,
    PCHAR Argv[])
{
    IO_STACK_LOCATION Stack;
    PIO_STACK_LOCATION Location;
    IRP Irp;
    PVOID Address;
    LONG Index;

    if (!KdbpAddressArgument(Argc, Argv, NULL, &Address) ||
        !KdbpRead(Address, &Irp, sizeof(Irp), "IRP"))
    {
        return TRUE;
    }

    KdbpPrint("IRP %p  flags 0x%08lx, status 0x%08lx, information 0x%Ix, mode %u, cancel %u\n",
              Address,
              Irp.Flags,
              Irp.IoStatus.Status,
              Irp.IoStatus.Information,
              Irp.RequestorMode,
              Irp.Cancel);
    KdbpPrint("  location %d of %d, thread %p, MDL %p, system buffer %p\n",
              Irp.CurrentLocation,
              Irp.StackCount,
              Irp.Tail.Overlay.Thread,
              Irp.MdlAddress,
              Irp.AssociatedIrp.SystemBuffer);

    Location = (PIO_STACK_LOCATION)((PIRP)Address + 1);
    for (Index = 0; (Index < Irp.StackCount) && (Index < 32); Index++)
    {
        if (!NT_SUCCESS(KdbpSafeReadMemory(&Stack, &Location[Index], sizeof(Stack))))
            break;

        KdbpPrint("  %s[%d] major 0x%02x minor 0x%02x, device %p, file %p, completion %p ",
                  (Index + 1 == Irp.CurrentLocation) ? ">" : " ",
                  (int)(Index + 1),
                  Stack.MajorFunction,
                  Stack.MinorFunction,
                  Stack.DeviceObject,
                  Stack.FileObject,
                  (PVOID)Stack.CompletionRoutine);
        if (Stack.CompletionRoutine != NULL)
            KdbSymPrintAddress((PVOID)Stack.CompletionRoutine, NULL);
        KdbPuts("\n");
    }

    return TRUE;
}

BOOLEAN
KdbpCmdFileObject(
    ULONG Argc,
    PCHAR Argv[])
{
    FILE_OBJECT File;
    PVOID Address;

    if (!KdbpAddressArgument(Argc, Argv, NULL, &Address) ||
        !KdbpRead(Address, &File, sizeof(File), "file object"))
    {
        return TRUE;
    }

    KdbpPrint("File %p ", Address);
    KdbpPrintCountedString(&File.FileName);
    KdbpPrint("\n  device %p, VPB %p, context %p %p, flags 0x%08lx, offset 0x%I64x\n",
              File.DeviceObject,
              File.Vpb,
              File.FsContext,
              File.FsContext2,
              File.Flags,
              File.CurrentByteOffset.QuadPart);
    KdbpPrint("  access read %u write %u delete %u, share read %u write %u delete %u\n",
              File.ReadAccess,
              File.WriteAccess,
              File.DeleteAccess,
              File.SharedRead,
              File.SharedWrite,
              File.SharedDelete);

    return TRUE;
}

/* EOF */
