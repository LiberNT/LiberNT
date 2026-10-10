/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Carry guest system services to the native system service table
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "amd64emu.h"
#include "signature.h"

#define SVC_WRAP_(Name, Count) SVC_(Name, Count)

enum
{
#define SVC_(Name, Count) EmuNt##Name,
#include <sysfuncs.h>
#undef SVC_
    EmuNtServiceCount
};

static const UCHAR EmuNtArguments[] =
{
#define SVC_(Name, Count) Count,
#include <sysfuncs.h>
#undef SVC_
};

static const UCHAR EmuWin32kArguments[] =
{
#define SVC_(Name, Count) Count,
#include <w32ksvc64.h>
#undef SVC_
};

#define SVC_(Name, Count) C_ASSERT(Count <= EMU_MAX_SERVICE_ARGUMENTS);
#include <sysfuncs.h>
#include <w32ksvc64.h>
#undef SVC_

C_ASSERT(RTL_NUMBER_OF(EmuNtArguments) == EmuNtServiceCount);

static
BOOLEAN
EmuIsCurrentProcess(
    _In_ HANDLE ProcessHandle)
{
    PROCESS_BASIC_INFORMATION Information;

    if (ProcessHandle == NtCurrentProcess()) return TRUE;
    if (!NT_SUCCESS(NtQueryInformationProcess(ProcessHandle,
                                              ProcessBasicInformation,
                                              &Information,
                                              sizeof(Information),
                                              NULL)))
    {
        return FALSE;
    }

    return Information.UniqueProcessId == (ULONG_PTR)NtCurrentTeb()->ClientId.UniqueProcess;
}

static
BOOLEAN
EmuIsForeignImage(
    _In_ PVOID Base)
{
    MEMORY_BASIC_INFORMATION Information;
    PIMAGE_NT_HEADERS Headers;

    if (!NT_SUCCESS(NtQueryVirtualMemory(NtCurrentProcess(),
                                         Base,
                                         MemoryBasicInformation,
                                         &Information,
                                         sizeof(Information),
                                         NULL)) ||
        Information.Type != MEM_IMAGE ||
        Information.AllocationBase != Base)
    {
        return FALSE;
    }

    Headers = RtlImageNtHeader(Base);
    return Headers && Headers->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64;
}

static
VOID
EmuQueryView(
    _In_ PVOID Address,
    _Out_ PVOID *Base,
    _Out_ PSIZE_T Size)
{
    MEMORY_BASIC_INFORMATION Information;
    PVOID AllocationBase;
    PUCHAR Current;

    *Base = NULL;
    *Size = 0;
    if (!NT_SUCCESS(NtQueryVirtualMemory(NtCurrentProcess(),
                                         Address,
                                         MemoryBasicInformation,
                                         &Information,
                                         sizeof(Information),
                                         NULL)) ||
        Information.State == MEM_FREE)
    {
        return;
    }

    AllocationBase = Information.AllocationBase;
    Current = AllocationBase;
    while (NT_SUCCESS(NtQueryVirtualMemory(NtCurrentProcess(),
                                           Current,
                                           MemoryBasicInformation,
                                           &Information,
                                           sizeof(Information),
                                           NULL)) &&
           Information.State != MEM_FREE &&
           Information.AllocationBase == AllocationBase)
    {
        Current += Information.RegionSize;
    }

    *Base = AllocationBase;
    *Size = Current - (PUCHAR)AllocationBase;
}

static
VOID
EmuReportProcessor(
    _In_ ULONG Class,
    _In_ PVOID Buffer,
    _In_ ULONG Length)
{
    PSYSTEM_PROCESSOR_INFORMATION Information = Buffer;

    if ((Class != SystemProcessorInformation && Class != SystemEmulationProcessorInformation) ||
        Length < sizeof(*Information))
    {
        return;
    }

    Information->ProcessorArchitecture = PROCESSOR_ARCHITECTURE_AMD64;
    EmuCoreGetProcessor(&Information->ProcessorLevel, &Information->ProcessorRevision);
}

static
NTSTATUS
EmuNtService(
    _In_ PEMU_THREAD Thread,
    _In_ ULONG Service,
    _Inout_ PULONG_PTR Arguments,
    _Out_ PBOOLEAN Resumed)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Name;
    WCHAR NameBuffer[MAX_PATH + 16];
    PVOID Base = NULL;
    SIZE_T Size = 0;
    NTSTATUS Status;

    switch (Service)
    {
        case EmuNtContinue:
        case EmuNtContinueEx:
            Status = EmuContinue(Thread, (const AMD64_CONTEXT *)Arguments[0]);
            *Resumed = NT_SUCCESS(Status);
            return Status;

        case EmuNtRaiseException:
            Status = EmuRaiseException(Thread,
                                       (const EXCEPTION_RECORD *)Arguments[0],
                                       (const AMD64_CONTEXT *)Arguments[1],
                                       (BOOLEAN)Arguments[2]);
            *Resumed = NT_SUCCESS(Status);
            return Status;

        case EmuNtCreateThread:
            return EmuCreateThread(Service, Arguments);

        case EmuNtTerminateThread:
            if (!Arguments[0] || (HANDLE)Arguments[0] == NtCurrentThread())
                EmuExitThread(Thread, (NTSTATUS)Arguments[1]);
            break;

        case EmuNtGetContextThread:
        case EmuNtSetContextThread:
        case EmuNtQueueApcThread:
        case EmuNtQueueApcThreadEx:
        case EmuNtQueueApcThreadEx2:
            return STATUS_NOT_IMPLEMENTED;

        case EmuNtCallbackReturn:
            return STATUS_NO_CALLBACK_ACTIVE;

        case EmuNtReadFile:
        case EmuNtWriteFile:
        case EmuNtReadFileScatter:
        case EmuNtWriteFileGather:
        case EmuNtDeviceIoControlFile:
        case EmuNtFsControlFile:
        case EmuNtLockFile:
        case EmuNtNotifyChangeDirectoryFile:
        case EmuNtNotifyChangeDirectoryFileEx:
        case EmuNtNotifyChangeKey:
        case EmuNtSetTimer:
            if (Arguments[2]) return STATUS_NOT_IMPLEMENTED;
            break;

        case EmuNtNotifyChangeMultipleKeys:
            if (Arguments[4]) return STATUS_NOT_IMPLEMENTED;
            break;

        case EmuNtOpenDirectoryObject:
            if (EmuIsKnownDllDirectory((POBJECT_ATTRIBUTES)Arguments[2])) return STATUS_OBJECT_NAME_NOT_FOUND;
            break;

        case EmuNtCreateFile:
        case EmuNtOpenFile:
            if (EmuRedirectObject((POBJECT_ATTRIBUTES)Arguments[2],
                                  &ObjectAttributes,
                                  &Name,
                                  NameBuffer,
                                  sizeof(NameBuffer)))
            {
                Arguments[2] = (ULONG_PTR)&ObjectAttributes;
            }
            break;

        case EmuNtQueryAttributesFile:
        case EmuNtQueryFullAttributesFile:
            if (EmuRedirectObject((POBJECT_ATTRIBUTES)Arguments[0],
                                  &ObjectAttributes,
                                  &Name,
                                  NameBuffer,
                                  sizeof(NameBuffer)))
            {
                Arguments[0] = (ULONG_PTR)&ObjectAttributes;
            }
            break;

        case EmuNtUnmapViewOfSection:
        case EmuNtUnmapViewOfSectionEx:
            if (EmuIsCurrentProcess((HANDLE)Arguments[0])) EmuQueryView((PVOID)Arguments[1], &Base, &Size);
            break;
    }

    Status = (NTSTATUS)EmuInvokeSystemService(Service, Arguments);
    if (!NT_SUCCESS(Status)) return Status;

    switch (Service)
    {
        case EmuNtUnmapViewOfSection:
        case EmuNtUnmapViewOfSectionEx:
            if (Size) EmuCoreInvalidate(Base, Size);
            break;

        case EmuNtMapViewOfSection:
            if (EmuIsCurrentProcess((HANDLE)Arguments[1]) && EmuIsForeignImage(*(PVOID *)Arguments[2]))
                return STATUS_IMAGE_MACHINE_TYPE_MISMATCH;
            break;

        case EmuNtFreeVirtualMemory:
        case EmuNtProtectVirtualMemory:
            if (EmuIsCurrentProcess((HANDLE)Arguments[0]))
                EmuCoreInvalidate(*(PVOID *)Arguments[1], *(PSIZE_T)Arguments[2]);
            break;

        case EmuNtFlushInstructionCache:
        case EmuNtWriteVirtualMemory:
            if (EmuIsCurrentProcess((HANDLE)Arguments[0]))
            {
                if (Service == EmuNtFlushInstructionCache && !Arguments[1])
                    EmuCoreInvalidate(NULL, EMU_ADDRESS_SPACE_SIZE);
                else
                    EmuCoreInvalidate((PVOID)Arguments[1], Service == EmuNtFlushInstructionCache ? Arguments[2] : Arguments[3]);
            }
            break;

        case EmuNtQuerySystemInformation:
            EmuReportProcessor((ULONG)Arguments[0], (PVOID)Arguments[1], (ULONG)Arguments[2]);
            break;
    }

    return Status;
}

VOID
EmuSystemService(
    _In_ PVOID Owner,
    _Inout_ PEMU_REGISTERS Registers)
{
    PEMU_THREAD Thread = Owner;
    ULONG_PTR Arguments[EMU_MAX_SERVICE_ARGUMENTS];
    ULONG Service = (ULONG)Registers->Gpr[EmuRax];
    ULONG Table = Service >> 12;
    ULONG Index = Service & 0xFFF;
    ULONG Count, Argument;
    ULONG64 Signature = 0;
    ULONG_PTR Result;
    BOOLEAN Resumed = FALSE;

    if (Table == 0 && Index < EmuNtServiceCount)
    {
        Count = EmuNtArguments[Index];
        Signature = EmuNtSignatures[Index];
    }
    else if (Table == 1 && Index < RTL_NUMBER_OF(EmuWin32kArguments))
    {
        Count = EmuWin32kArguments[Index];
    }
    else
    {
        Registers->Gpr[EmuRax] = (ULONG)STATUS_INVALID_SYSTEM_SERVICE;
        return;
    }

    RtlZeroMemory(Arguments, sizeof(Arguments));
    Arguments[0] = Registers->Gpr[EmuR10];
    Arguments[1] = Registers->Gpr[EmuRdx];
    Arguments[2] = Registers->Gpr[EmuR8];
    Arguments[3] = Registers->Gpr[EmuR9];

    Thread->HostDepth++;
    _SEH2_TRY
    {
        for (Argument = 4; Argument < Count; Argument++)
            Arguments[Argument] = ((PULONG_PTR)Registers->Gpr[EmuRsp])[Argument + 1];

        for (Argument = 0; Signature; Argument++, Signature >>= EMU_ARGUMENT_BITS)
        {
            switch (Signature & EMU_ARGUMENT_MASK)
            {
                case EMU_ARGUMENT_INT32:
                    Arguments[Argument] = (ULONG_PTR)(LONG)Arguments[Argument];
                    break;
                case EMU_ARGUMENT_INT16:
                    Arguments[Argument] = (ULONG_PTR)(SHORT)Arguments[Argument];
                    break;
                case EMU_ARGUMENT_INT8:
                    Arguments[Argument] = (ULONG_PTR)(CHAR)Arguments[Argument];
                    break;
                case EMU_ARGUMENT_UINT16:
                    Arguments[Argument] = (USHORT)Arguments[Argument];
                    break;
                case EMU_ARGUMENT_UINT8:
                    Arguments[Argument] = (UCHAR)Arguments[Argument];
                    break;
            }
        }

        if (Table)
            Result = EmuInvokeSystemService(Service, Arguments);
        else
            Result = (ULONG)EmuNtService(Thread, Index, Arguments, &Resumed);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Result = (ULONG)_SEH2_GetExceptionCode();
    }
    _SEH2_END;
    Thread->HostDepth--;

    if (!Resumed) Registers->Gpr[EmuRax] = Result;
}
