/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The headless terminal, a serial line the system can be read on
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

PHEADLESS_GLOBALS HeadlessGlobals = NULL;

/* PRIVATE DATA ***************************************************************/

#define EXP_HEADLESS_TAG 'ldaH'

/* How many messages the log keeps before it starts forgetting the oldest */
#define EXP_HEADLESS_LOG_ENTRIES 128

/* The room set aside for a line typed in on the terminal */
#define EXP_HEADLESS_INPUT_SIZE 256

/* The room set aside for a line on its way out */
#define EXP_HEADLESS_TMP_SIZE 256

/* The registers of the serial port, counted from its base */
#define EXP_SERIAL_DATA         0
#define EXP_SERIAL_DIVISOR_LOW  0
#define EXP_SERIAL_DIVISOR_HIGH 1
#define EXP_SERIAL_INT_ENABLE   1
#define EXP_SERIAL_FIFO_CONTROL 2
#define EXP_SERIAL_LINE_CONTROL 3
#define EXP_SERIAL_MODEM_CONTROL 4
#define EXP_SERIAL_LINE_STATUS  5

#define EXP_SERIAL_LSR_DATA_READY    0x01
#define EXP_SERIAL_LSR_HOLDING_EMPTY 0x20

#define EXP_SERIAL_LCR_8N1       0x03
#define EXP_SERIAL_LCR_DLAB      0x80
#define EXP_SERIAL_FCR_ENABLE    0x07
#define EXP_SERIAL_MCR_DTR_RTS   0x03

/* The rate the line runs at when nothing else is asked for */
#define EXP_SERIAL_DEFAULT_BAUD 9600

/*
 * The terminal is driven with the escape sequences every terminal that calls
 * itself one understands, so nothing has to be known about which one is on the
 * far end of the line.
 */
#define EXP_VT_ESCAPE "\033["

/* The colours a caller may ask for, in the order the sequences number them */
static const UCHAR ExpVtColour[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };

#define EXP_SERIAL_CLOCK 115200

static const ULONG ExpSerialPortAddress[] = { 0, 0x3F8, 0x2F8, 0x3E8, 0x2E8 };

/* PRIVATE FUNCTIONS **********************************************************/

static
VOID
ExpSerialWriteRegister(
    _In_ ULONG Offset,
    _In_ UCHAR Value)
{
    if (HeadlessGlobals->IsMMIODevice)
    {
        WRITE_REGISTER_UCHAR(HeadlessGlobals->TerminalPortAddress + Offset,
                             Value);
        return;
    }

    WRITE_PORT_UCHAR(HeadlessGlobals->TerminalPortAddress + Offset, Value);
}

static
UCHAR
ExpSerialReadRegister(
    _In_ ULONG Offset)
{
    if (HeadlessGlobals->IsMMIODevice)
    {
        return READ_REGISTER_UCHAR(HeadlessGlobals->TerminalPortAddress +
                                   Offset);
    }

    return READ_PORT_UCHAR(HeadlessGlobals->TerminalPortAddress + Offset);
}

static
VOID
ExpSerialSetBaudRate(
    _In_ ULONG BaudRate)
{
    USHORT Divisor;

    if (BaudRate == 0)
    {
        BaudRate = EXP_SERIAL_DEFAULT_BAUD;
    }

    Divisor = (USHORT)(EXP_SERIAL_CLOCK / BaudRate);
    if (Divisor == 0)
    {
        Divisor = 1;
    }

    ExpSerialWriteRegister(EXP_SERIAL_LINE_CONTROL, EXP_SERIAL_LCR_DLAB);
    ExpSerialWriteRegister(EXP_SERIAL_DIVISOR_LOW, (UCHAR)(Divisor & 0xFF));
    ExpSerialWriteRegister(EXP_SERIAL_DIVISOR_HIGH, (UCHAR)(Divisor >> 8));
    ExpSerialWriteRegister(EXP_SERIAL_LINE_CONTROL, EXP_SERIAL_LCR_8N1);
}

static
VOID
ExpSerialPutByte(
    _In_ UCHAR Value)
{
    ULONG Spin = 0;

    while (!(ExpSerialReadRegister(EXP_SERIAL_LINE_STATUS) &
             EXP_SERIAL_LSR_HOLDING_EMPTY))
    {
        if (++Spin > 1000000)
        {
            return;
        }

        KeStallExecutionProcessor(1);
    }

    ExpSerialWriteRegister(EXP_SERIAL_DATA, Value);
}

static
BOOLEAN
ExpSerialGetByte(
    _Out_ PUCHAR Value)
{
    if (!(ExpSerialReadRegister(EXP_SERIAL_LINE_STATUS) &
          EXP_SERIAL_LSR_DATA_READY))
    {
        return FALSE;
    }

    *Value = ExpSerialReadRegister(EXP_SERIAL_DATA);
    return TRUE;
}

VOID
NTAPI
HdlspPutData(
    _In_reads_bytes_(Count) PUCHAR Data,
    _In_ SIZE_T Count)
{
    SIZE_T Index;

    for (Index = 0; Index < Count; Index++)
    {
        ExpSerialPutByte(Data[Index]);
    }
}

/*
 * Writes a string as the terminal expects to read it: a line that ends with a
 * newline alone gets the carriage return the terminal needs put in front of it.
 */
VOID
NTAPI
HdlspPutString(
    _In_ PUCHAR String)
{
    PUCHAR Current;

    for (Current = String; *Current != 0; Current++)
    {
        if ((*Current == '\n') && !HeadlessGlobals->IsLastCharCR)
        {
            ExpSerialPutByte('\r');
        }

        ExpSerialPutByte(*Current);

        HeadlessGlobals->IsLastCharCR = (*Current == '\r');
    }
}

/*
 * Writes a number as the digits of its decimal form, which is how the escape
 * sequences carry one.
 */
static
PUCHAR
ExpVtNumber(
    _Out_writes_(12) PUCHAR Buffer,
    _In_ ULONG Value)
{
    UCHAR Digits[12];
    ULONG Count = 0;
    ULONG Index;

    if (Value == 0)
    {
        Buffer[0] = '0';
        Buffer[1] = ANSI_NULL;
        return Buffer;
    }

    while ((Value != 0) && (Count < sizeof(Digits)))
    {
        Digits[Count++] = (UCHAR)('0' + (Value % 10));
        Value /= 10;
    }

    for (Index = 0; Index < Count; Index++)
    {
        Buffer[Index] = Digits[Count - Index - 1];
    }

    Buffer[Count] = ANSI_NULL;

    return Buffer;
}

static
VOID
ExpVtSequence(
    _In_ PCSTR Prefix,
    _In_ PCSTR Suffix)
{
    HdlspPutData((PUCHAR)EXP_VT_ESCAPE, sizeof(EXP_VT_ESCAPE) - 1);

    if (Prefix != NULL)
    {
        HdlspPutData((PUCHAR)Prefix, (SIZE_T)strlen(Prefix));
    }

    HdlspPutData((PUCHAR)Suffix, (SIZE_T)strlen(Suffix));
}

VOID
NTAPI
HdlspSendStringAtBaud(
    _In_ PUCHAR String)
{
    ULONG SavedBaudRate = HeadlessGlobals->TerminalBaudRate;

    ExpSerialSetBaudRate(EXP_SERIAL_DEFAULT_BAUD);

    HdlspPutString(String);

    ExpSerialSetBaudRate(SavedBaudRate);
}

VOID
NTAPI
HdlspEnableTerminal(
    _In_ BOOLEAN Enable)
{
    if (Enable == (BOOLEAN)HeadlessGlobals->TerminalEnabled)
    {
        return;
    }

    if (!Enable)
    {
        HeadlessGlobals->TerminalEnabled = FALSE;
        return;
    }

    ExpSerialWriteRegister(EXP_SERIAL_INT_ENABLE, 0);
    ExpSerialSetBaudRate(HeadlessGlobals->TerminalBaudRate);
    ExpSerialWriteRegister(EXP_SERIAL_FIFO_CONTROL, EXP_SERIAL_FCR_ENABLE);
    ExpSerialWriteRegister(EXP_SERIAL_MODEM_CONTROL, EXP_SERIAL_MCR_DTR_RTS);

    HeadlessGlobals->TerminalEnabled = TRUE;
}

/*
 * Every command the terminal serves goes through here. The lock is held for as
 * long as the command runs, because one line cannot serve two callers at once.
 */
NTSTATUS
NTAPI
HdlspDispatch(
    _In_ HEADLESS_CMD Command,
    _In_opt_ PVOID InputBuffer,
    _In_ SIZE_T InputBufferSize,
    _Out_opt_ PVOID OutputBuffer,
    _Inout_opt_ PSIZE_T OutputBufferSize)
{
    PHEADLESS_RSP_QUERY_INFO Info;
    PHEADLESS_RSP_GET_BYTE Byte;
    UCHAR Value;

    switch (Command)
    {
        case HeadlessCmdEnableTerminal:
        {
            PHEADLESS_CMD_ENABLE_TERMINAL Enable = InputBuffer;

            if ((Enable == NULL) ||
                (InputBufferSize < sizeof(HEADLESS_CMD_ENABLE_TERMINAL)))
            {
                return STATUS_INVALID_PARAMETER;
            }

            HdlspEnableTerminal(Enable->Enable);
            return STATUS_SUCCESS;
        }

        case HeadlessCmdPutString:
        {
            if (!HeadlessGlobals->TerminalEnabled)
            {
                return STATUS_UNSUCCESSFUL;
            }

            if (InputBuffer == NULL)
            {
                return STATUS_INVALID_PARAMETER;
            }

            HdlspPutString(((PHEADLESS_CMD_PUT_STRING)InputBuffer)->String);
            return STATUS_SUCCESS;
        }

        case HeadlessCmdPutData:
        {
            if (!HeadlessGlobals->TerminalEnabled)
            {
                return STATUS_UNSUCCESSFUL;
            }

            if (InputBuffer == NULL)
            {
                return STATUS_INVALID_PARAMETER;
            }

            HdlspPutData(InputBuffer, InputBufferSize);
            return STATUS_SUCCESS;
        }

        case HeadlessCmdTerminalPoll:
        {
            if (!HeadlessGlobals->TerminalEnabled)
            {
                return STATUS_UNSUCCESSFUL;
            }

            return STATUS_SUCCESS;
        }

        case HeadlessCmdGetByte:
        {
            if ((OutputBuffer == NULL) || (OutputBufferSize == NULL) ||
                (*OutputBufferSize < sizeof(HEADLESS_RSP_GET_BYTE)))
            {
                return STATUS_INVALID_PARAMETER;
            }

            Byte = OutputBuffer;
            Byte->Value = ExpSerialGetByte(&Value) ? Value : 0;
            *OutputBufferSize = sizeof(HEADLESS_RSP_GET_BYTE);

            return STATUS_SUCCESS;
        }

        case HeadlessCmdQueryInformation:
        {
            if ((OutputBuffer == NULL) || (OutputBufferSize == NULL) ||
                (*OutputBufferSize < sizeof(HEADLESS_RSP_QUERY_INFO)))
            {
                return STATUS_INVALID_PARAMETER;
            }

            Info = OutputBuffer;
            Info->PortType = HeadlessSerialPort;
            Info->Serial.TerminalAttached =
                (BOOLEAN)HeadlessGlobals->TerminalEnabled;
            Info->Serial.UsedBiosSettings =
                (BOOLEAN)HeadlessGlobals->UsedBiosSettings;
            Info->Serial.TerminalPort =
                (HEADLESS_TERM_SERIAL_PORT)HeadlessGlobals->TerminalPortNumber;
            Info->Serial.TerminalPortBaseAddress =
                HeadlessGlobals->TerminalPortAddress;
            Info->Serial.TerminalBaudRate = HeadlessGlobals->TerminalBaudRate;
            Info->Serial.TerminalType = HeadlessGlobals->TerminalType;

            *OutputBufferSize = sizeof(HEADLESS_RSP_QUERY_INFO);

            return STATUS_SUCCESS;
        }

        case HeadlessCmdQueryGUID:
        {
            if ((OutputBuffer == NULL) || (OutputBufferSize == NULL) ||
                (*OutputBufferSize < sizeof(GUID)))
            {
                return STATUS_INVALID_PARAMETER;
            }

            *(GUID *)OutputBuffer = HeadlessGlobals->SystemGUID;
            *OutputBufferSize = sizeof(GUID);

            return STATUS_SUCCESS;
        }

        case HeadlessCmdClearDisplay:
        {
            if (!HeadlessGlobals->TerminalEnabled)
            {
                return STATUS_UNSUCCESSFUL;
            }

            /* The display is cleared and the cursor put back at its corner. */
            ExpVtSequence(NULL, "2J");
            ExpVtSequence(NULL, "H");
            return STATUS_SUCCESS;
        }

        case HeadlessCmdClearToEndOfDisplay:
        {
            if (!HeadlessGlobals->TerminalEnabled)
            {
                return STATUS_UNSUCCESSFUL;
            }

            ExpVtSequence(NULL, "0J");
            return STATUS_SUCCESS;
        }

        case HeadlessCmdClearToEndOfLine:
        {
            if (!HeadlessGlobals->TerminalEnabled)
            {
                return STATUS_UNSUCCESSFUL;
            }

            ExpVtSequence(NULL, "0K");
            return STATUS_SUCCESS;
        }

        case HeadlessCmdDisplayAttributesOff:
        {
            if (!HeadlessGlobals->TerminalEnabled)
            {
                return STATUS_UNSUCCESSFUL;
            }

            ExpVtSequence(NULL, "0m");
            return STATUS_SUCCESS;
        }

        case HeadlessCmdDisplayInverseVideo:
        {
            if (!HeadlessGlobals->TerminalEnabled)
            {
                return STATUS_UNSUCCESSFUL;
            }

            ExpVtSequence(NULL, "7m");
            return STATUS_SUCCESS;
        }

        case HeadlessCmdSetColor:
        {
            PHEADLESS_CMD_SET_COLOR Colour = InputBuffer;
            UCHAR Digits[12];

            if (!HeadlessGlobals->TerminalEnabled)
            {
                return STATUS_UNSUCCESSFUL;
            }

            if ((Colour == NULL) ||
                (InputBufferSize < sizeof(HEADLESS_CMD_SET_COLOR)))
            {
                return STATUS_INVALID_PARAMETER;
            }

            ExpVtSequence((PCSTR)ExpVtNumber(Digits,
                                             30 + ExpVtColour[
                                                 Colour->TextColor & 7]),
                          "m");
            ExpVtSequence((PCSTR)ExpVtNumber(Digits,
                                             40 + ExpVtColour[
                                                 Colour->BkgdColor & 7]),
                          "m");
            return STATUS_SUCCESS;
        }

        case HeadlessCmdPositionCursor:
        {
            PHEADLESS_CMD_POSITION_CURSOR Position = InputBuffer;
            UCHAR Digits[12];

            if (!HeadlessGlobals->TerminalEnabled)
            {
                return STATUS_UNSUCCESSFUL;
            }

            if ((Position == NULL) ||
                (InputBufferSize < sizeof(HEADLESS_CMD_POSITION_CURSOR)))
            {
                return STATUS_INVALID_PARAMETER;
            }

            /* The sequence counts rows and columns from one, not from zero. */
            HdlspPutData((PUCHAR)EXP_VT_ESCAPE, sizeof(EXP_VT_ESCAPE) - 1);
            ExpVtNumber(Digits, Position->CursorRow + 1);
            HdlspPutData(Digits, (SIZE_T)strlen((PCSTR)Digits));
            HdlspPutData((PUCHAR)";", 1);
            ExpVtNumber(Digits, Position->CursorCol + 1);
            HdlspPutData(Digits, (SIZE_T)strlen((PCSTR)Digits));
            HdlspPutData((PUCHAR)"H", 1);
            return STATUS_SUCCESS;
        }

        case HeadlessCmdGetLine:
        {
            PUCHAR Line = OutputBuffer;
            SIZE_T Room;
            UCHAR Value;

            if ((Line == NULL) || (OutputBufferSize == NULL) ||
                (*OutputBufferSize == 0))
            {
                return STATUS_INVALID_PARAMETER;
            }

            Room = *OutputBufferSize - 1;

            /*
             * A line is gathered a character at a time as they arrive; the
             * caller is told nothing until a whole line is in hand.
             */
            while (ExpSerialGetByte(&Value))
            {
                if ((Value == '\r') || (Value == '\n'))
                {
                    HeadlessGlobals->InputBuffer[
                        HeadlessGlobals->InputBufferIndex] = ANSI_NULL;

                    RtlCopyMemory(Line,
                                  HeadlessGlobals->InputBuffer,
                                  HeadlessGlobals->InputBufferIndex + 1);

                    *OutputBufferSize =
                        HeadlessGlobals->InputBufferIndex + 1;
                    HeadlessGlobals->InputBufferIndex = 0;

                    return STATUS_SUCCESS;
                }

                if (HeadlessGlobals->InputBufferIndex < Room)
                {
                    HeadlessGlobals->InputBuffer[
                        HeadlessGlobals->InputBufferIndex++] = Value;
                }
            }

            return STATUS_PENDING;
        }

        case HeadlessCmdAddLogEntry:
        {
            PHEADLESS_LOG_ENTRY Entry;
            USHORT Slot;

            if ((InputBuffer == NULL) ||
                (HeadlessGlobals->LogEntries == NULL))
            {
                return STATUS_INVALID_PARAMETER;
            }

            Slot = HeadlessGlobals->LogEntryLast;
            Entry = &HeadlessGlobals->LogEntries[Slot];

            /* The message is copied: the caller's own room is its own. */
            if (Entry->String != NULL)
            {
                ExFreePoolWithTag(Entry->String, EXP_HEADLESS_TAG);
                Entry->String = NULL;
            }

            Entry->String = ExAllocatePoolZero(NonPagedPool,
                                               InputBufferSize +
                                               sizeof(WCHAR),
                                               EXP_HEADLESS_TAG);
            if (Entry->String == NULL)
            {
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            RtlCopyMemory(Entry->String, InputBuffer, InputBufferSize);

            KeQuerySystemTime(&Entry->TimeOfEntry.BootTime);

            HeadlessGlobals->LogEntryLast =
                (USHORT)((Slot + 1) % EXP_HEADLESS_LOG_ENTRIES);

            /* A full ring forgets its oldest message to make room. */
            if (HeadlessGlobals->LogEntryLast ==
                HeadlessGlobals->LogEntryStart)
            {
                HeadlessGlobals->LogEntryStart =
                    (USHORT)((HeadlessGlobals->LogEntryStart + 1) %
                             EXP_HEADLESS_LOG_ENTRIES);
            }

            HeadlessGlobals->NewLogEntryAdded = TRUE;

            return STATUS_SUCCESS;
        }

        case HeadlessCmdDisplayLog:
        {
            USHORT Slot;

            if (!HeadlessGlobals->TerminalEnabled ||
                (HeadlessGlobals->LogEntries == NULL))
            {
                return STATUS_UNSUCCESSFUL;
            }

            for (Slot = HeadlessGlobals->LogEntryStart;
                 Slot != HeadlessGlobals->LogEntryLast;
                 Slot = (USHORT)((Slot + 1) % EXP_HEADLESS_LOG_ENTRIES))
            {
                PWCHAR Text = HeadlessGlobals->LogEntries[Slot].String;
                ULONG Index;

                if (Text == NULL)
                {
                    continue;
                }

                for (Index = 0; Text[Index] != UNICODE_NULL; Index++)
                {
                    ExpSerialPutByte((UCHAR)Text[Index]);
                }

                HdlspPutString((PUCHAR)"\r\n");
            }

            HeadlessGlobals->NewLogEntryAdded = FALSE;

            return STATUS_SUCCESS;
        }

        case HeadlessCmdSetBlueScreenData:
        {
            PHEADLESS_CMD_SET_BLUE_SCREEN_DATA Data = InputBuffer;
            PHEADLESS_CMD_SET_BLUE_SCREEN_DATA Copy;

            if ((Data == NULL) ||
                (InputBufferSize <
                 sizeof(HEADLESS_CMD_SET_BLUE_SCREEN_DATA)))
            {
                return STATUS_INVALID_PARAMETER;
            }

            Copy = ExAllocatePoolZero(NonPagedPool,
                                      InputBufferSize,
                                      EXP_HEADLESS_TAG);
            if (Copy == NULL)
            {
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            RtlCopyMemory(Copy, Data, InputBufferSize);

            if (HeadlessGlobals->BlueScreenData != NULL)
            {
                ExFreePoolWithTag(HeadlessGlobals->BlueScreenData,
                                  EXP_HEADLESS_TAG);
            }

            HeadlessGlobals->BlueScreenData = Copy;

            return STATUS_SUCCESS;
        }

        case HeadlessCmdSendBlueScreenData:
        {
            if (!HeadlessGlobals->TerminalEnabled ||
                (HeadlessGlobals->BlueScreenData == NULL))
            {
                return STATUS_UNSUCCESSFUL;
            }

            HdlspPutString((PUCHAR)
                           HeadlessGlobals->BlueScreenData->Data);
            HdlspPutString((PUCHAR)"\r\n");

            return STATUS_SUCCESS;
        }

        case HeadlessCmdStartBugCheck:
        {
            HeadlessGlobals->InBugCheck = TRUE;
            return STATUS_SUCCESS;
        }

        case HeadlessCmdDoBugCheckProcessing:
        {
            HdlspEnableTerminal(TRUE);
            return STATUS_SUCCESS;
        }

        case HeadlessCmdCheckForReboot:
        {
            return STATUS_SUCCESS;
        }

        default:
            break;
    }

    return STATUS_NOT_IMPLEMENTED;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
HeadlessDispatch(
    _In_ HEADLESS_CMD Command,
    _In_opt_ PVOID InputBuffer,
    _In_ SIZE_T InputBufferSize,
    _Out_opt_ PVOID OutputBuffer,
    _Inout_opt_ PSIZE_T OutputBufferSize)
{
    NTSTATUS Status;
    KIRQL OldIrql;

    if (HeadlessGlobals == NULL)
    {
        return STATUS_UNSUCCESSFUL;
    }

    KeAcquireSpinLock(&HeadlessGlobals->SpinLock, &OldIrql);

    Status = HdlspDispatch(Command,
                           InputBuffer,
                           InputBufferSize,
                           OutputBuffer,
                           OutputBufferSize);

    KeReleaseSpinLock(&HeadlessGlobals->SpinLock, OldIrql);

    return Status;
}

/*
 * Sets the terminal up from what the loader found. Nothing is set up at all
 * unless the loader says a terminal is there, so a machine with a head pays
 * nothing for this.
 */
CODE_SEG("INIT")
VOID
NTAPI
HeadlessInit(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PHEADLESS_LOADER_BLOCK Settings = LoaderBlock->Extension->HeadlessLoaderBlock;

    if (Settings == NULL)
    {
        return;
    }

    if (Settings->PortNumber >= RTL_NUMBER_OF(ExpSerialPortAddress))
    {
        return;
    }

    HeadlessGlobals = ExAllocatePoolZero(NonPagedPool,
                                         sizeof(HEADLESS_GLOBALS),
                                         EXP_HEADLESS_TAG);
    if (HeadlessGlobals == NULL)
    {
        return;
    }

    KeInitializeSpinLock(&HeadlessGlobals->SpinLock);

    HeadlessGlobals->TerminalPortNumber = Settings->PortNumber;
    HeadlessGlobals->TerminalBaudRate = Settings->BaudRate;
    HeadlessGlobals->TerminalType = Settings->TerminalType;
    HeadlessGlobals->UsedBiosSettings = Settings->UsedBiosSettings;
    HeadlessGlobals->SystemGUID = Settings->SystemGUID;
    HeadlessGlobals->IsMMIODevice = Settings->IsMMIODevice;

    if (Settings->PortAddress != NULL)
    {
        HeadlessGlobals->TerminalPortAddress = Settings->PortAddress;
    }
    else
    {
        HeadlessGlobals->TerminalPortAddress =
            (PUCHAR)(ULONG_PTR)ExpSerialPortAddress[Settings->PortNumber];
    }

    if (HeadlessGlobals->TerminalBaudRate == 0)
    {
        HeadlessGlobals->TerminalBaudRate = EXP_SERIAL_DEFAULT_BAUD;
    }

    HeadlessGlobals->InputBuffer = ExAllocatePoolZero(NonPagedPool,
                                                      EXP_HEADLESS_INPUT_SIZE,
                                                      EXP_HEADLESS_TAG);
    HeadlessGlobals->TmpBuffer = ExAllocatePoolZero(NonPagedPool,
                                                    EXP_HEADLESS_TMP_SIZE,
                                                    EXP_HEADLESS_TAG);
    HeadlessGlobals->LogEntries =
        ExAllocatePoolZero(NonPagedPool,
                           EXP_HEADLESS_LOG_ENTRIES *
                           sizeof(HEADLESS_LOG_ENTRY),
                           EXP_HEADLESS_TAG);

    HdlspEnableTerminal(TRUE);
}

/* EOF */
