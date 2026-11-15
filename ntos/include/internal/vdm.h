/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Virtual DOS machine internals
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define TRAMPOLINE_BASE                                 0x10000
#define TRAMPOLINE_TIB                                  0x12000
#define TRAMPOLINE_TEB                                  0x13000
#define TRAMPOLINE_BOP                                  0xFEC4C4

typedef enum _VdmEventClass
{
    VdmIO,
    VdmStringIO,
    VdmMemAccess,
    VdmIntAck,
    VdmBop,
    VdmError,
    VdmIrq13
} VDMEVENTCLASS, *PVDMEVENTCLASS;

typedef struct _Vdm_InterruptHandler
{
    USHORT CsSelector;
    USHORT Flags;
    ULONG Eip;
} VDM_INTERRUPTHANDLER, *PVDM_INTERRUPTHANDLER;

typedef struct _Vdm_FaultHandler
{
    USHORT CsSelector;
    USHORT SsSelector;
    ULONG Eip;
    ULONG Esp;
    ULONG Flags;
} VDM_FAULTHANDLER, *PVDM_FAULTHANDLER;

typedef struct _VdmEventInfo
{
    ULONG Size;
    VDMEVENTCLASS Event;
    ULONG InstructionSize;
    union
    {
        ULONG BopNumber;

        LONG ErrorStatus;
        ULONG IntAckInfo;
    };
} VDMEVENTINFO, *PVDMEVENTINFO;

typedef struct _Vdm_Printer_Info
{
    PUCHAR prt_state;

} VDM_PRINTER_INFO, *PVDM_PRINTER_INFO;

typedef struct _VdmTraceInfo
{
    UCHAR Flags;

} VDMTRACEINFO, *PVDMTRACEINFO;

typedef struct _tagFAMILY_TABLE
{
    INT numHookedAPIs;

} FAMILY_TABLE, *PFAMILY_TABLE;

typedef struct _Vdm_Tib
{
    ULONG Size;
    PVDM_INTERRUPTHANDLER VdmInterruptTable;
    PVDM_FAULTHANDLER VdmFaultTable;
    CONTEXT MonitorContext;
    CONTEXT VdmContext;
    VDMEVENTINFO EventInfo;
    VDM_PRINTER_INFO PrinterInfo;
    ULONG TempArea1[2];
    ULONG TempArea2[2];
    VDMTRACEINFO TraceInfo;
    ULONG IntelMSW;
    LONG NumTasks;
    PFAMILY_TABLE *pDpmFamTbls;
    BOOLEAN ContinueExecution;
} VDM_TIB, *PVDM_TIB;

typedef struct _VDM_PROCESS_OBJECTS
{
    PVOID VdmIoListHead;
    KAPC QueuedIntApc;
    KAPC QueuedIntUserApc;
    FAST_MUTEX DelayIntFastMutex;
    KSPIN_LOCK DelayIntSpinLock;
    LIST_ENTRY DelayIntListHead;
    PVOID pIcaUserData;
    PETHREAD MainThread;
    PVDM_TIB VdmTib;
    UCHAR PrinterState;
    UCHAR PrinterControl;
    UCHAR PrinterStatus;
    UCHAR PrinterHostState;
    USHORT AdlibStatus;
    USHORT AdlibIndexRegister;
    USHORT AdlibPhysPortStart;
    USHORT AdlibPhysPortEnd;
    USHORT AdlibVirtPortStart;
    USHORT AdlibVirtPortEnd;
    USHORT AdlibAction;
    USHORT VdmControl;
    ULONG PMCliTimeStamp;
} VDM_PROCESS_OBJECTS, *PVDM_PROCESS_OBJECTS;

NTSTATUS
NTAPI
VdmpStartExecution(VOID);
