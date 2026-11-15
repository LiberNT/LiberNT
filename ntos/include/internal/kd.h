/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel debugger provider internals
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once
#include <cportlib/cportlib.h>

#ifdef _NTOSKRNL_

typedef enum _KDP_DEBUG_SERVICE
{
    DumpNonPagedPool = 0x1e,
    ManualBugCheck = 0x30,
    DumpNonPagedPoolStats = 0x2e,
    DumpNewNonPagedPool = 0x20,
    DumpNewNonPagedPoolStats = 0x12,
    DumpAllThreads = 0x21,
    DumpUserThreads = 0x22,
    KdSpare1 = 0x23,
    KdSpare2 = 0x17,
    KdSpare3 = 0x24,
    EnterDebugger = 0x25,
    ThatsWhatSheSaid = 69
} KDP_DEBUG_SERVICE;

typedef VOID (NTAPI *PKDP_EARLY_SCREEN_ROUTINE)(_In_ PCCH String, _In_ ULONG Length);
extern PKDP_EARLY_SCREEN_ROUTINE KdpEarlyScreenRoutine;

#endif

#if DBG && defined(_M_IX86) && !defined(_WINKD_)
#define ID_Win32PreServiceHook 'WSH0'
#define ID_Win32PostServiceHook 'WSH1'
typedef void (NTAPI *PKDBG_PRESERVICEHOOK)(ULONG, PULONG_PTR);
typedef ULONG_PTR (NTAPI *PKDBG_POSTSERVICEHOOK)(ULONG, ULONG_PTR);
extern PKDBG_PRESERVICEHOOK KeWin32PreServiceHook;
extern PKDBG_POSTSERVICEHOOK KeWin32PostServiceHook;
#endif

BOOLEAN
NTAPI
KdPortGetByteEx(
    PCPPORT PortInformation,
    PUCHAR ByteReceived);

BOOLEAN
NTAPI
KdPortInitializeEx(
    PCPPORT PortInformation,
    ULONG ComPortNumber);

BOOLEAN
NTAPI
KdPortIsComPortNumberValid(
    ULONG ComPortNumber);

VOID
NTAPI
KdPortPutBufferEx(
    PCPPORT PortInformation,
    PCCH Buffer,
    ULONG Length);

VOID
NTAPI
KdPortPutByteEx(
    PCPPORT PortInformation,
    UCHAR ByteToSend);
