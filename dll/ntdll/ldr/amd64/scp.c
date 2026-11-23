/*
 * PROJECT:     LiberNT NT Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Supplemental code page unwind registration
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <ntdll.h>
#include <libernt/cfgscp.h>
#include <libernt/wow64shared.h>

extern SYSTEM_DLL_INIT_BLOCK LdrSystemDllInitBlock;
static volatile LONG LdrpCfgPolicy = -1;

static NTSTATUS
LdrpGetCfgPolicy(PULONG Policy)
{
    LONG Cached = InterlockedCompareExchange(&LdrpCfgPolicy, -1, -1);
    if (Cached < 0)
    {
        ULONG Info[2] = { 7, 0 };
        NTSTATUS Status = NtQueryInformationProcess(NtCurrentProcess(), ProcessMitigationPolicy, Info, sizeof(Info), NULL);
        if (!NT_SUCCESS(Status))
            return Status;
        Cached = (LONG)Info[1];
        InterlockedExchange(&LdrpCfgPolicy, Cached);
    }
    *Policy = (ULONG)Cached;
    return STATUS_SUCCESS;
}

static BOOLEAN
LdrpCfgInImage(PLDR_DATA_TABLE_ENTRY Entry, ULONG_PTR Address, SIZE_T Bytes)
{
    ULONG_PTR Base = (ULONG_PTR)Entry->DllBase;
    return Address >= Base && Bytes <= Entry->SizeOfImage && Address - Base <= Entry->SizeOfImage - Bytes;
}

static NTSTATUS
LdrpInstallCfgSlot(PLDR_DATA_TABLE_ENTRY Entry, ULONG64 SlotAddress, ULONG64 Function)
{
    PVOID Base = (PVOID)(ULONG_PTR)SlotAddress;
    SIZE_T Size = sizeof(PVOID);
    ULONG Old, Ignored;
    NTSTATUS Status;
    if (!SlotAddress)
        return STATUS_SUCCESS;
    if (!Function || (SlotAddress & (sizeof(PVOID) - 1)) || !LdrpCfgInImage(Entry, SlotAddress, Size))
        return STATUS_INVALID_IMAGE_FORMAT;
    Status = NtProtectVirtualMemory(NtCurrentProcess(), &Base, &Size, PAGE_READWRITE, &Old);
    if (!NT_SUCCESS(Status))
        return Status;
    _SEH2_TRY
    {
        InterlockedExchangePointer((PVOID volatile *)(ULONG_PTR)SlotAddress, (PVOID)(ULONG_PTR)Function);
    }
    _SEH2_FINALLY
    {
        Status = NtProtectVirtualMemory(NtCurrentProcess(), &Base, &Size, Old, &Ignored);
    }
    _SEH2_END;
    return Status;
}

static PIMAGE_LOAD_CONFIG_DIRECTORY64
LdrpGetAmd64GuardConfig(PLDR_DATA_TABLE_ENTRY Entry, ULONG Required)
{
    PIMAGE_NT_HEADERS Headers = RtlImageNtHeader(Entry->DllBase);
    PIMAGE_LOAD_CONFIG_DIRECTORY64 Config;
    ULONG Size;
    if (!Headers || Headers->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        !(Headers->OptionalHeader.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_GUARD_CF))
        return NULL;
    Config = RtlImageDirectoryEntryToData(Entry->DllBase, TRUE, IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG, &Size);
    if (!Config || Size < Required || !LdrpCfgInImage(Entry, (ULONG_PTR)Config, Required) || Config->Size < Required)
        return NULL;
    return Config;
}

NTSTATUS
LdrpInitializeAmd64Guard(PLDR_DATA_TABLE_ENTRY Entry)
{
    PIMAGE_LOAD_CONFIG_DIRECTORY64 Config;
    ULONG Policy;
    NTSTATUS Status;
    if (!LdrSystemDllInitBlock.CfgBitMap)
        return STATUS_SUCCESS;
    Config = LdrpGetAmd64GuardConfig(Entry, RTL_SIZEOF_THROUGH_FIELD(IMAGE_LOAD_CONFIG_DIRECTORY64, GuardFlags));
    if (!Config || !(Config->GuardFlags & IMAGE_GUARD_CF_INSTRUMENTED))
        return STATUS_SUCCESS;
    Status = LdrpGetCfgPolicy(&Policy);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = LdrpInstallCfgSlot(Entry, Config->GuardCFCheckFunctionPointer, (Policy & 2) ?
                               LdrSystemDllInitBlock.ScpCfgCheckESFunction : LdrSystemDllInitBlock.ScpCfgCheckFunction);
    if (NT_SUCCESS(Status))
        Status = LdrpInstallCfgSlot(Entry, Config->GuardCFDispatchFunctionPointer, (Policy & 2) ?
                                   LdrSystemDllInitBlock.ScpCfgDispatchESFunction : LdrSystemDllInitBlock.ScpCfgDispatchFunction);
    return Status;
}

NTSTATUS
LdrpEnableCfgExport(PVOID Address)
{
    ULONG_PTR Target = (ULONG_PTR)Address;
    ULONG Processed = 0, Policy;
    MEMORY_RANGE_ENTRY Range = { (PVOID)(Target & ~(ULONG_PTR)(PAGE_SIZE - 1)), PAGE_SIZE };
    CFG_CALL_TARGET_INFO Info = { Target & (PAGE_SIZE - 1), CFG_CALL_TARGET_CONVERT_EXPORT_SUPPRESSED_TO_VALID | CFG_CALL_TARGET_VALID };
    CFG_CALL_TARGET_LIST_INFORMATION List = { 1, 0, &Processed, &Info, NULL, 0 };
    NTSTATUS Status;
    UCHAR Value;
    if (!LdrSystemDllInitBlock.CfgBitMap || (Target & 15) || (Target >> 6) >= LdrSystemDllInitBlock.CfgBitMapSize)
        return STATUS_SUCCESS;
    Status = LdrpGetCfgPolicy(&Policy);
    if (!NT_SUCCESS(Status) || !(Policy & 2))
        return Status;
    _SEH2_TRY
    {
        Value = ((const volatile UCHAR *)(ULONG_PTR)LdrSystemDllInitBlock.CfgBitMap)[Target >> 6];
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode() == STATUS_ACCESS_VIOLATION ? STATUS_SUCCESS : _SEH2_GetExceptionCode();)
    }
    _SEH2_END;
    if (((Value >> ((Target >> 3) & 6)) & 3) != 2)
        return STATUS_SUCCESS;
    return NtSetInformationVirtualMemory(NtCurrentProcess(), VmCfgCallTargetInformation, 1, &Range, &List, sizeof(List));
}

NTSTATUS
LdrpEnableAddressTakenImports(PLDR_DATA_TABLE_ENTRY Entry)
{
    PIMAGE_LOAD_CONFIG_DIRECTORY64 Config;
    ULONG Policy, Stride, IatSize, Previous = 0;
    ULONG_PTR Iat, Base = (ULONG_PTR)Entry->DllBase;
    ULONG64 Index, Count;
    PUCHAR Table;
    NTSTATUS Status;
    if (!LdrSystemDllInitBlock.CfgBitMap)
        return STATUS_SUCCESS;
    Config = LdrpGetAmd64GuardConfig(Entry, RTL_SIZEOF_THROUGH_FIELD(IMAGE_LOAD_CONFIG_DIRECTORY64, GuardAddressTakenIatEntryCount));
    if (!Config || !(Config->GuardFlags & IMAGE_GUARD_CF_EXPORT_SUPPRESSION_INFO_PRESENT) || !Config->GuardAddressTakenIatEntryCount)
        return STATUS_SUCCESS;
    Status = LdrpGetCfgPolicy(&Policy);
    if (!NT_SUCCESS(Status) || !(Policy & 2))
        return Status;
    Stride = sizeof(ULONG) + ((Config->GuardFlags & IMAGE_GUARD_CF_FUNCTION_TABLE_SIZE_MASK) >> IMAGE_GUARD_CF_FUNCTION_TABLE_SIZE_SHIFT);
    Count = Config->GuardAddressTakenIatEntryCount;
    Table = (PUCHAR)(ULONG_PTR)Config->GuardAddressTakenIatEntryTable;
    Iat = (ULONG_PTR)RtlImageDirectoryEntryToData(Entry->DllBase, TRUE, IMAGE_DIRECTORY_ENTRY_IAT, &IatSize);
    if (Count > Entry->SizeOfImage / Stride || !LdrpCfgInImage(Entry, (ULONG_PTR)Table, (SIZE_T)Count * Stride) ||
        !Iat || IatSize < sizeof(PVOID) || !LdrpCfgInImage(Entry, Iat, IatSize))
        return STATUS_INVALID_IMAGE_FORMAT;
    for (Index = 0; Index < Count; ++Index)
    {
        ULONG Rva;
        RtlCopyMemory(&Rva, Table + Index * Stride, sizeof(Rva));
        if ((Rva & (sizeof(PVOID) - 1)) || (Index && Rva < Previous) || Rva < Iat - Base ||
            Rva - (Iat - Base) > IatSize - sizeof(PVOID))
            return STATUS_INVALID_IMAGE_FORMAT;
        Previous = Rva;
        Status = LdrpEnableCfgExport(*(PVOID *)(Base + Rva));
        if (!NT_SUCCESS(Status))
            return Status;
    }
    return STATUS_SUCCESS;
}

DWORD NTAPI RtlAddGrowableFunctionTable(PVOID *, PRUNTIME_FUNCTION, ULONG, ULONG, ULONG_PTR, ULONG_PTR);
VOID NTAPI RtlDeleteGrowableFunctionTable(PVOID);

NTSTATUS
LdrpArchRegisterScp(PLDR_DATA_TABLE_ENTRY Entry)
{
    PLDRP_MODULE_ENTRY Module = CONTAINING_RECORD(Entry, LDRP_MODULE_ENTRY, Entry);
    MEMORY_IMAGE_EXTENSION_INFORMATION Info = { MemoryImageExtensionCfgScp };
    ULONG_PTR ImageBase = (ULONG_PTR)Entry->DllBase;
    ULONG_PTR Base, Rva;
    SIZE_T Returned;
    ULONG TableRva;
    PRUNTIME_FUNCTION Function;
    NTSTATUS Status;

    if (Module->ScpFunctionTable)
        return STATUS_SUCCESS;

    Status = NtQueryVirtualMemory(NtCurrentProcess(), Entry->DllBase, MemoryImageExtensionInformation,
                                  &Info, sizeof(Info), &Returned);
    if (Status == STATUS_INVALID_INFO_CLASS || Status == STATUS_NOT_SUPPORTED)
        return STATUS_SUCCESS;
    if (!NT_SUCCESS(Status))
        return Status;

    if (Returned != sizeof(Info))
        return STATUS_INVALID_IMAGE_FORMAT;
    if (!Info.ExtensionSize && !Info.ExtensionImageBaseRva)
        return STATUS_SUCCESS;

    Rva = (ULONG_PTR)Info.ExtensionImageBaseRva;
    if (Rva < Entry->SizeOfImage || Rva > MAXULONG_PTR - ImageBase ||
        Rva % TYPE_ALIGNMENT(LDRP_SCP_CFG_HEADER) || Info.ExtensionSize < sizeof(LDRP_SCP_CFG_HEADER))
        return STATUS_INVALID_IMAGE_FORMAT;

    Base = ImageBase + Rva;
    if (Info.ExtensionSize > MAXULONG_PTR - Base)
        return STATUS_INVALID_IMAGE_FORMAT;

    /* Each AMD64 CFG template describes one non-leaf routine. The remaining routines are leaves. */
    TableRva = ((LDRP_SCP_CFG_HEADER *)Base)->FnTableRva;
    if (TableRva < sizeof(LDRP_SCP_CFG_HEADER) || TableRva % TYPE_ALIGNMENT(RUNTIME_FUNCTION) ||
        TableRva > Info.ExtensionSize - sizeof(RUNTIME_FUNCTION))
        return STATUS_INVALID_IMAGE_FORMAT;

    Function = (PRUNTIME_FUNCTION)(Base + TableRva);
    if (Function->BeginAddress >= Function->EndAddress || Function->EndAddress > Info.ExtensionSize ||
        Function->UnwindData > Info.ExtensionSize - sizeof(ULONG))
        return STATUS_INVALID_IMAGE_FORMAT;

    Status = RtlAddGrowableFunctionTable(&Module->ScpFunctionTable, Function, 1, 1, Base,
                                         Base + Info.ExtensionSize);
    if (NT_SUCCESS(Status))
        InterlockedOr((PLONG)&Entry->Flags, LDRP_SCP_IN_EXCEPTION_TABLE);
    return Status;
}

VOID
LdrpArchUnregisterScp(PLDR_DATA_TABLE_ENTRY Entry)
{
    PLDRP_MODULE_ENTRY Module = CONTAINING_RECORD(Entry, LDRP_MODULE_ENTRY, Entry);

    if (Module->ScpFunctionTable)
    {
        RtlDeleteGrowableFunctionTable(Module->ScpFunctionTable);
        Module->ScpFunctionTable = NULL;
        InterlockedAnd((PLONG)&Entry->Flags, ~LDRP_SCP_IN_EXCEPTION_TABLE);
    }
}
