/*
 * PROJECT:     LiberNT NDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM (Thumb-2) memory manager types
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * The ARMv7 port translates through LPAE long descriptors (ARM ARM B3.6), so a
 * hardware PTE is 64 bits wide, unlike the WDK's short-descriptor ARM layout.
 */

#ifndef _ARM_MMTYPES_H
#define _ARM_MMTYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PAGE_SIZE
#define PAGE_SIZE                       0x1000
#endif
#define PAGE_SHIFT                      12L
#define MM_ALLOCATION_GRANULARITY       0x10000
#define MM_ALLOCATION_GRANULARITY_SHIFT 16L
#define MM_PAGE_FRAME_NUMBER_SIZE       28
#define MM_SHARED_USER_DATA_VA          0x7FFE0000
#define MI_HIGHEST_USER_ADDRESS         ((PVOID)0x7FFEFFFF)

#if !defined(RC_INVOKED)
extern NTSYSAPI ULONG MmUserProbeAddress;
#define MM_USER_PROBE_ADDRESS MmUserProbeAddress
extern NTSYSAPI PVOID MmSystemRangeStart;
#define MM_SYSTEM_RANGE_START MmSystemRangeStart
extern NTSYSAPI PVOID MmHighestUserAddress;
#define MM_HIGHEST_USER_ADDRESS MmHighestUserAddress
#endif

/* Long-descriptor page/block entry, stage 1 (ARM ARM B3.6.2). */
typedef struct _HARDWARE_PTE
{
    ULONGLONG Valid : 1;
    ULONGLONG NotLargePage : 1;
    ULONGLONG AttrIndex : 3;
    ULONGLONG NonSecure : 1;
    ULONGLONG Owner : 1;
    ULONGLONG ReadOnly : 1;
    ULONGLONG Shareability : 2;
    ULONGLONG Accessed : 1;
    ULONGLONG NonGlobal : 1;
    ULONGLONG PageFrameNumber : 28;
    ULONGLONG Reserved : 12;
    ULONGLONG Contiguous : 1;
    ULONGLONG PrivilegedNoExecute : 1;
    ULONGLONG UserNoExecute : 1;
    ULONGLONG Software : 4;
    ULONGLONG Ignored : 5;
} HARDWARE_PTE, *PHARDWARE_PTE;

C_ASSERT(sizeof(HARDWARE_PTE) == sizeof(ULONGLONG));

typedef struct _MMPTE_HARDWARE
{
    ULONGLONG Valid : 1;
    ULONGLONG NotLargePage : 1;
    ULONGLONG AttrIndex : 3;
    ULONGLONG NonSecure : 1;
    ULONGLONG Owner : 1;
    ULONGLONG ReadOnly : 1;
    ULONGLONG Shareability : 2;
    ULONGLONG Accessed : 1;
    ULONGLONG NonGlobal : 1;
    ULONGLONG PageFrameNumber : 28;
    ULONGLONG Reserved : 12;
    ULONGLONG Contiguous : 1;
    ULONGLONG PrivilegedNoExecute : 1;
    ULONGLONG UserNoExecute : 1;
    ULONGLONG Writable : 1;
    ULONGLONG CopyOnWrite : 1;
    ULONGLONG Software : 2;
    ULONGLONG Ignored : 5;
} MMPTE_HARDWARE, *PMMPTE_HARDWARE;

typedef struct _MMPTE
{
    union
    {
        ULONGLONG Long;
        HARDWARE_PTE Flush;
        MMPTE_HARDWARE Hard;
    } u;
} MMPTE, *PMMPTE, MMPDE, *PMMPDE;

C_ASSERT(sizeof(MMPTE) == sizeof(ULONGLONG));

#ifdef __cplusplus
}
#endif

#endif /* _ARM_MMTYPES_H */
