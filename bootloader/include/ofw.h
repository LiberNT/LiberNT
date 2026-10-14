/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Open Firmware client interface and platform definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define PPC_STAGE0_MAGIC   0x30475453
#define PPC_STAGE0_VERSION 1

#define OFW_APPLICATION_MAGIC   0x5050414F
#define OFW_APPLICATION_VERSION 1

#define PPC_MSR_EE  0x00008000UL
#define PPC_MSR_FP  0x00002000UL
#define PPC_MSR_ME  0x00001000UL
#define PPC_MSR_IR  0x00000020UL
#define PPC_MSR_DR  0x00000010UL
#define PPC_MSR_ILE 0x00010000UL
#define PPC_MSR_RI  0x00000002UL
#define PPC_MSR_LE  0x00000001UL

typedef struct _PPC_STAGE0_INFO
{
    ULONG Magic;
    ULONG Version;
    ULONG OfEntry;
    ULONG BeGate;
    ULONG Stage0Base;
    ULONG Stage0Size;
    ULONG ImageBase;
    ULONG ImageSize;
    ULONG InitrdBase;
    ULONG InitrdSize;
    ULONG MachineId;
    ULONG Reserved[5];
} PPC_STAGE0_INFO, *PPPC_STAGE0_INFO;

#define OFW_MAX_ALLOCATIONS 1024
#define OFW_PATH_CHARS      260

typedef struct _OFW_ALLOCATION
{
    ULONG Base;
    ULONG Size;
} OFW_ALLOCATION, *POFW_ALLOCATION;

typedef struct _OFW_HEAP_BLOCK
{
    ULONG Size;
    ULONG Free;
    struct _OFW_HEAP_BLOCK* Next;
    ULONG Reserved;
} OFW_HEAP_BLOCK, *POFW_HEAP_BLOCK;

typedef struct _OFW_SHARED_STATE
{
    ULONG AllocationCount;
    OFW_ALLOCATION Allocations[OFW_MAX_ALLOCATIONS];
    POFW_HEAP_BLOCK Heap;
} OFW_SHARED_STATE, *POFW_SHARED_STATE;

typedef struct _OFW_APPLICATION_PARAMETERS
{
    ULONG Magic;
    ULONG Version;
    PPPC_STAGE0_INFO Stage0;
    POFW_SHARED_STATE Shared;
    PVOID ImageBase;
    ULONG ImageSize;
    ULONG BootDisk;
    ULONG BootPartition;
    PCWSTR LoadOptions;
    WCHAR ImagePath[OFW_PATH_CHARS];
} OFW_APPLICATION_PARAMETERS, *POFW_APPLICATION_PARAMETERS;

typedef ULONG OFW_HANDLE;
#define OFW_INVALID 0

typedef struct _OFW_MACHINE_INFO
{
    ULONG MachineType;
    ULONG TimebaseFrequency;
    ULONG ProcessorFrequency;
    ULONG BusFrequency;
    ULONG DcacheLineSize;
    ULONG IcacheLineSize;
    ULONG DcacheSize;
    ULONG IcacheSize;
    ULONG MemorySize;
    ULONG IsaIoPhysicalBase;
    ULONG PciMemoryPhysicalBase;
    ULONG PciDmaOffset;
    ULONG ConsolePort;
    CHAR Model[64];
} OFW_MACHINE_INFO, *POFW_MACHINE_INFO;

extern PPPC_STAGE0_INFO OfwStage0Info;
extern POFW_APPLICATION_PARAMETERS OfwParameters;
extern OFW_HANDLE OfwStdin;
extern OFW_HANDLE OfwStdout;
extern BOOLEAN OfwAvailable;
extern OFW_MACHINE_INFO OfwMachine;

LONG
OfwGate(
    _Inout_ PULONG Args,
    _In_ ULONG BeGate,
    _In_ ULONG OfEntry);

BOOLEAN
OfwInitialize(
    _In_ PPPC_STAGE0_INFO Info);

LONG
OfwCall(
    _In_z_ PCSTR Service,
    _In_ ULONG ArgCount,
    _In_ ULONG RetCount,
    _Inout_ PULONG Cells);

OFW_HANDLE
OfwFindDevice(
    _In_z_ PCSTR Path);

OFW_HANDLE
OfwPeer(
    _In_ OFW_HANDLE Phandle);

OFW_HANDLE
OfwChild(
    _In_ OFW_HANDLE Phandle);

LONG
OfwGetProp(
    _In_ OFW_HANDLE Phandle,
    _In_z_ PCSTR Name,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length);

BOOLEAN
OfwGetPropCell(
    _In_ OFW_HANDLE Phandle,
    _In_z_ PCSTR Name,
    _Out_ PULONG Value);

LONG
OfwPackageToPath(
    _In_ OFW_HANDLE Phandle,
    _Out_writes_(Length) PCHAR Buffer,
    _In_ ULONG Length);

OFW_HANDLE
OfwOpen(
    _In_z_ PCSTR Path);

LONG
OfwRead(
    _In_ OFW_HANDLE Ihandle,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length);

LONG
OfwWrite(
    _In_ OFW_HANDLE Ihandle,
    _In_reads_bytes_(Length) const VOID* Buffer,
    _In_ ULONG Length);

LONG
OfwSeek(
    _In_ OFW_HANDLE Ihandle,
    _In_ ULONGLONG Position);

ULONG
OfwClaim(
    _In_ ULONG Virt,
    _In_ ULONG Size,
    _In_ ULONG Align);

ULONG
OfwMilliseconds(VOID);

LONG
OfwCallMethod1(
    _In_z_ PCSTR Method,
    _In_ OFW_HANDLE Ihandle,
    _Out_ PULONG Result);

DECLSPEC_NORETURN
VOID
OfwExit(VOID);

VOID
OfwDetectMachine(VOID);

BOOLEAN
OfwGetDisplay(
    _Out_ PULONG Address,
    _Out_ PULONG Physical,
    _Out_ PULONG Width,
    _Out_ PULONG Height,
    _Out_ PULONG Stride);

VOID
PpcFlushCacheRange(
    _In_ PVOID Address,
    _In_ SIZE_T Length);

FORCEINLINE
ULONG
OfwCellToHost(
    _In_ ULONG Cell)
{
    return _byteswap_ulong(Cell);
}
