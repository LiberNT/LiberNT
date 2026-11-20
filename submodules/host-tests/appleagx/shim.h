/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Environment for the Apple graphics firmware data tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef APPLEAGX_TEST_SHIM_H
#define APPLEAGX_TEST_SHIM_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define UNIT_TEST 1

#define _In_
#define _In_opt_
#define _In_z_
#define _Out_
#define _Out_opt_
#define _Inout_
#define _In_reads_(size)
#define _In_reads_bytes_(size)
#define _In_reads_bytes_opt_(size)
#define _Out_writes_(size)
#define _Out_writes_bytes_(size)

#define NTAPI
#define TRUE 1
#define FALSE 0

typedef void VOID;
typedef void *PVOID;
typedef const char *PCSTR;
typedef uint8_t UCHAR, *PUCHAR, BOOLEAN;
typedef uint16_t USHORT, *PUSHORT;
typedef uint32_t ULONG, *PULONG;
typedef int32_t LONG, *PLONG, NTSTATUS;
typedef uint64_t ULONG64, *PULONG64;
typedef size_t SIZE_T;

#define STATUS_SUCCESS                  ((NTSTATUS)0x00000000)
#define STATUS_DEVICE_BUSY              ((NTSTATUS)0x80000011)
#define STATUS_DATA_ERROR               ((NTSTATUS)0xC000003E)
#define STATUS_INVALID_DEVICE_STATE     ((NTSTATUS)0xC0000184)
#define NT_SUCCESS(Status)              ((NTSTATUS)(Status) >= 0)

#define ASSERT(Expression)              assert(Expression)
#define UNREFERENCED_PARAMETER(P)       ((void)(P))
#define RTL_NUMBER_OF(A)                (sizeof(A) / sizeof((A)[0]))
#define RtlCopyMemory(D, S, L)          memcpy((D), (S), (L))
#define RtlZeroMemory(D, L)             memset((D), 0, (L))
#define RtlFillMemory(D, L, F)          memset((D), (F), (L))
#define KeMemoryBarrier()               __sync_synchronize()
#define min(A, B)                       (((A) < (B)) ? (A) : (B))
#define max(A, B)                       (((A) > (B)) ? (A) : (B))

#endif
