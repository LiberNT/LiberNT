/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     PC/AT BIOS platform definitions shared by the boot sectors, the startup stub and the boot applications
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define BIOS_TRANSFER_BASE          0x10000
#define BIOS_TRANSFER_SIZE          0x10000
#define BIOS_TRANSFER_DATA          0x11000
#define BIOS_TRANSFER_DATA_SIZE     0xF000
#define BIOS_STUB_SEGMENT           0x2000
#define BIOS_STUB_BASE              0x20000
#define BIOS_STUB_SIZE              0x10000
#define BIOS_STUB_IMAGE_OFFSET      0x4000
#define BIOS_STUB_SCRATCH_OFFSET    0x4000
#define BIOS_STUB_REAL_STACK        0xFFF0
#define BIOS_PAGE_TABLE_BASE        0x30000
#define BIOS_PAGE_TABLE_SIZE        0x6000
#define BIOS_STACK_BASE             0x60000
#define BIOS_STACK_SIZE             0x20000
#define BIOS_CONVENTIONAL_LIMIT     0xA0000
#define BIOS_ROM_BASE               0xA0000
#define BIOS_ROM_LIMIT              0x100000
#define BIOS_FILE_BASE              0x100000
#define BIOS_FILE_LIMIT             0x800000
#define BIOS_IDENTITY_LIMIT         0x100000000ULL

#define BIOS_SEL_CODE32             0x08
#define BIOS_SEL_DATA32             0x10
#define BIOS_SEL_CODE64             0x18
#define BIOS_SEL_CODE16             0x20
#define BIOS_SEL_DATA16             0x28

/* Handed over by the boot sector in EAX together with DL = drive, DH = medium, EDI:EBX = volume start LBA and ECX = file size */
#define BIOS_LOADER_MAGIC           0x52444C42

#define BIOS_MEDIUM_DISK            0
#define BIOS_MEDIUM_CDROM           1
#define BIOS_MEDIUM_NETWORK         2

#define BIOS_STARTUP_MAGIC          0x50545342
#define BIOS_STARTUP_VERSION        1
#define BIOS_STARTUP_INFO_OFFSET    0x10

#define BSI_MAGIC                   0x00
#define BSI_VERSION                 0x04
#define BSI_BOOT_DRIVE              0x08
#define BSI_BOOT_MEDIUM             0x0C
#define BSI_PARTITION_START         0x10
#define BSI_CALL                    0x18
#define BSI_FILE_SIZE               0x1C
#define BSI_IMAGE_BASE              0x20
#define BSI_IMAGE_SIZE              0x24
#define BSI_ENTRY                   0x28
#define BSI_SIZE                    0x30

#define BR_EAX                      0x00
#define BR_EBX                      0x04
#define BR_ECX                      0x08
#define BR_EDX                      0x0C
#define BR_ESI                      0x10
#define BR_EDI                      0x14
#define BR_EBP                      0x18
#define BR_DS                       0x1C
#define BR_ES                       0x1E
#define BR_EFLAGS                   0x20
#define BR_SIZE                     0x24

#ifndef __ASM__

#define BIOS_CARRY_FLAG             0x0001

typedef struct _BIOS_REGISTERS
{
    ULONG Eax;
    ULONG Ebx;
    ULONG Ecx;
    ULONG Edx;
    ULONG Esi;
    ULONG Edi;
    ULONG Ebp;
    USHORT Ds;
    USHORT Es;
    ULONG Eflags;
} BIOS_REGISTERS, *PBIOS_REGISTERS;

C_ASSERT(FIELD_OFFSET(BIOS_REGISTERS, Eax) == BR_EAX);
C_ASSERT(FIELD_OFFSET(BIOS_REGISTERS, Ebx) == BR_EBX);
C_ASSERT(FIELD_OFFSET(BIOS_REGISTERS, Ecx) == BR_ECX);
C_ASSERT(FIELD_OFFSET(BIOS_REGISTERS, Edx) == BR_EDX);
C_ASSERT(FIELD_OFFSET(BIOS_REGISTERS, Esi) == BR_ESI);
C_ASSERT(FIELD_OFFSET(BIOS_REGISTERS, Edi) == BR_EDI);
C_ASSERT(FIELD_OFFSET(BIOS_REGISTERS, Ebp) == BR_EBP);
C_ASSERT(FIELD_OFFSET(BIOS_REGISTERS, Ds) == BR_DS);
C_ASSERT(FIELD_OFFSET(BIOS_REGISTERS, Es) == BR_ES);
C_ASSERT(FIELD_OFFSET(BIOS_REGISTERS, Eflags) == BR_EFLAGS);
C_ASSERT(sizeof(BIOS_REGISTERS) == BR_SIZE);

typedef VOID (__cdecl *PBIOS_CALL)(ULONG Interrupt, PBIOS_REGISTERS Registers);

typedef struct _BIOS_STARTUP_INFO
{
    ULONG Magic;
    ULONG Version;
    ULONG BootDrive;
    ULONG BootMedium;
    ULONGLONG PartitionStart;
    ULONG Call;
    ULONG FileSize;
    ULONG ImageBase;
    ULONG ImageSize;
    ULONG Entry;
    ULONG Reserved;
} BIOS_STARTUP_INFO, *PBIOS_STARTUP_INFO;

C_ASSERT(FIELD_OFFSET(BIOS_STARTUP_INFO, Magic) == BSI_MAGIC);
C_ASSERT(FIELD_OFFSET(BIOS_STARTUP_INFO, Version) == BSI_VERSION);
C_ASSERT(FIELD_OFFSET(BIOS_STARTUP_INFO, BootDrive) == BSI_BOOT_DRIVE);
C_ASSERT(FIELD_OFFSET(BIOS_STARTUP_INFO, BootMedium) == BSI_BOOT_MEDIUM);
C_ASSERT(FIELD_OFFSET(BIOS_STARTUP_INFO, PartitionStart) == BSI_PARTITION_START);
C_ASSERT(FIELD_OFFSET(BIOS_STARTUP_INFO, Call) == BSI_CALL);
C_ASSERT(FIELD_OFFSET(BIOS_STARTUP_INFO, FileSize) == BSI_FILE_SIZE);
C_ASSERT(FIELD_OFFSET(BIOS_STARTUP_INFO, ImageBase) == BSI_IMAGE_BASE);
C_ASSERT(FIELD_OFFSET(BIOS_STARTUP_INFO, ImageSize) == BSI_IMAGE_SIZE);
C_ASSERT(FIELD_OFFSET(BIOS_STARTUP_INFO, Entry) == BSI_ENTRY);
C_ASSERT(sizeof(BIOS_STARTUP_INFO) == BSI_SIZE);

#define BIOS_APPLICATION_MAGIC      0x50414942
#define BIOS_APPLICATION_VERSION    1
#define BIOS_MAX_ALLOCATIONS        2048
#define BIOS_MAX_MEMORY_RANGES      128
#define BIOS_PATH_CHARS             260

typedef struct _BIOS_MEMORY_RANGE
{
    ULONGLONG Base;
    ULONGLONG Length;
    ULONG Type;
    ULONG Attributes;
} BIOS_MEMORY_RANGE, *PBIOS_MEMORY_RANGE;

typedef struct _BIOS_ALLOCATION
{
    ULONGLONG Base;
    ULONGLONG Size;
    TYPE_OF_MEMORY Type;
    ULONG Reserved;
} BIOS_ALLOCATION, *PBIOS_ALLOCATION;

typedef struct _BIOS_HEAP_BLOCK
{
    SIZE_T Size;
    SIZE_T Free;
    struct _BIOS_HEAP_BLOCK* Next;
    SIZE_T Reserved;
} BIOS_HEAP_BLOCK, *PBIOS_HEAP_BLOCK;

typedef struct _BIOS_DISPLAY
{
    ULONG Mode;
    ULONG Width;
    ULONG Height;
    ULONG Pitch;
    ULONG BitsPerPixel;
    ULONG RedMask;
    ULONG GreenMask;
    ULONG BlueMask;
    ULONG ReservedMask;
    ULONGLONG FrameBuffer;
    ULONG FrameBufferSize;
} BIOS_DISPLAY, *PBIOS_DISPLAY;

typedef struct _BIOS_SHARED_STATE
{
    ULONG AllocationCount;
    BIOS_ALLOCATION Allocations[BIOS_MAX_ALLOCATIONS];
    PBIOS_HEAP_BLOCK Heap;
    ULONG RangeCount;
    BIOS_MEMORY_RANGE Ranges[BIOS_MAX_MEMORY_RANGES];
    ULONGLONG TscFrequency;
    BIOS_DISPLAY Display;
} BIOS_SHARED_STATE, *PBIOS_SHARED_STATE;

typedef struct _BIOS_APPLICATION_PARAMETERS
{
    ULONG Magic;
    ULONG Version;
    PBIOS_STARTUP_INFO Startup;
    PBIOS_SHARED_STATE Shared;
    PVOID ImageBase;
    ULONG ImageSize;
    ULONG BootDisk;
    ULONG BootPartition;
    PCWSTR LoadOptions;
    WCHAR ImagePath[BIOS_PATH_CHARS];
} BIOS_APPLICATION_PARAMETERS, *PBIOS_APPLICATION_PARAMETERS;

extern PBIOS_STARTUP_INFO BiosStartup;
extern PBIOS_SHARED_STATE BiosShared;
extern PBIOS_APPLICATION_PARAMETERS BiosParameters;

VOID
BiosCall(
    _In_ UCHAR Interrupt,
    _Inout_ PBIOS_REGISTERS Registers);

USHORT
BiosSegment(
    _In_ const VOID* Address);

USHORT
BiosOffset(
    _In_ const VOID* Address);

PVOID
BiosLinear(
    _In_ ULONG FarPointer);

ULONGLONG
BiosReadTsc(VOID);

VOID
BiosCalibrateTimer(VOID);

ULONG
BiosMilliseconds(VOID);

VOID
BiosMemoryInitialize(
    _In_ BOOLEAN Root);

BOOLEAN
BiosGetUsableRun(
    _In_ ULONG Index,
    _Out_ PPFN_NUMBER BasePage,
    _Out_ PPFN_NUMBER PageCount);

BOOLEAN
BiosGetFreeRun(
    _Inout_ PULONG Cursor,
    _Inout_ PPFN_NUMBER Position,
    _Out_ PPFN_NUMBER BasePage,
    _Out_ PPFN_NUMBER PageCount);

BOOLEAN
BiosReserveMemory(
    _In_ ULONGLONG Base,
    _In_ ULONGLONG Size,
    _In_ TYPE_OF_MEMORY Type);

VOID
BiosDiskInitialize(
    _In_ BOOLEAN Root);

VOID
BiosConsoleInitialize(
    _In_ BOOLEAN Root);

VOID
BiosConsoleWrite(
    _In_reads_(Length) PCSTR Text,
    _In_ ULONG Length);

BOOLEAN
BiosGetDisplay(
    _Out_ PBIOS_DISPLAY Display);

#endif
