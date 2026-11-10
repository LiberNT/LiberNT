/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            ntoskrnl/kd/kdio.c
 * PURPOSE:         NT Kernel Debugger Input/Output Functions
 *
 * PROGRAMMERS:     Alex Ionescu (alex@relsoft.net)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include <reactos/buildno.h>
#include "kd.h"
#include "kdterminal.h"
#ifdef KDBG
#include "../kdbg/kdb.h"
#endif
#include <cportlib/uartinfo.h>

#define NDEBUG
#include <debug.h>

#undef KdSendPacket
#undef KdReceivePacket

/* GLOBALS *******************************************************************/

static KSPIN_LOCK KdpSerialSpinLock;
ULONG  SerialPortNumber = DEFAULT_DEBUG_PORT;
CPPORT SerialPortInfo   = {0, DEFAULT_DEBUG_BAUD_RATE, 0};

#define KdpScreenLineLengthDefault 80
#define KdpScreenLineLengthMaximum 2048
static CHAR KdpScreenLineBuffer[KdpScreenLineLengthMaximum + 1] = "";
static ULONG KdpScreenLineBufferPos = 0, KdpScreenLineLength = 0;
static ULONG KdpScreenLineCapacity = KdpScreenLineLengthDefault;
static BOOLEAN KdpScreenInitialized = FALSE;
#define KdpScreenHistorySize (64 * 1024)
static CHAR KdpScreenHistory[KdpScreenHistorySize];
static ULONG KdpScreenHistoryEnd = 0;
static BOOLEAN KdpScreenHistoryWrapped = FALSE;
static KSPIN_LOCK KdpScreenSpinLock;

KDP_DEBUG_MODE KdpDebugMode;
LIST_ENTRY KdProviders = {&KdProviders, &KdProviders};
KD_DISPATCH_TABLE DispatchTable[KdMax] = {0};

PKDP_INIT_ROUTINE InitRoutines[KdMax] =
{
    KdpScreenInit,
    KdpSerialInit,
#ifdef KDBG // See kdb_cli.c
    KdpKdbgInit,
#endif
    KdpNetInit
};

/* LOCKING FUNCTIONS *********************************************************/

BOOLEAN
NTAPI
KdbpAcquireLock(
    _In_ PKSPIN_LOCK SpinLock,
    _Out_ PKIRQL OldIrql)
{
    /* A frozen processor may hold the lock, so debugger and bugcheck paths
     * must not wait for a lock that can no longer be released. */
    if (KdEnteredDebugger || KeBugCheckActive)
    {
        KeRaiseIrql(HIGH_LEVEL, OldIrql);
        return KeTryToAcquireSpinLockAtDpcLevel(SpinLock);
    }

    /* Acquire the spinlock without waiting at raised IRQL */
    while (TRUE)
    {
        /* Loop until the spinlock becomes available */
        while (!KeTestSpinLock(SpinLock));

        /* Spinlock is free, raise IRQL to high level */
        KeRaiseIrql(HIGH_LEVEL, OldIrql);

        /* Try to get the spinlock */
        if (KeTryToAcquireSpinLockAtDpcLevel(SpinLock))
            break;

        /* Someone else got the spinlock, lower IRQL back */
        KeLowerIrql(*OldIrql);
    }

    return TRUE;
}

VOID
NTAPI
KdbpReleaseLock(
    _In_ PKSPIN_LOCK SpinLock,
    _In_ KIRQL OldIrql,
    _In_ BOOLEAN LockAcquired)
{
    /* Release the spinlock if it was acquired */
    if (LockAcquired)
    {
        KiReleaseSpinLock(SpinLock);
        // KeReleaseSpinLockFromDpcLevel(SpinLock);
    }

    /* Restore the old IRQL */
    KeLowerIrql(OldIrql);
}

/* SERIAL FUNCTIONS **********************************************************/

static VOID
NTAPI
KdpSerialPrint(
    _In_ PCCH String,
    _In_ ULONG Length)
{
    BOOLEAN LockAcquired;
    PCCH pch = String;
    PCCH End = String + Length;
    PCCH Run;
    KIRQL OldIrql;

    /* Acquire the printing spinlock without waiting at raised IRQL */
    LockAcquired = KdbpAcquireLock(&KdpSerialSpinLock, &OldIrql);

    /* Output unlocked rather than deadlock if the lock is unavailable.
     * Send the longest newline-free run as one buffer write, so transports
     * with per-transfer command cost pay it per run instead of per byte. */
    while (pch < End && *pch)
    {
        Run = pch;
        while (pch < End && *pch && *pch != '\n') ++pch;
        if (pch > Run)
        {
            KdPortPutBufferEx(&SerialPortInfo, Run, (ULONG)(pch - Run));
        }
        if (pch < End && *pch == '\n')
        {
            KdPortPutBufferEx(&SerialPortInfo, "\r\n", 2);
            ++pch;
        }
    }

    /* Release the spinlock */
    KdbpReleaseLock(&KdpSerialSpinLock, OldIrql, LockAcquired);
}

NTSTATUS
NTAPI
KdpSerialInit(
    _In_ PKD_DISPATCH_TABLE DispatchTable,
    _In_ ULONG BootPhase)
{
    if (!KdpDebugMode.Serial)
        return STATUS_PORT_DISCONNECTED;

    if (BootPhase == 0)
    {
        /* Write out the functions that we support for now */
        DispatchTable->KdpPrintRoutine = KdpSerialPrint;

        /* Initialize the Port */
        if (!KdPortInitializeEx(&SerialPortInfo, SerialPortNumber))
        {
            KdpDebugMode.Serial = FALSE;
            return STATUS_DEVICE_DOES_NOT_EXIST;
        }
        KdComPortInUse = SerialPortInfo.Address;

        /* Initialize spinlock */
        KeInitializeSpinLock(&KdpSerialSpinLock);

        /* Register for BootPhase 1 initialization and as a Provider */
        DispatchTable->KdpInitRoutine = KdpSerialInit;
        InsertTailList(&KdProviders, &DispatchTable->KdProvidersList);
    }
    else if (BootPhase == 1)
    {
        /* Announce ourselves */
        HalDisplayString("   Serial debugging enabled\r\n");
    }

    return STATUS_SUCCESS;
}

/* SCREEN FUNCTIONS **********************************************************/

static BOOLEAN
KdpScreenAcquireDisplay(
    _Out_ PBOOLEAN InitializeDisplay)
{
    BOOLEAN DisplayAcquired = FALSE;
    VID_DISPLAY_INFO DisplayInfo;

    *InitializeDisplay = FALSE;
    if (InbvIsBootDriverInstalled())
    {
        InbvUseCompactFont();
        InbvQueryDisplayInfo(&DisplayInfo);
        if (DisplayInfo.CharacterWidth)
        {
            KdpScreenLineCapacity = min(DisplayInfo.Width / DisplayInfo.CharacterWidth, KdpScreenLineLengthMaximum);
        }
        if (!KdpScreenLineCapacity)
            KdpScreenLineCapacity = KdpScreenLineLengthDefault;

        DisplayAcquired = (InbvGetDisplayState() != INBV_DISPLAY_STATE_OWNED);
        *InitializeDisplay = DisplayAcquired || !KdpScreenInitialized;

        /* Acquire ownership and initialize a new screen session */
        InbvAcquireDisplayOwnership();
        if (*InitializeDisplay)
        {
            InbvResetDisplay();
            InbvSolidColorFill(0, 0, DisplayInfo.Width - 1, DisplayInfo.Height - 1, BV_COLOR_BLACK);
        }

        InbvSetTextColor(BV_COLOR_WHITE);
        InbvInstallDisplayStringFilter(NULL);
        InbvEnableDisplayString(TRUE);
        if (*InitializeDisplay)
        {
            InbvSetScrollRegion(0, 0, DisplayInfo.Width - 1, DisplayInfo.Height - 1);
        }

        KdpScreenInitialized = TRUE;
    }

    return DisplayAcquired;
}

BOOLEAN
NTAPI
KdShowShutdownLog(VOID)
{
    if (!KdpDebugMode.Screen || !InbvIsBootDriverInstalled())
        return FALSE;

    (VOID)KdpScreenAcquire();
    return TRUE;
}

VOID
NTAPI
KdEnableBugCheckDebugger(VOID)
{
#ifdef KDBG
    static BOOLEAN Prepared = FALSE;

    if (KdPitchDebugger || (Prepared && KdDebuggerEnabled))
        return;

    if (!KdpDebugMode.Screen && InbvIsBootDriverInstalled())
    {
        KdpDebugMode.Screen = TRUE;
        if (InbvGetDisplayState() == INBV_DISPLAY_STATE_OWNED)
            KdpScreenInitialized = TRUE;
    }

    if (!KdpDebugMode.Serial)
    {
        KdpDebugMode.Serial = TRUE;
        SerialPortNumber = 1;
    }

    if (KdDebuggerEnabled)
        KdDebuggerInitialize0(NULL);
    else if (NT_SUCCESS(KdEnableDebuggerWithLock(FALSE)) && !KdDebuggerEnabled)
        KdInitSystem(0, NULL);

    if (!KdDebuggerEnabled)
        return;

    Prepared = TRUE;
    KdDebuggerNotPresent = FALSE;
    MmWriteableSharedUserData->KdDebuggerEnabled |= 0x00000002;
#endif
}

// extern VOID NTAPI InbvSetDisplayOwnership(IN BOOLEAN DisplayOwned);

VOID
KdpScreenRelease(
    _In_ BOOLEAN DisplayAcquired)
{
    if (DisplayAcquired &&
        InbvIsBootDriverInstalled() &&
        InbvCheckDisplayOwnership())
    {
        /* Release the display */
        // InbvSetDisplayOwnership(FALSE);
        InbvNotifyDisplayOwnershipLost(NULL);
        InbvRestoreDisplaySnapshot();
    }
}

static VOID
KdpScreenOutput(
    _In_ PCCH String,
    _In_ ULONG Length)
{
    PCCH pch = String;

    while (pch < String + Length && *pch)
    {
        if (*pch == '\b')
        {
            /* HalDisplayString does not support '\b'. Workaround it and use '\r' */
            if (KdpScreenLineLength > 0)
            {
                /* Remove last character from buffer */
                KdpScreenLineBuffer[--KdpScreenLineLength] = '\0';
                KdpScreenLineBufferPos = KdpScreenLineLength;

                /* Clear row and print line again */
                HalDisplayString("\r");
                HalDisplayString(KdpScreenLineBuffer);
            }
        }
        else
        {
            KdpScreenLineBuffer[KdpScreenLineLength++] = *pch;
            KdpScreenLineBuffer[KdpScreenLineLength] = '\0';
        }

        if (*pch == '\n' || KdpScreenLineLength >= KdpScreenLineCapacity)
        {
            /* Print buffered characters */
            if (KdpScreenLineBufferPos != KdpScreenLineLength)
                HalDisplayString(KdpScreenLineBuffer + KdpScreenLineBufferPos);

            /* Clear line buffer */
            KdpScreenLineBuffer[0] = '\0';
            KdpScreenLineLength = KdpScreenLineBufferPos = 0;
        }

        ++pch;
    }

    /* Print buffered characters */
    if (KdpScreenLineBufferPos != KdpScreenLineLength)
    {
        HalDisplayString(KdpScreenLineBuffer + KdpScreenLineBufferPos);
        KdpScreenLineBufferPos = KdpScreenLineLength;
    }
}

static ULONG
KdpScreenRecord(
    _In_ PCCH String,
    _In_ ULONG Length)
{
    ULONG Recorded = 0;
    ULONG Chunk;

    while ((Recorded < Length) && String[Recorded])
        ++Recorded;

    for (Length = Recorded; Length != 0; Length -= Chunk)
    {
        Chunk = min(Length, KdpScreenHistorySize - KdpScreenHistoryEnd);
        RtlCopyMemory(KdpScreenHistory + KdpScreenHistoryEnd, String, Chunk);
        String += Chunk;
        KdpScreenHistoryEnd += Chunk;
        if (KdpScreenHistoryEnd == KdpScreenHistorySize)
        {
            KdpScreenHistoryEnd = 0;
            KdpScreenHistoryWrapped = TRUE;
        }
    }

    return Recorded;
}

static ULONG
KdpScreenLineRows(
    _In_ ULONG LineLength)
{
    ULONG Rows = (LineLength + KdpScreenLineCapacity - 1) / KdpScreenLineCapacity;

    return Rows ? Rows : 1;
}

static VOID
KdpScreenReplay(VOID)
{
    VID_DISPLAY_INFO DisplayInfo;
    ULONG Size, First, Start, Index, LineLength, Rows, MaxRows, Chunk;

    InbvQueryDisplayInfo(&DisplayInfo);
    MaxRows = DisplayInfo.CharacterHeight ? DisplayInfo.Height / DisplayInfo.CharacterHeight : 0;
    if (MaxRows != 0)
        --MaxRows;

    Size = KdpScreenHistoryWrapped ? KdpScreenHistorySize : KdpScreenHistoryEnd;
    First = KdpScreenHistoryWrapped ? KdpScreenHistoryEnd : 0;
    Start = Size;
    Rows = 0;
    LineLength = 0;

    for (Index = Size; Index != 0; --Index)
    {
        if (KdpScreenHistory[(First + Index - 1) % KdpScreenHistorySize] != '\n')
        {
            ++LineLength;
        }
        else if (Index != Size)
        {
            Rows += KdpScreenLineRows(LineLength);
            if (Rows > MaxRows)
                break;

            Start = Index;
            LineLength = 0;
        }
    }

    if ((Index == 0) && !KdpScreenHistoryWrapped &&
        (Rows + KdpScreenLineRows(LineLength) <= MaxRows))
    {
        Start = 0;
    }

    KdpScreenLineBuffer[0] = '\0';
    KdpScreenLineLength = KdpScreenLineBufferPos = 0;

    Index = (First + Start) % KdpScreenHistorySize;
    Size -= Start;
    Chunk = min(Size, KdpScreenHistorySize - Index);
    KdpScreenOutput(KdpScreenHistory + Index, Chunk);
    if (Size > Chunk)
        KdpScreenOutput(KdpScreenHistory, Size - Chunk);
}

BOOLEAN
KdpScreenAcquire(VOID)
{
    BOOLEAN DisplayAcquired, InitializeDisplay, LockAcquired;
    KIRQL OldIrql;

    LockAcquired = KdbpAcquireLock(&KdpScreenSpinLock, &OldIrql);

    DisplayAcquired = KdpScreenAcquireDisplay(&InitializeDisplay);
    if (InitializeDisplay)
        KdpScreenReplay();

    KdbpReleaseLock(&KdpScreenSpinLock, OldIrql, LockAcquired);
    return DisplayAcquired;
}

static VOID
KdpScreenRegister(
    _In_ PKD_DISPATCH_TABLE Table);

/* A stop the machine hit by itself takes the display on every boot profile; one a debugger asked for needs DEBUGPORT=SCREEN. */
BOOLEAN
KdpScreenEnterStop(
    _In_ BOOLEAN Requested)
{
    BOOLEAN DisplayAcquired;
    ULONG Length;

    if (!KdpDebugMode.Screen)
    {
        /* TODO: a native display driver keeps showing the frozen desktop: it gives the display back for good, which only a bugcheck can afford */
        if (Requested || !InbvHasValidGopFrameBuffer() || !InbvIsBootDriverInstalled() || InbvDisplayNeedsDriverReset())
            return FALSE;

        if (DispatchTable[KdScreen].KdpPrintRoutine == NULL)
        {
            Length = KdpCopyPrintBufferTail(KdpScreenHistory, KdpScreenHistorySize);
            KdpScreenHistoryEnd = Length % KdpScreenHistorySize;
            KdpScreenHistoryWrapped = (Length == KdpScreenHistorySize);
            KdpScreenRegister(&DispatchTable[KdScreen]);
        }
    }

    if (InbvGetDisplayState() == INBV_DISPLAY_STATE_LOST)
        InbvSaveDisplaySnapshot();

    DisplayAcquired = KdpScreenAcquire();
    if (KdpScreenLineLength != 0)
        HalDisplayString("\r\n");
    KdpNetDisplayStatus();
    return DisplayAcquired;
}

static VOID
NTAPI
KdpScreenPrint(
    _In_ PCCH String,
    _In_ ULONG Length)
{
    BOOLEAN LockAcquired, InitializeDisplay;
    KIRQL OldIrql;

    LockAcquired = KdbpAcquireLock(&KdpScreenSpinLock, &OldIrql);

    Length = KdpScreenRecord(String, Length);
    if (!KdpScreenInitialized && InbvIsBootDriverInstalled())
    {
        (VOID)KdpScreenAcquireDisplay(&InitializeDisplay);
        KdpScreenReplay();
    }
    else
    {
        KdpScreenOutput(String, Length);
    }

    KdbpReleaseLock(&KdpScreenSpinLock, OldIrql, LockAcquired);
}

static VOID
KdpScreenRegister(
    _In_ PKD_DISPATCH_TABLE Table)
{
    Table->KdpPrintRoutine = KdpScreenPrint;
    KeInitializeSpinLock(&KdpScreenSpinLock);
    Table->KdpInitRoutine = KdpScreenInit;
    InsertTailList(&KdProviders, &Table->KdProvidersList);
}

NTSTATUS
NTAPI
KdpScreenInit(
    _In_ PKD_DISPATCH_TABLE DispatchTable,
    _In_ ULONG BootPhase)
{
    if (!KdpDebugMode.Screen)
        return STATUS_PORT_DISCONNECTED;

    if (BootPhase == 0)
    {
        KdpScreenRegister(DispatchTable);
    }
    else if (BootPhase == 1)
    {
        /* Take control of the display */
        (VOID)KdpScreenAcquire();

        /* Announce ourselves */
        HalDisplayString("   Screen debugging enabled\r\n");
    }

    return STATUS_SUCCESS;
}


/* GENERAL FUNCTIONS *********************************************************/

static VOID
KdIoPrintString(
    _In_ PCCH String,
    _In_ ULONG Length)
{
    PLIST_ENTRY CurrentEntry;
    PKD_DISPATCH_TABLE CurrentTable;

    /* Call the registered providers */
    for (CurrentEntry = KdProviders.Flink;
         CurrentEntry != &KdProviders;
         CurrentEntry = CurrentEntry->Flink)
    {
        CurrentTable = CONTAINING_RECORD(CurrentEntry,
                                         KD_DISPATCH_TABLE,
                                         KdProvidersList);

        CurrentTable->KdpPrintRoutine(String, Length);
    }
}

VOID
KdIoPuts(
    _In_ PCSTR String)
{
    KdIoPrintString(String, (ULONG)strlen(String));
}

VOID
__cdecl
KdIoPrintf(
    _In_ PCSTR Format,
    ...)
{
    va_list ap;
    ULONG Length;
    CHAR Buffer[512];

    /* Format the string */
    va_start(ap, Format);
    Length = (ULONG)_vsnprintf(Buffer,
                               sizeof(Buffer),
                               Format,
                               ap);
    va_end(ap);

    /* Send it to the display providers */
    KdIoPrintString(Buffer, Length);
}

#ifdef KDBG
extern const CSTRING KdbPromptStr;
#endif

#define KDP_FILE_IO_TIMEOUT_POLLS 100000
#define KDP_FILE_IO_NET_TIMEOUT_POLLS 400000
#define KDP_FILE_IO_TIMEOUT_MS 1000
#define KDP_FILE_IO_NET_TIMEOUT_MS 4000

static BOOLEAN KdpFileIoHostAbsent = FALSE;
static ULONG KdpFileIoPacketId = INITIAL_PACKET_ID;
static ULONG KdpFileIoApiNumber;

static
ULONG
KdpFileIoChecksum(
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    PUCHAR Byte = Buffer;
    ULONG Checksum = 0;

    while (Length-- > 0)
        Checksum += *Byte++;

    return Checksum;
}

static
BOOLEAN
KdpFileIoReceive(
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    PUCHAR Byte = Buffer;
    ULONG Polls;
    LARGE_INTEGER Frequency;
    LONGLONG Deadline;

    while (Length-- > 0)
    {
        Deadline = KeQueryPerformanceCounter(&Frequency).QuadPart +
                   Frequency.QuadPart * (KdpDebugMode.Net ? KDP_FILE_IO_NET_TIMEOUT_MS : KDP_FILE_IO_TIMEOUT_MS) / 1000;
        for (Polls = 0; !(KdpDebugMode.Net ? KdpNetGetByte(Byte) : KdPortGetByteEx(&SerialPortInfo, Byte)); Polls++)
        {
            if (Polls == (KdpDebugMode.Net ? KDP_FILE_IO_NET_TIMEOUT_POLLS : KDP_FILE_IO_TIMEOUT_POLLS) ||
                KeQueryPerformanceCounter(NULL).QuadPart >= Deadline)
            {
                return FALSE;
            }
            KeStallExecutionProcessor(10);
        }
        Byte++;
    }

    return TRUE;
}

static
BOOLEAN
KdpFileIoTransportReady(VOID)
{
    if (!KdpDebugMode.Net)
        return KdpDebugMode.Serial;

    /* A network host that attaches later gets a new chance to answer */
    if (!KdpNetQueryAttached(NULL, NULL))
    {
        KdpFileIoHostAbsent = FALSE;
        return FALSE;
    }

    return TRUE;
}

static UCHAR KdpFileIoNetPacket[sizeof(KD_PACKET) + PACKET_MAX_SIZE + sizeof(UCHAR)];

VOID
KdpResetFileIoHost(VOID)
{
    KdpFileIoHostAbsent = FALSE;
}

static
VOID
KdpSendFileIoPacket(
    _In_ PSTRING MessageHeader,
    _In_opt_ PSTRING MessageData)
{
    KD_PACKET Packet;
    UCHAR Trailer = PACKET_TRAILING_BYTE;

    if (!KdpFileIoTransportReady() || KdpFileIoHostAbsent)
        return;

    KdpFileIoApiNumber = ((PDBGKD_FILE_IO)MessageHeader->Buffer)->ApiNumber;

    Packet.PacketLeader = PACKET_LEADER;
    Packet.PacketType = PACKET_TYPE_KD_FILE_IO;
    Packet.ByteCount = MessageHeader->Length;
    Packet.PacketId = ++KdpFileIoPacketId;
    Packet.Checksum = KdpFileIoChecksum(MessageHeader->Buffer, MessageHeader->Length);
    if (MessageData)
    {
        Packet.ByteCount += MessageData->Length;
        Packet.Checksum += KdpFileIoChecksum(MessageData->Buffer, MessageData->Length);
    }

    if (KdpDebugMode.Net)
    {
        ULONG Length = 0;

        if (sizeof(Packet) + Packet.ByteCount + sizeof(Trailer) > sizeof(KdpFileIoNetPacket))
            return;
        RtlCopyMemory(KdpFileIoNetPacket, &Packet, sizeof(Packet));
        Length += sizeof(Packet);
        RtlCopyMemory(KdpFileIoNetPacket + Length, MessageHeader->Buffer, MessageHeader->Length);
        Length += MessageHeader->Length;
        if (MessageData)
        {
            RtlCopyMemory(KdpFileIoNetPacket + Length, MessageData->Buffer, MessageData->Length);
            Length += MessageData->Length;
        }
        KdpFileIoNetPacket[Length++] = Trailer;
        KdpNetSendBytes((PCCH)KdpFileIoNetPacket, Length);
        return;
    }

    KdPortPutBufferEx(&SerialPortInfo, (PCCH)&Packet, sizeof(Packet));
    KdPortPutBufferEx(&SerialPortInfo, MessageHeader->Buffer, MessageHeader->Length);
    if (MessageData)
        KdPortPutBufferEx(&SerialPortInfo, MessageData->Buffer, MessageData->Length);
    KdPortPutBufferEx(&SerialPortInfo, (PCCH)&Trailer, sizeof(Trailer));
}

static
KDSTATUS
KdpReceiveFileIoPacket(
    _Out_ PSTRING MessageHeader,
    _Out_ PSTRING MessageData,
    _Out_ PULONG DataLength)
{
    KD_PACKET Packet;
    ULONG Matched = 0, Checksum;
    UCHAR Byte;

    if (!KdpFileIoTransportReady() || KdpFileIoHostAbsent)
        return KdPacketTimedOut;

    do
    {
        Matched = 0;
        while (Matched < sizeof(Packet.PacketLeader))
        {
            if (!KdpFileIoReceive(&Byte, sizeof(Byte)))
            {
                if (KdpFileIoApiNumber != DbgKdCreateFileApi)
                    return KdPacketNeedsResend;

                KdpFileIoHostAbsent = TRUE;
                return KdPacketTimedOut;
            }
            Matched = (Byte == PACKET_LEADER_BYTE) ? Matched + 1 : 0;
        }

        if (!KdpFileIoReceive(&Packet.PacketType,
                              sizeof(Packet) - FIELD_OFFSET(KD_PACKET, PacketType)))
        {
            return KdPacketNeedsResend;
        }
    } while (Packet.PacketId != KdpFileIoPacketId);

    if ((Packet.PacketType != PACKET_TYPE_KD_FILE_IO) ||
        (Packet.ByteCount < MessageHeader->MaximumLength) ||
        (Packet.ByteCount - MessageHeader->MaximumLength > MessageData->MaximumLength))
    {
        return KdPacketNeedsResend;
    }

    MessageHeader->Length = MessageHeader->MaximumLength;
    MessageData->Length = Packet.ByteCount - MessageHeader->MaximumLength;
    if (!KdpFileIoReceive(MessageHeader->Buffer, MessageHeader->Length) ||
        !KdpFileIoReceive(MessageData->Buffer, MessageData->Length) ||
        !KdpFileIoReceive(&Byte, sizeof(Byte)))
    {
        return KdPacketNeedsResend;
    }

    Checksum = KdpFileIoChecksum(MessageHeader->Buffer, MessageHeader->Length) +
               KdpFileIoChecksum(MessageData->Buffer, MessageData->Length);
    if ((Byte != PACKET_TRAILING_BYTE) || (Checksum != Packet.Checksum))
        return KdPacketNeedsResend;

    *DataLength = MessageData->Length;
    return KdPacketReceived;
}

VOID
NTAPI
KdSendPacket(
    _In_ ULONG PacketType,
    _In_ PSTRING MessageHeader,
    _In_opt_ PSTRING MessageData,
    _Inout_ PKD_CONTEXT Context)
{
    PDBGKD_DEBUG_IO DebugIo;

    if (PacketType == PACKET_TYPE_KD_STATE_CHANGE32 ||
        PacketType == PACKET_TYPE_KD_STATE_CHANGE64)
    {
        PDBGKD_ANY_WAIT_STATE_CHANGE WaitStateChange = (PDBGKD_ANY_WAIT_STATE_CHANGE)MessageHeader->Buffer;

        if (WaitStateChange->NewState == DbgKdLoadSymbolsStateChange)
            return; // Ignore: invoked anytime a new module is loaded.

        /* We should not get there, unless an exception has been raised */
        if (WaitStateChange->NewState == DbgKdExceptionStateChange)
        {
            PEXCEPTION_RECORD64 ExceptionRecord = &WaitStateChange->u.Exception.ExceptionRecord;

            /*
             * Claim the debugger to be present, so that KdpSendWaitContinue()
             * can call back KdReceivePacket(PACKET_TYPE_KD_STATE_MANIPULATE),
             * which, in turn, informs KD that the exception cannot be handled.
             */
            KD_DEBUGGER_NOT_PRESENT = FALSE;
            MmWriteableSharedUserData->KdDebuggerEnabled |= 0x00000002;

            KdIoPrintf("%s: Got exception 0x%08lx @ 0x%p, Flags 0x%08x, %s - Info[0]: 0x%p\n",
                       __FUNCTION__,
                       ExceptionRecord->ExceptionCode,
                       (PVOID)(ULONG_PTR)ExceptionRecord->ExceptionAddress,
                       ExceptionRecord->ExceptionFlags,
                       WaitStateChange->u.Exception.FirstChance ? "FirstChance" : "LastChance",
                       ExceptionRecord->ExceptionInformation[0]);
            extern VOID NTAPI RtlpBreakWithStatusInstruction(VOID);

            if ((ExceptionRecord->ExceptionCode == STATUS_BREAKPOINT) &&
                ((PVOID)(ULONG_PTR)ExceptionRecord->ExceptionAddress == (PVOID)RtlpBreakWithStatusInstruction))
            {
                PCONTEXT ContextRecord = &KeGetCurrentPrcb()->ProcessorState.ContextFrame;
                ULONG Status =
#if defined(_M_IX86)
                    ContextRecord->Eax;
#elif defined(_M_AMD64)
                    (ULONG)ContextRecord->Rcx;
#elif defined(_M_ARM)
                    ContextRecord->R0;
#elif defined(_M_ARM64)
                    (ULONG)ContextRecord->X0;
#elif defined(_M_RISCV64)
                    (ULONG)ContextRecord->A0;
#elif defined(_M_PPC)
                    ContextRecord->Gpr3;
#else
#error Unknown architecture
#endif
                KdIoPrintf("STATUS_BREAKPOINT Status 0x%08lx\n", Status);
            }
            return;
        }

        KdIoPrintf("%s: PACKET_TYPE_KD_STATE_CHANGE32/64 NewState %d is UNIMPLEMENTED\n",
                   __FUNCTION__, WaitStateChange->NewState);
        return;
    }
    else
    if (PacketType == PACKET_TYPE_KD_STATE_MANIPULATE)
    {
        PDBGKD_MANIPULATE_STATE64 ManipulateState = (PDBGKD_MANIPULATE_STATE64)MessageHeader->Buffer;
        KdIoPrintf("%s: PACKET_TYPE_KD_STATE_MANIPULATE for ApiNumber %lu\n",
                   __FUNCTION__, ManipulateState->ApiNumber);
        return;
    }

    if (PacketType == PACKET_TYPE_KD_FILE_IO)
    {
        KdpSendFileIoPacket(MessageHeader, MessageData);
        return;
    }

    if (PacketType != PACKET_TYPE_KD_DEBUG_IO)
    {
        KdIoPrintf("%s: PacketType %d is UNIMPLEMENTED\n", __FUNCTION__, PacketType);
        return;
    }

    DebugIo = (PDBGKD_DEBUG_IO)MessageHeader->Buffer;

    /* Validate API call */
    if (MessageHeader->Length != sizeof(DBGKD_DEBUG_IO))
        return;
    if ((DebugIo->ApiNumber != DbgKdPrintStringApi) &&
        (DebugIo->ApiNumber != DbgKdGetStringApi))
    {
        return;
    }
    if (!MessageData)
        return;

    /* NOTE: MessageData->Length should be equal to
     * DebugIo.u.PrintString.LengthOfString, or to
     * DebugIo.u.GetString.LengthOfPromptString */

    if (!KdpDebugMode.Value)
        return;

    /* Print the string proper */
    KdIoPrintString(MessageData->Buffer, MessageData->Length);
}

KDSTATUS
NTAPI
KdReceivePacket(
    _In_ ULONG PacketType,
    _Out_ PSTRING MessageHeader,
    _Out_ PSTRING MessageData,
    _Out_ PULONG DataLength,
    _Inout_ PKD_CONTEXT Context)
{
#ifdef KDBG
    PDBGKD_DEBUG_IO DebugIo;
    STRING ResponseString;
    CHAR MessageBuffer[512];
    BOOLEAN DisplayAcquired;
#endif

    if (PacketType == PACKET_TYPE_KD_POLL_BREAKIN)
    {
        UCHAR Byte;

        /*
         * KDBG uses the native KD terminal multiplexer rather than kdcom.dll,
         * so it must poll the configured serial provider itself.  Do not take
         * KdpSerialSpinLock here: this runs from the clock interrupt and may
         * have interrupted a debug-print operation that owns that lock.
         */
        if (KdpDebugMode.Serial &&
            (SerialPortInfo.Address != NULL) &&
            KdPortGetByteEx(&SerialPortInfo, &Byte) &&
            (Byte == BREAKIN_PACKET_BYTE))
        {
            return KdPacketReceived;
        }

        if (KdpDebugMode.Net && KdpNetPollBreakIn())
            return KdPacketReceived;

        return KdPacketTimedOut;
    }

    if (PacketType == PACKET_TYPE_KD_STATE_MANIPULATE)
    {
        PDBGKD_MANIPULATE_STATE64 ManipulateState = (PDBGKD_MANIPULATE_STATE64)MessageHeader->Buffer;
        RtlZeroMemory(MessageHeader->Buffer, MessageHeader->MaximumLength);

        /* The exception (notified via DbgKdExceptionStateChange in
         * KdSendPacket()) cannot be handled: return a failure code */
        ManipulateState->ApiNumber = DbgKdContinueApi;
        ManipulateState->u.Continue.ContinueStatus = DBG_EXCEPTION_NOT_HANDLED;
        return KdPacketReceived;
    }

    if (PacketType == PACKET_TYPE_KD_FILE_IO)
        return KdpReceiveFileIoPacket(MessageHeader, MessageData, DataLength);

    if (PacketType != PACKET_TYPE_KD_DEBUG_IO)
    {
        KdIoPrintf("%s: PacketType %d is UNIMPLEMENTED\n", __FUNCTION__, PacketType);
        return KdPacketTimedOut;
    }

#ifdef KDBG
    DebugIo = (PDBGKD_DEBUG_IO)MessageHeader->Buffer;

    /* Validate API call */
    if (MessageHeader->MaximumLength != sizeof(DBGKD_DEBUG_IO))
        return KdPacketNeedsResend;
    if (DebugIo->ApiNumber != DbgKdGetStringApi)
        return KdPacketNeedsResend;

    /* NOTE: We cannot use directly MessageData->Buffer here as it points
     * to the temporary KdpMessageBuffer scratch buffer that is being
     * shared with all the possible I/O KD operations that may happen. */
    ResponseString.Buffer = MessageBuffer;
    ResponseString.Length = 0;
    ResponseString.MaximumLength = min(sizeof(MessageBuffer),
                                       MessageData->MaximumLength);
    ResponseString.MaximumLength = min(ResponseString.MaximumLength,
                                       DebugIo->u.GetString.LengthOfStringRead);

    /* An assertion may prompt before KDBG has initialized its pager. */
    KdpInitTerminal();

    /* KDBG reads its own command line here too, with the display already settled */
    DisplayAcquired = KdbActive ? FALSE : KdpScreenEnterStop(FALSE);

    /* The prompt string has been printed by KdSendPacket; go to
     * new line and print the kdb prompt -- for SYSREG2 support. */
    KdIoPrintString("\n", 1);
    KdIoPuts(KdbPromptStr.Buffer); // Alternatively, use "Input> "

    if (!KdTermSerial)
        KbdDisableMouse();

    /*
     * Read a NULL-terminated line of user input and retrieve its length.
     * Official documentation states that DbgPrompt() includes a terminating
     * newline character but does not NULL-terminate. However, experiments
     * show that this behaviour is left at the discretion of WinDbg itself.
     * WinDbg NULL-terminates the string unless its buffer is too short,
     * in which case the string is simply truncated without NULL-termination.
     */
    ResponseString.Length =
        (USHORT)KdIoReadLine(ResponseString.Buffer,
                             ResponseString.MaximumLength);

    if (!KdTermSerial)
        KbdEnableMouse();

    KdpScreenRelease(DisplayAcquired);

    /* Adjust and return the string length */
    *DataLength = min(ResponseString.Length + sizeof(ANSI_NULL),
                      DebugIo->u.GetString.LengthOfStringRead);
    MessageData->Length = DebugIo->u.GetString.LengthOfStringRead = *DataLength;

    /* Only now we can copy back the data into MessageData->Buffer */
    RtlCopyMemory(MessageData->Buffer, ResponseString.Buffer, *DataLength);
#endif

    return KdPacketReceived;
}

/* EOF */
