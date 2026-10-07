/*
 * PROJECT:     LiberNT Network Kernel Debugger Transport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Transport module entry points and the terminal channel of the built-in debugger
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "kdnet.h"

#define KDNET_ENTROPY_SAMPLES   1024
#define KDNET_ADDRESS_WAIT      (8 * KDNET_SECOND)
#define KDNET_RECEIVE_BURST     32
#define KDNET_CLOCK_STALL_LIMIT 256
#define KDNET_CLOCK_STEP        50

KDNET_OPTIONS KdNetOptions;
volatile BOOLEAN KdNetReady;

static LARGE_INTEGER KdpFrequency;
static LONGLONG KdpLastCounter;
static ULONG64 KdpClockOffset;
static ULONG KdpClockStalled;
static BOOLEAN KdpInService;

ULONG64
KdNetTime(VOID)
{
    LARGE_INTEGER Counter, Frequency;

    Counter = KeQueryPerformanceCounter(&Frequency);
    if (!KdpFrequency.QuadPart)
        KdpFrequency = Frequency;

    if (Counter.QuadPart != KdpLastCounter)
    {
        KdpLastCounter = Counter.QuadPart;
        KdpClockStalled = 0;
    }
    else if (KdpClockStalled < KDNET_CLOCK_STALL_LIMIT)
    {
        KdpClockStalled++;
    }
    else
    {
        KeStallExecutionProcessor(KDNET_CLOCK_STEP);
        KdpClockOffset += KDNET_CLOCK_STEP;
    }

    return (ULONG64)(Counter.QuadPart / KdpFrequency.QuadPart) * KDNET_SECOND +
           (ULONG64)(Counter.QuadPart % KdpFrequency.QuadPart) * KDNET_SECOND / KdpFrequency.QuadPart +
           KdpClockOffset;
}

VOID
KdNetService(VOID)
{
    PUCHAR Frame;
    ULONG Length, Handle, Count;
    ULONG64 Now;

    if (KdpInService)
        return;

    KdpInService = TRUE;
    for (Count = 0; Count < KDNET_RECEIVE_BURST; Count++)
    {
        if (!KdNicReceive(&Frame, &Length, &Handle))
            break;

        KdIpInput(Frame, Length);
        KdNicRelease(Handle);
    }

    Now = KdNetTime();
    KdIpTimer(Now);
    KdSessionTimer(Now);
    KdpInService = FALSE;
}

static
PCSTR
KdpFindOption(
    _In_ PCSTR Options,
    _In_ PCSTR Name)
{
    SIZE_T Length = strlen(Name);
    PCSTR Cursor = Options;

    while ((Cursor = strstr(Cursor, Name)) != NULL)
    {
        if ((Cursor == Options || Cursor[-1] == ' ' || Cursor[-1] == '/') && Cursor[Length] == '=')
            return Cursor + Length + 1;

        Cursor += Length;
    }

    return NULL;
}

static
BOOLEAN
KdpParseNumbers(
    _In_ PCSTR Text,
    _In_ CHAR Separator,
    _Out_writes_(Count) PULONG Values,
    _In_ ULONG Count)
{
    ULONG i, Value;

    for (i = 0; i < Count; i++)
    {
        if (*Text < '0' || *Text > '9')
            return FALSE;

        Value = 0;
        while (*Text >= '0' && *Text <= '9')
        {
            Value = Value * 10 + (*Text++ - '0');
            if (Value > 0xFFFF)
                return FALSE;
        }

        Values[i] = Value;
        if (i + 1 < Count && *Text++ != Separator)
            return FALSE;
    }

    return *Text == ANSI_NULL || *Text == ' ';
}

static
VOID
KdpParseOptions(
    _Inout_opt_z_ PCHAR Options)
{
    ULONG Values[4], i;
    PCSTR Text;
    PCHAR Secret;

    RtlZeroMemory(&KdNetOptions, sizeof(KdNetOptions));
    KdNetOptions.HostPort = KDNET_DEFAULT_PORT;
    if (!Options)
        return;

    Text = KdpFindOption(Options, "HOST_IP");
    if (Text && KdpParseNumbers(Text, '.', Values, 4) &&
        Values[0] < 256 && Values[1] < 256 && Values[2] < 256 && Values[3] < 256)
    {
        KdNetOptions.HostIp = KDNET_IP(Values[0], Values[1], Values[2], Values[3]);
    }

    Text = KdpFindOption(Options, "HOST_NAME");
    if (Text && !KdNetOptions.HostIp)
    {
        for (i = 0; i < sizeof(KdNetOptions.HostName) - 1 && Text[i] && Text[i] != ' '; i++)
            KdNetOptions.HostName[i] = Text[i];

        KdNetOptions.HostName[i] = ANSI_NULL;
    }

    Text = KdpFindOption(Options, "HOST_PORT");
    if (Text && KdpParseNumbers(Text, ANSI_NULL, Values, 1) && Values[0])
        KdNetOptions.HostPort = (USHORT)Values[0];

    Text = KdpFindOption(Options, "BUSPARAMS");
    if (Text && KdpParseNumbers(Text, '.', Values, 3) &&
        Values[0] < 256 && Values[1] < PCI_MAX_DEVICES && Values[2] < PCI_MAX_FUNCTION)
    {
        KdNetOptions.HaveLocation = TRUE;
        KdNetOptions.Bus = Values[0];
        KdNetOptions.Device = Values[1];
        KdNetOptions.Function = Values[2];
    }

    Text = KdpFindOption(Options, "ENCRYPTION_KEY");
    if (Text)
    {
        for (i = 0; i < sizeof(KdNetOptions.Key) - 1 && Text[i] && Text[i] != ' '; i++)
            KdNetOptions.Key[i] = Text[i];

        KdNetOptions.Key[i] = ANSI_NULL;
        KdNetOptions.HaveKey = i != 0;
        for (Secret = Options + (Text - Options); *Secret && *Secret != ' '; Secret++)
            *Secret = '*';
    }
}

static
VOID
KdpGatherEntropy(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    LARGE_INTEGER Samples[16], Time;
    ULONG i, Value;

    for (i = 0; i < KDNET_ENTROPY_SAMPLES; i++)
    {
        Samples[i % RTL_NUMBER_OF(Samples)] = KeQueryPerformanceCounter(NULL);
        KeStallExecutionProcessor(1);
        if (i % RTL_NUMBER_OF(Samples) == RTL_NUMBER_OF(Samples) - 1)
            KdCryptoAddEntropy(Samples, sizeof(Samples));
    }

    for (i = 0; i < 64 && KdNetArchRandom(&Value); i++)
        KdCryptoAddEntropy(&Value, sizeof(Value));

    KeQuerySystemTime(&Time);
    KdCryptoAddEntropy(&Time, sizeof(Time));
    Time.QuadPart = KeQueryInterruptTime();
    KdCryptoAddEntropy(&Time, sizeof(Time));
    KdCryptoAddEntropy(&LoaderBlock, sizeof(LoaderBlock));
    if (LoaderBlock->Extension &&
        LoaderBlock->Extension->Size >= RTL_SIZEOF_THROUGH_FIELD(LOADER_PARAMETER_EXTENSION, BootEntropyResult))
    {
        KdCryptoAddEntropy(&LoaderBlock->Extension->BootEntropyResult,
                           sizeof(LoaderBlock->Extension->BootEntropyResult));
    }
}

static
VOID
NTAPI
KdpTerminalConfigure(
    _Inout_opt_z_ PCHAR LoadOptions)
{
    KdpParseOptions(LoadOptions);
}

static
NTSTATUS
NTAPI
KdpTerminalInitialize(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    NTSTATUS Status;
    ULONG64 Start;

    if (KdNetReady)
        return STATUS_SUCCESS;

    Status = KdCryptoInitialize();
    if (!NT_SUCCESS(Status))
        return Status;

    KdpGatherEntropy(LoaderBlock);
    Status = KdNicInitialize(LoaderBlock);
    if (!NT_SUCCESS(Status))
        return Status;

    KdCryptoAddEntropy(KdNicAddress, sizeof(KdNicAddress));
    KdIpInitialize();

    Start = KdNetTime();
    while ((!KdIpAddress || (KdNetOptions.HostName[0] && !KdNetOptions.HostIp)) &&
           KdNicLinkState && KdNetTime() - Start < KDNET_ADDRESS_WAIT)
    {
        KdNetService();
        KeStallExecutionProcessor(500);
    }

    KdSessionInitialize();
    KeMemoryBarrier();
    KdNetReady = TRUE;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
KdpTerminalWrite(
    _In_reads_bytes_(Length) const CHAR *Buffer,
    _In_ ULONG Length)
{
    KdSessionWrite(Buffer, Length);
    if (KdNetReady)
        KdNetService();
}

static
BOOLEAN
NTAPI
KdpTerminalRead(
    _Out_ PUCHAR Byte)
{
    if (!KdNetReady)
        return FALSE;

    KdNetStopped = TRUE;
    if (KdSessionRead(Byte))
        return TRUE;

    KdNetService();
    return KdSessionRead(Byte);
}

static
BOOLEAN
NTAPI
KdpTerminalPoll(VOID)
{
    if (!KdNetReady)
        return FALSE;

    KdNetStopped = FALSE;
    KdNetService();
    return KdSessionTakeBreak();
}

static
BOOLEAN
NTAPI
KdpTerminalQueryAttached(
    _Out_opt_ PULONG Columns,
    _Out_opt_ PULONG Rows)
{
    return KdNetReady && KdSessionAttached(Columns, Rows);
}

static
ULONG
NTAPI
KdpTerminalQueryStatus(
    _Out_writes_z_(Size) PCHAR Buffer,
    _In_ ULONG Size)
{
    ULONG Host = KdNetOptions.HostIp;
    int Length;

    if (!Size)
        return 0;

    if (!KdNetReady)
    {
        Length = _snprintf(Buffer, Size, "Network debugger: no usable network card");
    }
    else if (!KdIpAddress)
    {
        Length = _snprintf(Buffer, Size,
                           "Network debugger: %02x-%02x-%02x-%02x-%02x-%02x has no address yet",
                           KdNicAddress[0], KdNicAddress[1], KdNicAddress[2],
                           KdNicAddress[3], KdNicAddress[4], KdNicAddress[5]);
    }
    else if (!Host && KdNetOptions.HostName[0])
    {
        Length = _snprintf(Buffer, Size,
                           "Network debugger: %lu.%lu.%lu.%lu cannot resolve %s yet",
                           KdIpAddress >> 24, (KdIpAddress >> 16) & 255, (KdIpAddress >> 8) & 255,
                           KdIpAddress & 255, KdNetOptions.HostName);
    }
    else if (Host)
    {
        Length = _snprintf(Buffer, Size,
                           "Network debugger: %lu.%lu.%lu.%lu to %lu.%lu.%lu.%lu port %u%s",
                           KdIpAddress >> 24, (KdIpAddress >> 16) & 255, (KdIpAddress >> 8) & 255,
                           KdIpAddress & 255, Host >> 24, (Host >> 16) & 255, (Host >> 8) & 255,
                           Host & 255, KdNetOptions.HostPort,
                           KdSessionAttached(NULL, NULL) ? ", attached" : "");
    }
    else
    {
        Length = _snprintf(Buffer, Size,
                           "Network debugger: %lu.%lu.%lu.%lu port %u%s",
                           KdIpAddress >> 24, (KdIpAddress >> 16) & 255, (KdIpAddress >> 8) & 255,
                           KdIpAddress & 255, KdNetOptions.HostPort,
                           KdSessionAttached(NULL, NULL) ? ", attached" : "");
    }

    if (Length < 0 || (ULONG)Length >= Size)
        Length = Size - 1;

    Buffer[Length] = ANSI_NULL;
    return Length;
}

static
ULONG
NTAPI
KdpTerminalQueryKey(
    _Out_writes_z_(Size) PCHAR Buffer,
    _In_ ULONG Size)
{
    int Length;

    if (!Size)
        return 0;

    *Buffer = ANSI_NULL;
    if (!KdNetReady || KdNetOptions.HaveKey)
        return 0;

    Length = _snprintf(Buffer, Size, "%s", KdSessionKeyText);
    if (Length < 0 || (ULONG)Length >= Size)
        Length = Size - 1;

    Buffer[Length] = ANSI_NULL;
    return Length;
}

static
VOID
NTAPI
KdpTerminalSetHost(
    _In_ const KD_TERMINAL_HOST *Host)
{
    KdNetHost = *Host;
}

NTSTATUS
NTAPI
KdQueryTerminalInterface(
    _Out_ PKD_TERMINAL_INTERFACE Interface)
{
    Interface->Version = KD_TERMINAL_INTERFACE_VERSION;
    Interface->Configure = KdpTerminalConfigure;
    Interface->Initialize = KdpTerminalInitialize;
    Interface->Write = KdpTerminalWrite;
    Interface->Read = KdpTerminalRead;
    Interface->Poll = KdpTerminalPoll;
    Interface->QueryAttached = KdpTerminalQueryAttached;
    Interface->QueryStatus = KdpTerminalQueryStatus;
    Interface->QueryKey = KdpTerminalQueryKey;
    Interface->SetHost = KdpTerminalSetHost;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KdDebuggerInitialize0(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNREFERENCED_PARAMETER(LoaderBlock);

    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
KdDebuggerInitialize1(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNREFERENCED_PARAMETER(LoaderBlock);

    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
KdD0Transition(VOID)
{
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KdD3Transition(VOID)
{
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KdSave(
    _In_ BOOLEAN SleepTransition)
{
    UNREFERENCED_PARAMETER(SleepTransition);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KdRestore(
    _In_ BOOLEAN SleepTransition)
{
    UNREFERENCED_PARAMETER(SleepTransition);

    return STATUS_SUCCESS;
}

VOID
NTAPI
KdSendPacket(
    _In_ ULONG PacketType,
    _In_ PSTRING MessageHeader,
    _In_opt_ PSTRING MessageData,
    _Inout_ PKD_CONTEXT Context)
{
    UNREFERENCED_PARAMETER(PacketType);
    UNREFERENCED_PARAMETER(MessageHeader);
    UNREFERENCED_PARAMETER(MessageData);
    UNREFERENCED_PARAMETER(Context);
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
    UNREFERENCED_PARAMETER(PacketType);
    UNREFERENCED_PARAMETER(MessageHeader);
    UNREFERENCED_PARAMETER(MessageData);
    UNREFERENCED_PARAMETER(DataLength);
    UNREFERENCED_PARAMETER(Context);

    return KdPacketTimedOut;
}
