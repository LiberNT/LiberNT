/*
 * PROJECT:     LiberNT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Calls between emulated guest code and native libraries of an emulation host
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _REACTOS_EMUHOST_H_
#define _REACTOS_EMUHOST_H_

#define EMU_HOST_SERVICE_BASE   0x40000000
#define EMU_HOST_SERVICE_LOAD   (EMU_HOST_SERVICE_BASE + 0)
#define EMU_HOST_SERVICE_RETURN (EMU_HOST_SERVICE_BASE + 1)
#define EMU_HOST_SERVICE_THUNK  (EMU_HOST_SERVICE_BASE + 16)

#define EMU_HOST_INITIALIZE_EXPORT "EmuHostInitialize"
#define EMU_HOST_LIBRARY           L"amd64emu.dll"
#define EMU_HOST_INTERFACE_EXPORT  "EmuHostGetInterface"

#define EMU_HOST_TYPE_VOID   'v'
#define EMU_HOST_TYPE_INT64  'q'
#define EMU_HOST_TYPE_INT32  'w'
#define EMU_HOST_TYPE_INT16  'h'
#define EMU_HOST_TYPE_UINT16 'H'
#define EMU_HOST_TYPE_INT8   'b'
#define EMU_HOST_TYPE_UINT8  'B'
#define EMU_HOST_TYPE_FLOAT  'f'
#define EMU_HOST_TYPE_DOUBLE 'd'
#define EMU_HOST_TYPE_PAIR   'P'

#define EMU_HOST_MAX_ARGUMENTS 24

#define EMU_HOST_DESCRIPTOR_SIZE        16
#define EMU_HOST_DESCRIPTOR_MAGIC       0x5852
#define EMU_HOST_DESCRIPTOR_VARIADIC    0x10
#define EMU_HOST_DESCRIPTOR_STRUCT      0x20
#define EMU_HOST_DESCRIPTOR_UNSUPPORTED 0x40

typedef struct _EMU_HOST_INTERFACE
{
    ULONG Size;
    ULONG64 (NTAPI *GuestThunk)(_In_ PVOID Native, _In_ PCSTR Signature);
    ULONG64 (NTAPI *CallGuest)(_In_ ULONG64 Function, _In_ PCSTR Signature, _In_opt_ const ULONG64 *Arguments);
    ULONG64 (NTAPI *GuestProcedure)(_In_ PCSTR Module, _In_ PCSTR Name);
    ULONG64 (NTAPI *CallDescribed)(_In_ ULONG64 Function, _In_ ULONG64 DescriptorLow, _In_ ULONG64 DescriptorHigh,
                                   _In_opt_ const ULONG64 *Arguments);
} EMU_HOST_INTERFACE, *PEMU_HOST_INTERFACE;

typedef ULONG64 (NTAPI *PEMU_HOST_INITIALIZE)(VOID);

const EMU_HOST_INTERFACE *
NTAPI
EmuHostGetInterface(VOID);

#endif
