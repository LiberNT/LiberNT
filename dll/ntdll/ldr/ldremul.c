/*
 * PROJECT:     LiberNT NT Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Start a native emulation host beside a foreign process image
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntdll.h>
#include <reactos/emulation.h>

#define NDEBUG
#include <debug.h>

#define LDRP_EMULATION_UNKNOWN  0
#define LDRP_EMULATION_STARTING 1
#define LDRP_EMULATION_NATIVE   2
#define LDRP_EMULATION_HOSTED   3

typedef VOID (NTAPI *PEMULATION_HOST_ENTRY)(_In_ PEMULATION_THREAD Thread);

static LONG LdrpEmulationState;
static PPEB LdrpEmulationHostPeb;
static PEMULATION_HOST_ENTRY LdrpEmulationHostEntry;
static SIZE_T LdrpEmulationStackSize;
static PVOID LdrpEmulationNtdllBase;

static
NTSTATUS
LdrpMapEmulationHost(
    _In_ PCWSTR ImageName,
    _Out_ PVOID *ImageBase)
{
    WCHAR Buffer[MAX_PATH];
    UNICODE_STRING Path;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE FileHandle, SectionHandle;
    PIMAGE_NT_HEADERS Headers;
    SIZE_T ViewSize = 0;
    NTSTATUS Status;

    *ImageBase = NULL;
    RtlInitEmptyUnicodeString(&Path, Buffer, sizeof(Buffer));
    Status = RtlAppendUnicodeToString(&Path, L"\\??\\");
    if (NT_SUCCESS(Status)) Status = RtlAppendUnicodeToString(&Path, SharedUserData->NtSystemRoot);
    if (NT_SUCCESS(Status)) Status = RtlAppendUnicodeToString(&Path, L"\\System32\\");
    if (NT_SUCCESS(Status)) Status = RtlAppendUnicodeToString(&Path, ImageName);
    if (!NT_SUCCESS(Status)) return Status;

    InitializeObjectAttributes(&ObjectAttributes, &Path, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&FileHandle,
                        FILE_EXECUTE | FILE_READ_DATA | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_DELETE,
                        FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status)) return Status;

    Status = NtCreateSection(&SectionHandle, SECTION_ALL_ACCESS, NULL, NULL, PAGE_EXECUTE, SEC_IMAGE, FileHandle);
    NtClose(FileHandle);
    if (!NT_SUCCESS(Status)) return Status;

    Status = NtMapViewOfSection(SectionHandle,
                                NtCurrentProcess(),
                                ImageBase,
                                0,
                                0,
                                NULL,
                                &ViewSize,
                                ViewShare,
                                0,
                                PAGE_EXECUTE_READ);
    NtClose(SectionHandle);
    if (!NT_SUCCESS(Status)) return Status;

    Headers = RtlImageNtHeader(*ImageBase);
    if (!Headers ||
        Headers->FileHeader.Machine != IMAGE_FILE_MACHINE_NATIVE ||
        Headers->OptionalHeader.Subsystem != IMAGE_SUBSYSTEM_NATIVE)
    {
        NtUnmapViewOfSection(NtCurrentProcess(), *ImageBase);
        *ImageBase = NULL;
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
LdrpCreateEmulationHost(
    _In_ PPEB Peb,
    _In_ PCWSTR ImageName)
{
    PRTL_USER_PROCESS_PARAMETERS Parameters = NULL;
    PIMAGE_NT_HEADERS Headers;
    PPEB HostPeb = NULL;
    PWSTR Environment, End;
    SIZE_T EnvironmentSize = 0;
    PVOID ImageBase;
    SIZE_T Size;
    NTSTATUS Status;

    Status = LdrpMapEmulationHost(ImageName, &ImageBase);
    if (!NT_SUCCESS(Status)) return Status;

    Size = PAGE_SIZE;
    Status = NtAllocateVirtualMemory(NtCurrentProcess(),
                                     (PVOID *)&HostPeb,
                                     0,
                                     &Size,
                                     MEM_COMMIT | MEM_RESERVE,
                                     PAGE_READWRITE);
    if (!NT_SUCCESS(Status)) goto Failure;

    RtlNormalizeProcessParams(Peb->ProcessParameters);
    Environment = Peb->ProcessParameters->Environment;
    if (Environment)
    {
        End = Environment;
        while (*End) End += wcslen(End) + 1;
        EnvironmentSize = (End + 1 - Environment) * sizeof(WCHAR);
    }

    Size = sizeof(*Parameters) + EnvironmentSize;
    Status = NtAllocateVirtualMemory(NtCurrentProcess(),
                                     (PVOID *)&Parameters,
                                     0,
                                     &Size,
                                     MEM_COMMIT | MEM_RESERVE,
                                     PAGE_READWRITE);
    if (!NT_SUCCESS(Status)) goto Failure;

    *Parameters = *Peb->ProcessParameters;
    if (Environment)
    {
        Parameters->Environment = (PWSTR)(Parameters + 1);
        RtlCopyMemory(Parameters->Environment, Environment, EnvironmentSize);
    }

    *HostPeb = *Peb;
    HostPeb->ImageBaseAddress = ImageBase;
    HostPeb->Ldr = NULL;
    HostPeb->ProcessParameters = Parameters;
    HostPeb->ProcessHeap = NULL;
    HostPeb->NumberOfHeaps = 0;
    HostPeb->MaximumNumberOfHeaps = (PAGE_SIZE - sizeof(*HostPeb)) / sizeof(PVOID);
    HostPeb->ProcessHeaps = (PVOID *)(HostPeb + 1);
    HostPeb->ImageSubsystem = IMAGE_SUBSYSTEM_NATIVE;
    HostPeb->BeingDebugged = FALSE;
    HostPeb->KernelCallbackTable = NULL;
    HostPeb->PostProcessInitRoutine = NULL;
    HostPeb->pShimData = NULL;
    HostPeb->AppCompatInfo = NULL;
    HostPeb->ActivationContextData = NULL;
    HostPeb->ProcessAssemblyStorageMap = NULL;

    Headers = RtlImageNtHeader(ImageBase);
    LdrpEmulationHostEntry = (PEMULATION_HOST_ENTRY)((PUCHAR)ImageBase + Headers->OptionalHeader.AddressOfEntryPoint);
    LdrpEmulationStackSize = ALIGN_UP_BY(Headers->OptionalHeader.SizeOfStackReserve, PAGE_SIZE);
    LdrpEmulationHostPeb = HostPeb;
    return STATUS_SUCCESS;

Failure:
    if (HostPeb)
    {
        Size = 0;
        NtFreeVirtualMemory(NtCurrentProcess(), (PVOID *)&HostPeb, &Size, MEM_RELEASE);
    }
    NtUnmapViewOfSection(NtCurrentProcess(), ImageBase);
    return Status;
}

static
VOID
NTAPI
LdrpEmulationHostStart(
    _In_ PEMULATION_THREAD Thread)
{
    LdrpInit(NULL, LdrpEmulationNtdllBase, NULL);
    LdrpEmulationHostEntry(Thread);
    NtTerminateThread(NtCurrentThread(), STATUS_UNSUCCESSFUL);
}

static
NTSTATUS
LdrpEnterEmulationHost(
    _Inout_ PCONTEXT Context,
    _In_ PTEB Teb)
{
    PEMULATION_THREAD Thread;
    PVOID Base = NULL, CommitBase;
    SIZE_T ReserveSize, CommitSize;
    PTEB HostTeb;
    NTSTATUS Status;

    ReserveSize = PAGE_SIZE + LdrpEmulationStackSize +
                  ALIGN_UP_BY(sizeof(EMULATION_THREAD) + sizeof(TEB) + 16, PAGE_SIZE);
    Status = NtAllocateVirtualMemory(NtCurrentProcess(), &Base, 0, &ReserveSize, MEM_RESERVE, PAGE_READWRITE);
    if (!NT_SUCCESS(Status)) return Status;

    CommitBase = (PUCHAR)Base + PAGE_SIZE;
    CommitSize = ReserveSize - PAGE_SIZE;
    Status = NtAllocateVirtualMemory(NtCurrentProcess(), &CommitBase, 0, &CommitSize, MEM_COMMIT, PAGE_READWRITE);
    if (!NT_SUCCESS(Status))
    {
        ReserveSize = 0;
        NtFreeVirtualMemory(NtCurrentProcess(), &Base, &ReserveSize, MEM_RELEASE);
        return Status;
    }

    HostTeb = ALIGN_DOWN_POINTER_BY((PUCHAR)Base + ReserveSize - sizeof(TEB), 16);
    Thread = (PEMULATION_THREAD)HostTeb - 1;
    Thread->StartContext = *Context;
    Thread->GuestTeb = Teb;
    Thread->GuestPeb = Teb->ProcessEnvironmentBlock;
    Thread->StackBase = Thread;
    Thread->StackLimit = CommitBase;
    Thread->AllocationBase = Base;

    HostTeb->NtTib.ExceptionList = Teb->NtTib.ExceptionList;
    HostTeb->NtTib.StackBase = Thread->StackBase;
    HostTeb->NtTib.StackLimit = Thread->StackLimit;
    HostTeb->NtTib.Version = Teb->NtTib.Version;
    HostTeb->NtTib.Self = &HostTeb->NtTib;
    HostTeb->DeallocationStack = Base;
    HostTeb->ClientId = Teb->ClientId;
    HostTeb->RealClientId = Teb->RealClientId;
    HostTeb->CurrentLocale = Teb->CurrentLocale;
    HostTeb->ProcessEnvironmentBlock = LdrpEmulationHostPeb;
    HostTeb->StaticUnicodeString.MaximumLength = sizeof(HostTeb->StaticUnicodeBuffer);
    HostTeb->StaticUnicodeString.Buffer = HostTeb->StaticUnicodeBuffer;

    LdrpArchEnterEmulationHost(Context, HostTeb, Thread->StackBase, LdrpEmulationHostStart, Thread);
    return STATUS_SUCCESS;
}

NTSTATUS
LdrpPrepareEmulatedProcess(
    _Inout_opt_ PCONTEXT Context,
    _In_ PVOID NtdllBase,
    _Out_ PBOOLEAN Redirected)
{
    PTEB Teb = NtCurrentTeb();
    PPEB Peb = Teb->ProcessEnvironmentBlock;
    PIMAGE_NT_HEADERS Headers;
    LARGE_INTEGER Timeout;
    PCWSTR HostImage;
    NTSTATUS Status;
    LONG State;

    *Redirected = FALSE;
    if (LdrpEmulationState == LDRP_EMULATION_NATIVE) return STATUS_SUCCESS;
    if (LdrpEmulationState == LDRP_EMULATION_HOSTED && Peb == LdrpEmulationHostPeb) return STATUS_SUCCESS;

    for (;;)
    {
        State = _InterlockedCompareExchange(&LdrpEmulationState,
                                            LDRP_EMULATION_STARTING,
                                            LDRP_EMULATION_UNKNOWN);
        if (State != LDRP_EMULATION_STARTING) break;
        Timeout.QuadPart = Int32x32To64(30, -10000);
        ZwDelayExecution(FALSE, &Timeout);
    }

    if (State == LDRP_EMULATION_NATIVE) return STATUS_SUCCESS;

    if (State == LDRP_EMULATION_UNKNOWN)
    {
        Headers = RtlImageNtHeader(Peb->ImageBaseAddress);
        HostImage = Headers ? LdrpArchGetEmulationHost(Headers->FileHeader.Machine) : NULL;
        if (!HostImage)
        {
            _InterlockedExchange(&LdrpEmulationState, LDRP_EMULATION_NATIVE);
            return STATUS_SUCCESS;
        }

        LdrpEmulationNtdllBase = NtdllBase;
        Status = LdrpCreateEmulationHost(Peb, HostImage);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("LDR: No emulation host %S for machine %04x: %08lx\n",
                    HostImage, Headers->FileHeader.Machine, Status);
            _InterlockedExchange(&LdrpEmulationState, LDRP_EMULATION_UNKNOWN);
            return Status;
        }
        _InterlockedExchange(&LdrpEmulationState, LDRP_EMULATION_HOSTED);
    }

    Status = LdrpEnterEmulationHost(Context, Teb);
    if (NT_SUCCESS(Status)) *Redirected = TRUE;
    return Status;
}
