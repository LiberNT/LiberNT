/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Call native host libraries from guest code and guest code from them
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "amd64emu.h"

#define EMU_STUB_SIZE         16
#define EMU_STUB_BLOCK_SIZE   0x10000
#define EMU_THUNK_CHUNK_SIZE  1024
#define EMU_THUNK_CHUNK_COUNT 64
#define EMU_GUEST_STACK_GAP   128
#define EMU_FORWARDER_DEPTH   4

typedef struct _EMU_HOST_THUNK
{
    PVOID Native;
    ULONG64 Stub;
    ULONG Count;
    CHAR Signature[EMU_HOST_MAX_ARGUMENTS + 2];
} EMU_HOST_THUNK, *PEMU_HOST_THUNK;

int Felix86NtSetJump(PULONG64 Buffer) __attribute__((returns_twice));
DECLSPEC_NORETURN void Felix86NtLongJump(PULONG64 Buffer, int Value);

static RTL_CRITICAL_SECTION EmuHostLock;
static PEMU_HOST_THUNK EmuHostThunks[EMU_THUNK_CHUNK_COUNT];
static volatile LONG EmuHostThunkCount;
static PUCHAR EmuStubBlock;
static ULONG EmuStubUsed;
static ULONG64 EmuGuestReturnStub;
#define EMU_RV64X_SECTION ".rv64x"

static PUCHAR EmuCodeMap;
static ULONG_PTR EmuCodeMapLimit;
static PVOID EmuDllNotificationCookie;

static const EMU_REGISTER EmuServiceArguments[4] = { EmuR10, EmuRdx, EmuR8, EmuR9 };
static const EMU_REGISTER EmuCallArguments[4] = { EmuRcx, EmuRdx, EmuR8, EmuR9 };

static
BOOLEAN
EmuIsValidSignature(
    _In_ PCSTR Signature)
{
    SIZE_T Length = strlen(Signature);
    SIZE_T Index;

    if (!Length || Length > EMU_HOST_MAX_ARGUMENTS + 1) return FALSE;
    for (Index = 0; Index < Length; Index++)
    {
        switch (Signature[Index])
        {
            case EMU_HOST_TYPE_VOID:
                if (Index) return FALSE;
                break;

            case EMU_HOST_TYPE_INT64:
            case EMU_HOST_TYPE_INT32:
            case EMU_HOST_TYPE_INT16:
            case EMU_HOST_TYPE_UINT16:
            case EMU_HOST_TYPE_INT8:
            case EMU_HOST_TYPE_UINT8:
            case EMU_HOST_TYPE_FLOAT:
            case EMU_HOST_TYPE_DOUBLE:
                break;

            default:
                return FALSE;
        }
    }
    return TRUE;
}

static
BOOLEAN
EmuIsFloatType(
    _In_ CHAR Type)
{
    return Type == EMU_HOST_TYPE_FLOAT || Type == EMU_HOST_TYPE_DOUBLE;
}

static
ULONG64
EmuHostNarrow(
    _In_ CHAR Type,
    _In_ ULONG64 Value)
{
    switch (Type)
    {
        case EMU_HOST_TYPE_INT32: return (ULONG64)(LONG64)(LONG)Value;
        case EMU_HOST_TYPE_INT16: return (ULONG64)(LONG64)(SHORT)Value;
        case EMU_HOST_TYPE_UINT16: return (USHORT)Value;
        case EMU_HOST_TYPE_INT8: return (ULONG64)(LONG64)(CHAR)Value;
        case EMU_HOST_TYPE_UINT8: return (UCHAR)Value;
        case EMU_HOST_TYPE_FLOAT: return (ULONG)Value;
        default: return Value;
    }
}

static
BOOLEAN
EmuDescriptorSignature(
    _In_reads_bytes_(EMU_HOST_DESCRIPTOR_SIZE) const UCHAR *Descriptor,
    _In_ BOOLEAN Entry,
    _Out_writes_(EMU_HOST_MAX_ARGUMENTS + 2) PCHAR Signature)
{
    static const CHAR Types[] = "vqwhHbBfd";
    ULONG Count = Descriptor[2], Index, Kind;

    if ((Descriptor[0] | (Descriptor[1] << 8)) != EMU_HOST_DESCRIPTOR_MAGIC) return FALSE;
    if ((Descriptor[3] & EMU_HOST_DESCRIPTOR_UNSUPPORTED) || Count > EMU_HOST_MAX_ARGUMENTS) return FALSE;
    if (Entry && (Descriptor[3] & EMU_HOST_DESCRIPTOR_VARIADIC)) return FALSE;

    Kind = Descriptor[3] & 0xF;
    if (Kind >= sizeof(Types) - 1) return FALSE;
    Signature[0] = Types[Kind];
    for (Index = 0; Index < Count; Index++)
    {
        Kind = (Descriptor[4 + Index / 2] >> ((Index & 1) * 4)) & 0xF;
        if (!Kind || Kind >= sizeof(Types) - 1) return FALSE;
        Signature[Index + 1] = Types[Kind];
    }
    Signature[Count + 1] = ANSI_NULL;
    return TRUE;
}

static
VOID
EmuMarkNativeImage(
    _In_ PVOID Base,
    _In_ BOOLEAN Native)
{
    PIMAGE_NT_HEADERS Headers = RtlImageNtHeader(Base);
    PIMAGE_SECTION_HEADER Section;
    ULONG_PTR Page, Last;
    ULONG Index;

    if (!EmuCodeMap || !Headers) return;
    Section = IMAGE_FIRST_SECTION(Headers);
    for (Index = 0; Index < Headers->FileHeader.NumberOfSections; Index++, Section++)
    {
        if (!(Section->Characteristics & IMAGE_SCN_MEM_EXECUTE) || !Section->Misc.VirtualSize) continue;
        Page = ((ULONG_PTR)Base + Section->VirtualAddress) >> PAGE_SHIFT;
        Last = ((ULONG_PTR)Base + Section->VirtualAddress + Section->Misc.VirtualSize - 1) >> PAGE_SHIFT;
        for (; Page <= Last && Page <= (EmuCodeMapLimit >> PAGE_SHIFT); Page++)
        {
            if (Native) InterlockedOr8((PCHAR)&EmuCodeMap[Page >> 3], (CHAR)(1 << (Page & 7)));
            else InterlockedAnd8((PCHAR)&EmuCodeMap[Page >> 3], (CHAR)~(1 << (Page & 7)));
        }
    }

    if (!Native) return;
    Section = IMAGE_FIRST_SECTION(Headers);
    for (Index = 0; Index < Headers->FileHeader.NumberOfSections; Index++, Section++)
    {
        if (!memcmp(Section->Name, EMU_RV64X_SECTION, sizeof(EMU_RV64X_SECTION)) &&
            (Section->Characteristics & IMAGE_SCN_MEM_WRITE) && Section->Misc.VirtualSize >= sizeof(PVOID))
        {
            InterlockedExchangePointer((PVOID *)((PUCHAR)Base + Section->VirtualAddress), EmuCodeMap);
        }
    }
}

static
VOID
EmuMarkNativeImages(VOID)
{
    PLIST_ENTRY Head = &NtCurrentPeb()->Ldr->InLoadOrderModuleList, Entry;

    RtlEnterCriticalSection(NtCurrentPeb()->LoaderLock);
    for (Entry = Head->Flink; Entry != Head; Entry = Entry->Flink)
        EmuMarkNativeImage(CONTAINING_RECORD(Entry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks)->DllBase, TRUE);
    RtlLeaveCriticalSection(NtCurrentPeb()->LoaderLock);
}

static
VOID
NTAPI
EmuDllNotification(
    _In_ ULONG Reason,
    _In_ PCLDR_DLL_NOTIFICATION_DATA Data,
    _In_opt_ PVOID Context)
{
    EmuMarkNativeImage(Data->Loaded.DllBase, Reason == LDR_DLL_NOTIFICATION_REASON_LOADED);
}

static
NTSTATUS
EmuInitializeCodeMap(VOID)
{
    SYSTEM_BASIC_INFORMATION Information;
    SIZE_T Size;
    PVOID Base = NULL;
    NTSTATUS Status;

    Status = NtQuerySystemInformation(SystemBasicInformation, &Information, sizeof(Information), NULL);
    if (!NT_SUCCESS(Status)) return Status;

    Size = (Information.MaximumUserModeAddress >> (PAGE_SHIFT + 3)) + 1;
    Status = NtAllocateVirtualMemory(NtCurrentProcess(), &Base, 0, &Size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!NT_SUCCESS(Status)) return Status;

    EmuCodeMapLimit = Information.MaximumUserModeAddress;
    EmuCodeMap = Base;
    Status = LdrRegisterDllNotification(0, EmuDllNotification, NULL, &EmuDllNotificationCookie);
    if (!NT_SUCCESS(Status)) return Status;
    EmuMarkNativeImages();
    NtCurrentPeb()->EcCodeBitMap = Base;
    return STATUS_SUCCESS;
}

static
ULONG64
EmuAllocateStub(
    _In_reads_(Length) const UCHAR *Code,
    _In_ ULONG Length)
{
    SIZE_T Size = EMU_STUB_BLOCK_SIZE;
    PVOID Base = NULL;
    PUCHAR Stub;

    if (!EmuStubBlock || EmuStubUsed + EMU_STUB_SIZE > EMU_STUB_BLOCK_SIZE)
    {
        if (!NT_SUCCESS(NtAllocateVirtualMemory(NtCurrentProcess(), &Base, 0, &Size,
                                                MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE)))
        {
            return 0;
        }
        EmuStubBlock = Base;
        EmuStubUsed = 0;
    }

    Stub = EmuStubBlock + EmuStubUsed;
    RtlFillMemory(Stub, EMU_STUB_SIZE, 0xCC);
    RtlCopyMemory(Stub, Code, Length);
    EmuStubUsed += EMU_STUB_SIZE;
    return (ULONG64)Stub;
}

static
ULONG64
NTAPI
EmuGuestThunk(
    _In_ PVOID Native,
    _In_opt_ PCSTR Signature)
{
    CHAR Described[EMU_HOST_MAX_ARGUMENTS + 2];
    UCHAR Code[] = { 0x4C, 0x8B, 0xD1, 0xB8, 0, 0, 0, 0, 0x0F, 0x05, 0xC3 };
    PEMU_HOST_THUNK Thunk;
    ULONG Index, Count, Service;
    ULONG64 Stub = 0;

    if (Native && !Signature)
    {
        _SEH2_TRY
        {
            if (EmuDescriptorSignature((PUCHAR)Native - EMU_HOST_DESCRIPTOR_SIZE, TRUE, Described))
                Signature = Described;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
        }
        _SEH2_END;
    }
    if (!Native || !Signature || !EmuIsValidSignature(Signature)) return 0;

    RtlEnterCriticalSection(&EmuHostLock);
    Count = (ULONG)EmuHostThunkCount;
    for (Index = 0; Index < Count; Index++)
    {
        Thunk = &EmuHostThunks[Index / EMU_THUNK_CHUNK_SIZE][Index % EMU_THUNK_CHUNK_SIZE];
        if (Thunk->Native == Native && !strcmp(Thunk->Signature, Signature))
        {
            Stub = Thunk->Stub;
            break;
        }
    }

    if (!Stub && Count < EMU_THUNK_CHUNK_SIZE * EMU_THUNK_CHUNK_COUNT)
    {
        if (!EmuHostThunks[Count / EMU_THUNK_CHUNK_SIZE])
        {
            EmuHostThunks[Count / EMU_THUNK_CHUNK_SIZE] =
                RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, EMU_THUNK_CHUNK_SIZE * sizeof(EMU_HOST_THUNK));
        }

        if (EmuHostThunks[Count / EMU_THUNK_CHUNK_SIZE])
        {
            Thunk = &EmuHostThunks[Count / EMU_THUNK_CHUNK_SIZE][Count % EMU_THUNK_CHUNK_SIZE];
            Service = EMU_HOST_SERVICE_THUNK + Count;
            RtlCopyMemory(&Code[4], &Service, sizeof(Service));
            Stub = EmuAllocateStub(Code, sizeof(Code));
            if (Stub)
            {
                Thunk->Native = Native;
                Thunk->Stub = Stub;
                Thunk->Count = (ULONG)strlen(Signature) - 1;
                strcpy(Thunk->Signature, Signature);
                InterlockedExchange(&EmuHostThunkCount, (LONG)Count + 1);
            }
        }
    }
    RtlLeaveCriticalSection(&EmuHostLock);
    return Stub;
}

static
NTSTATUS
EmuCollectArguments(
    _In_ PEMU_THREAD Thread,
    _In_ PCSTR Type,
    _In_ PEMU_REGISTERS Registers,
    _Out_writes_(EMU_HOST_MAX_ARGUMENTS) PULONG64 Arguments)
{
    PULONG64 Vectors;
    PULONG64 GuestStack = (PULONG64)Registers->Gpr[EmuRsp];
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Stride, Index;
    ULONG64 Value;

    Vectors = EmuCoreGetVectors(Thread->Cpu, &Stride);
    _SEH2_TRY
    {
        for (Index = 0; Type[Index]; Index++)
        {
            if (Index < 4)
                Value = EmuIsFloatType(Type[Index]) ? Vectors[Index * Stride] : Registers->Gpr[EmuServiceArguments[Index]];
            else
                Value = GuestStack[Index + 1];
            Arguments[Index] = Type[Index] == EMU_HOST_TYPE_FLOAT ? (ULONG64)(LONG64)(LONG)Value :
                                                                    EmuHostNarrow(Type[Index], Value);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    return Status;
}

static
VOID
EmuHostCall(
    _In_ PEMU_THREAD Thread,
    _In_ PEMU_HOST_THUNK Thunk,
    _Inout_ PEMU_REGISTERS Registers)
{
    ULONG64 Arguments[EMU_HOST_MAX_ARGUMENTS] = { 0 };
    ULONG Count = Thunk->Count;
    ULONG64 Result;
    PULONG64 Vectors;
    ULONG Stride;
    NTSTATUS Status;

    Status = EmuCollectArguments(Thread, Thunk->Signature + 1, Registers, Arguments);
    if (!NT_SUCCESS(Status))
    {
        Registers->Gpr[EmuRax] = (ULONG)Status;
        return;
    }

    Result = EmuInvokeNative(Thunk->Native, Arguments, Arguments + 8, Count > 8 ? Count - 8 : 0);
    switch (Thunk->Signature[0])
    {
        case EMU_HOST_TYPE_VOID:
            break;

        case EMU_HOST_TYPE_FLOAT:
        case EMU_HOST_TYPE_DOUBLE:
            Vectors = EmuCoreGetVectors(Thread->Cpu, &Stride);
            Vectors[0] = EmuHostNarrow(Thunk->Signature[0], Result);
            Vectors[1] = 0;
            break;

        default:
            Registers->Gpr[EmuRax] = Result;
            break;
    }
}

DECLSPEC_NORETURN
static
VOID
EmuGuestReturn(
    _In_ PEMU_THREAD Thread,
    _In_ PEMU_REGISTERS Registers)
{
    PEMU_GUEST_CALL Call = Thread->GuestCall;
    ULONG Stride;

    if (!Call) EmuFatal(STATUS_INVALID_SYSTEM_SERVICE, "guest return without a host call");

    Call->Result = Registers->Gpr[EmuR10];
    Call->FloatResult = EmuCoreGetVectors(Thread->Cpu, &Stride)[0];
    Thread->GuestCall = Call->Previous;
    Registers->Gpr[EmuRsp] = Call->Rsp;
    Registers->Rip = Call->Rip;
    EmuCoreSetFrame(Thread->Cpu, Call->Frame);
    Thread->HostDepth = Call->HostDepth;
    Felix86NtLongJump(Call->Jump, 1);
}

static
ULONG64
NTAPI
EmuCallGuest(
    _In_ ULONG64 Function,
    _In_ PCSTR Signature,
    _In_opt_ const ULONG64 *Arguments)
{
    PEMU_THREAD Thread = EmuCurrentThread();
    PEMU_REGISTERS Registers;
    EMU_GUEST_CALL Call;
    PULONG64 Stack, Vectors;
    ULONG Count, Index, Stride;
    ULONG64 Value;

    if (!EmuIsValidSignature(Signature)) EmuFatal(STATUS_INVALID_PARAMETER, "invalid guest call signature");
    if (!Thread || !Thread->Cpu || !Thread->HostDepth || !Function) return 0;

    if (Felix86NtSetJump(Call.Jump))
    {
        if (EmuIsFloatType(Signature[0])) return EmuHostNarrow(Signature[0], Call.FloatResult);
        if (Signature[0] == EMU_HOST_TYPE_VOID) return 0;
        return EmuHostNarrow(Signature[0], Call.Result);
    }

    Registers = EmuCoreGetRegisters(Thread->Cpu);
    Vectors = EmuCoreGetVectors(Thread->Cpu, &Stride);
    Call.Rsp = Registers->Gpr[EmuRsp];
    Call.Rip = Registers->Rip;
    Call.Frame = EmuCoreGetFrame(Thread->Cpu);
    Call.HostDepth = Thread->HostDepth;

    Count = (ULONG)strlen(Signature) - 1;
    Stack = (PULONG64)(((Call.Rsp - EMU_GUEST_STACK_GAP - max(Count, 4) * sizeof(ULONG64)) & ~(ULONG64)15) -
                       sizeof(ULONG64));
    Stack[0] = EmuGuestReturnStub;
    for (Index = 0; Index < Count; Index++)
    {
        Value = EmuHostNarrow(Signature[Index + 1], Arguments[Index]);
        if (Index < 4)
        {
            if (EmuIsFloatType(Signature[Index + 1]))
            {
                Vectors[Index * Stride] = Value;
                Vectors[Index * Stride + 1] = 0;
            }
            Registers->Gpr[EmuCallArguments[Index]] = Value;
        }
        Stack[Index + 1] = Value;
    }
    Registers->Gpr[EmuRsp] = (ULONG64)Stack;
    Registers->Rip = Function;

    Call.Previous = Thread->GuestCall;
    Thread->GuestCall = &Call;
    Thread->HostDepth = 0;
    EmuCoreRun(Thread->Cpu);
}

static
ULONG64
EmuFindGuestExport(
    _In_ PCSTR Module,
    _In_ PCSTR Name,
    _In_ ULONG Depth)
{
    PIMAGE_EXPORT_DIRECTORY Exports;
    PLDR_DATA_TABLE_ENTRY Entry;
    UNICODE_STRING ModuleName;
    ANSI_STRING ModuleAnsi;
    WCHAR ModuleBuffer[MAX_PATH];
    CHAR Forward[MAX_PATH], ForwardModule[MAX_PATH];
    PLIST_ENTRY Link, Head;
    PCHAR Dot;
    PVOID Base = NULL, Address;
    ULONG Size;

    if (Depth > EMU_FORWARDER_DEPTH) return 0;

    RtlInitAnsiString(&ModuleAnsi, Module);
    RtlInitEmptyUnicodeString(&ModuleName, ModuleBuffer, sizeof(ModuleBuffer));
    if (!NT_SUCCESS(RtlAnsiStringToUnicodeString(&ModuleName, &ModuleAnsi, FALSE))) return 0;

    Head = &EmuProcess.GuestPeb->Ldr->InLoadOrderModuleList;
    for (Link = Head->Flink; Link != Head; Link = Link->Flink)
    {
        Entry = CONTAINING_RECORD(Link, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        if (RtlEqualUnicodeString(&Entry->BaseDllName, &ModuleName, TRUE))
        {
            Base = Entry->DllBase;
            break;
        }
    }
    if (!Base) return 0;

    Address = RtlFindExportedRoutineByName(Base, Name);
    Exports = RtlImageDirectoryEntryToData(Base, TRUE, IMAGE_DIRECTORY_ENTRY_EXPORT, &Size);
    if (!Address || !Exports || (PUCHAR)Address < (PUCHAR)Exports || (PUCHAR)Address >= (PUCHAR)Exports + Size)
        return (ULONG64)Address;

    if (strlen(Address) >= sizeof(Forward)) return 0;
    strcpy(Forward, Address);
    Dot = strrchr(Forward, '.');
    if (!Dot || Dot[1] == '#' || (SIZE_T)(Dot - Forward) + sizeof(".dll") > sizeof(ForwardModule)) return 0;
    *Dot = ANSI_NULL;
    strcpy(ForwardModule, Forward);
    strcat(ForwardModule, ".dll");
    return EmuFindGuestExport(ForwardModule, Dot + 1, Depth + 1);
}

static
ULONG64
NTAPI
EmuGuestProcedure(
    _In_ PCSTR Module,
    _In_ PCSTR Name)
{
    return EmuFindGuestExport(Module, Name, 0);
}

static
ULONG64
EmuHostLoad(
    _In_ PCWSTR Name)
{
    ANSI_STRING Export = RTL_CONSTANT_STRING(EMU_HOST_INITIALIZE_EXPORT);
    PEMU_HOST_INITIALIZE Initialize;
    UNICODE_STRING Library;
    WCHAR Buffer[MAX_PATH];
    NTSTATUS Status;
    PVOID Base;

    RtlInitEmptyUnicodeString(&Library, Buffer, sizeof(Buffer) - sizeof(WCHAR));
    _SEH2_TRY
    {
        Status = RtlAppendUnicodeToString(&Library, Name);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    if (!NT_SUCCESS(Status) || wcschr(Buffer, L'\\') || wcschr(Buffer, L'/')) return 0;

    Status = LdrLoadDll(NULL, NULL, &Library, &Base);
    if (NT_SUCCESS(Status)) Status = LdrGetProcedureAddress(Base, &Export, 0, (PVOID *)&Initialize);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("AMD64EMU: cannot load the host library %wZ: %08lx\n", &Library, Status);
        return 0;
    }
    return Initialize();
}

VOID
EmuHostService(
    _In_ PEMU_THREAD Thread,
    _In_ ULONG Service,
    _Inout_ PEMU_REGISTERS Registers)
{
    ULONG64 ServiceRip = Registers->Rip;
    ULONG64 ReturnRip = Registers->Gpr[EmuRcx];
    ULONG Index = Service - EMU_HOST_SERVICE_THUNK;

    if (Service == EMU_HOST_SERVICE_RETURN) EmuGuestReturn(Thread, Registers);

    Registers->Rip = ReturnRip;
    Thread->HostDepth++;
    if (Service == EMU_HOST_SERVICE_LOAD)
        Registers->Gpr[EmuRax] = EmuHostLoad((PCWSTR)Registers->Gpr[EmuR10]);
    else if (Service >= EMU_HOST_SERVICE_THUNK && Index < (ULONG)EmuHostThunkCount)
        EmuHostCall(Thread, &EmuHostThunks[Index / EMU_THUNK_CHUNK_SIZE][Index % EMU_THUNK_CHUNK_SIZE], Registers);
    else
        Registers->Gpr[EmuRax] = (ULONG)STATUS_INVALID_SYSTEM_SERVICE;
    Thread->HostDepth--;
    if (Registers->Rip == ReturnRip) Registers->Rip = ServiceRip;
}

static
ULONG64
NTAPI
EmuCallDescribed(
    _In_ ULONG64 Function,
    _In_ ULONG64 DescriptorLow,
    _In_ ULONG64 DescriptorHigh,
    _In_opt_ const ULONG64 *Arguments)
{
    UCHAR Descriptor[EMU_HOST_DESCRIPTOR_SIZE];
    CHAR Signature[EMU_HOST_MAX_ARGUMENTS + 2];
    ULONG Count;
    PVOID Base;

    RtlCopyMemory(Descriptor, &DescriptorLow, sizeof(DescriptorLow));
    RtlCopyMemory(Descriptor + sizeof(DescriptorLow), &DescriptorHigh, sizeof(DescriptorHigh));
    if (!Function) RtlRaiseStatus(STATUS_ACCESS_VIOLATION);
    if (!EmuDescriptorSignature(Descriptor, FALSE, Signature)) EmuFatal(STATUS_NOT_SUPPORTED, "unsupported RV64X exit");

    if (RtlPcToFileHeader((PVOID)(ULONG_PTR)Function, &Base))
    {
        EmuMarkNativeImage(Base, TRUE);
        Count = Descriptor[2];
        return EmuInvokeNative((PVOID)(ULONG_PTR)Function, Arguments, Arguments + 8, Count > 8 ? Count - 8 : 0);
    }
    return EmuCallGuest(Function, Signature, Arguments);
}

ULONG64
EmuNativeEntry(
    _In_ ULONG64 Rip)
{
    ULONG64 Stub;

    if (!EmuCodeMap || Rip > EmuCodeMapLimit) return 0;
    if (!(EmuCodeMap[Rip >> (PAGE_SHIFT + 3)] & (1 << ((Rip >> PAGE_SHIFT) & 7)))) return 0;
    Stub = EmuGuestThunk((PVOID)(ULONG_PTR)Rip, NULL);
    if (!Stub)
    {
        PLDR_DATA_TABLE_ENTRY Entry;

        if (NT_SUCCESS(LdrFindEntryForAddress((PVOID)(ULONG_PTR)Rip, &Entry)))
            DbgPrint("AMD64EMU: AMD64 code entered %wZ+%Ix, which has no RV64X entry descriptor\n",
                     &Entry->BaseDllName, (SIZE_T)(Rip - (ULONG_PTR)Entry->DllBase));
        EmuFatal(STATUS_NOT_SUPPORTED, "AMD64 code entered native code that has no RV64X entry descriptor");
    }
    return Stub;
}

static const EMU_HOST_INTERFACE EmuHostInterface =
{
    sizeof(EMU_HOST_INTERFACE),
    EmuGuestThunk,
    EmuCallGuest,
    EmuGuestProcedure,
    EmuCallDescribed
};

const EMU_HOST_INTERFACE *
NTAPI
EmuHostGetInterface(VOID)
{
    return &EmuHostInterface;
}

NTSTATUS
EmuInitializeHostCalls(VOID)
{
    UCHAR Return[] = { 0x49, 0x89, 0xC2, 0xB8, 0, 0, 0, 0, 0x0F, 0x05, 0xCC };
    ULONG Service = EMU_HOST_SERVICE_RETURN;
    NTSTATUS Status;

    Status = RtlInitializeCriticalSection(&EmuHostLock);
    if (!NT_SUCCESS(Status)) return Status;
    Status = EmuInitializeCodeMap();
    if (!NT_SUCCESS(Status)) return Status;

    RtlCopyMemory(&Return[4], &Service, sizeof(Service));
    RtlEnterCriticalSection(&EmuHostLock);
    EmuGuestReturnStub = EmuAllocateStub(Return, sizeof(Return));
    RtlLeaveCriticalSection(&EmuHostLock);
    return EmuGuestReturnStub ? STATUS_SUCCESS : STATUS_NO_MEMORY;
}
