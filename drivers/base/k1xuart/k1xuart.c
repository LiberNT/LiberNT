/*
 * PROJECT:     LiberNT SpacemiT K1 UART driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SerCx2 controller driver for the SpacemiT K1 UARTs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <wdf.h>
#include <ntddser.h>
#include <SerCx.h>
#include <reshub.h>

#define NDEBUG
#include <debug.h>

#define K1XUART_TAG             'rUiK'
#define K1XUART_SIZE            0x100
#define K1XUART_FIFO_DEPTH      64
#define K1XUART_DEFAULT_BAUD    115200

#define K1XUART_RBR             0x00
#define K1XUART_THR             0x00
#define K1XUART_DLL             0x00
#define K1XUART_IER             0x04
#define K1XUART_DLH             0x04
#define K1XUART_IIR             0x08
#define K1XUART_FCR             0x08
#define K1XUART_LCR             0x0C
#define K1XUART_MCR             0x10
#define K1XUART_LSR             0x14
#define K1XUART_MSR             0x18

#define K1XUART_IER_RDI         0x01
#define K1XUART_IER_THRI        0x02
#define K1XUART_IER_RLSI        0x04
#define K1XUART_IER_RTOIE       0x10
#define K1XUART_IER_UUE         0x40
#define K1XUART_IER_RX          (K1XUART_IER_RDI | K1XUART_IER_RLSI | K1XUART_IER_RTOIE)

#define K1XUART_IIR_NO_INT      0x01
#define K1XUART_IIR_ID_MASK     0x0E
#define K1XUART_IIR_THRE        0x02
#define K1XUART_IIR_RLS         0x06

#define K1XUART_FCR_ENABLE      0x01
#define K1XUART_FCR_CLEAR_RX    0x02
#define K1XUART_FCR_CLEAR_TX    0x04
#define K1XUART_FCR_RX_TRIG8    0x40

#define K1XUART_LCR_WLEN_MASK   0x03
#define K1XUART_LCR_STOP        0x04
#define K1XUART_LCR_PARITY      0x08
#define K1XUART_LCR_EVEN        0x10
#define K1XUART_LCR_STICK       0x20
#define K1XUART_LCR_BREAK       0x40
#define K1XUART_LCR_DLAB        0x80

#define K1XUART_MCR_DTR         0x01
#define K1XUART_MCR_RTS         0x02
#define K1XUART_MCR_OUT2        0x08
#define K1XUART_MCR_AFE         0x20

#define K1XUART_LSR_DR          0x01
#define K1XUART_LSR_THRE        0x20
#define K1XUART_LSR_TEMT        0x40

#define K1X_APBC_BASE           0xD4015000ULL
#define K1X_APBC_SIZE           0x100
#define K1X_APBC_CLOCKS         0x00000003UL
#define K1X_APBC_RESET          0x00000004UL
#define K1X_APBC_MUX_SHIFT      4
#define K1X_APBC_MUX_MASK       0x00000070UL

typedef struct _K1XUART_UNIT
{
    ULONG64 Base;
    ULONG ApbcOffset;
} K1XUART_UNIT;

static const K1XUART_UNIT K1xUartUnits[] =
{
    { 0xD4017000ULL, 0x00 }, { 0xD4017100ULL, 0x04 }, { 0xD4017200ULL, 0x24 },
    { 0xD4017300ULL, 0x70 }, { 0xD4017400ULL, 0x74 }, { 0xD4017500ULL, 0x78 },
    { 0xD4017600ULL, 0x94 }, { 0xD4017700ULL, 0x98 }, { 0xD4017800ULL, 0x9C },
};

static const ULONG K1xUartClockRates[] = { 57600000, 14745600, 48000000 };

typedef struct _K1XUART
{
    WDFDEVICE Device;
    PUCHAR Base;
    ULONG Length;
    ULONG64 PhysicalBase;
    WDFINTERRUPT Interrupt;
    SERCX2PIORECEIVE PioReceive;
    SERCX2PIOTRANSMIT PioTransmit;
    WDFTIMER DrainTimer;
    ULONG ClockHz;
    ULONG BaudRate;
    SERIAL_LINE_CONTROL LineControl;
    SERIAL_HANDFLOW HandFlow;
    SERIAL_CHARS Chars;
    UCHAR Ier;
    UCHAR Mcr;
    UCHAR Lcr;
    BOOLEAN RxPending;
    BOOLEAN TxPending;
    BOOLEAN Draining;
} K1XUART, *PK1XUART;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(K1XUART, K1xUartGetContext)

typedef struct _K1XUART_PIO
{
    PK1XUART Uart;
} K1XUART_PIO, *PK1XUART_PIO;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(K1XUART_PIO, K1xUartGetPio)

#pragma pack(push, 1)
typedef struct _K1XUART_ACPI_UART_DESCRIPTOR
{
    UCHAR Tag;
    USHORT Length;
    UCHAR RevisionId;
    UCHAR ResourceSourceIndex;
    UCHAR SerialBusType;
    UCHAR GeneralFlags;
    USHORT TypeSpecificFlags;
    UCHAR TypeSpecificRevisionId;
    USHORT TypeDataLength;
    ULONG BaudRate;
    USHORT RxFifoSize;
    USHORT TxFifoSize;
    UCHAR Parity;
    UCHAR SerialLinesEnabled;
} K1XUART_ACPI_UART_DESCRIPTOR, *PK1XUART_ACPI_UART_DESCRIPTOR;
#pragma pack(pop)

#define K1XUART_ACPI_SERIAL_BUS_TAG 0x8E
#define K1XUART_ACPI_SERIAL_BUS_UART 3

static
UCHAR
K1xUartRead(
    _In_ PK1XUART Uart,
    _In_ ULONG Register)
{
    return (UCHAR)READ_REGISTER_ULONG((PULONG)(Uart->Base + Register));
}

static
VOID
K1xUartWrite(
    _In_ PK1XUART Uart,
    _In_ ULONG Register,
    _In_ UCHAR Value)
{
    WRITE_REGISTER_ULONG((PULONG)(Uart->Base + Register), Value);
}

static
VOID
K1xUartSetIer(
    _In_ PK1XUART Uart,
    _In_ UCHAR Set,
    _In_ UCHAR Clear)
{
    WdfInterruptAcquireLock(Uart->Interrupt);
    Uart->Ier = (UCHAR)((Uart->Ier & ~Clear) | Set);
    K1xUartWrite(Uart, K1XUART_IER, Uart->Ier);
    WdfInterruptReleaseLock(Uart->Interrupt);
}

static
NTSTATUS
K1xUartEnableClock(
    _In_ PK1XUART Uart)
{
    PHYSICAL_ADDRESS Address;
    PULONG Apbc;
    ULONG Index, Unit = MAXULONG, Value, Mux;

    for (Index = 0; Index < RTL_NUMBER_OF(K1xUartUnits); Index++)
    {
        if (K1xUartUnits[Index].Base == Uart->PhysicalBase)
            Unit = Index;
    }
    if (Unit == MAXULONG)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    Address.QuadPart = K1X_APBC_BASE;
    Apbc = MmMapIoSpace(Address, K1X_APBC_SIZE, MmNonCached);
    if (!Apbc)
        return STATUS_INSUFFICIENT_RESOURCES;

    Mux = (READ_REGISTER_ULONG(&Apbc[K1xUartUnits[0].ApbcOffset / sizeof(ULONG)]) & K1X_APBC_MUX_MASK) >> K1X_APBC_MUX_SHIFT;
    if (Mux >= RTL_NUMBER_OF(K1xUartClockRates))
        Mux = 0;
    Uart->ClockHz = K1xUartClockRates[Mux];
    if (Unit != 0)
    {
        PULONG Register = &Apbc[K1xUartUnits[Unit].ApbcOffset / sizeof(ULONG)];

        Value = READ_REGISTER_ULONG(Register) & ~(K1X_APBC_MUX_MASK | K1X_APBC_RESET);
        Value |= K1X_APBC_CLOCKS | (Mux << K1X_APBC_MUX_SHIFT);
        WRITE_REGISTER_ULONG(Register, Value | K1X_APBC_RESET);
        KeStallExecutionProcessor(10);
        WRITE_REGISTER_ULONG(Register, Value);
        KeStallExecutionProcessor(10);
    }
    MmUnmapIoSpace(Apbc, K1X_APBC_SIZE);
    return STATUS_SUCCESS;
}

static
NTSTATUS
K1xUartProgramLine(
    _In_ PK1XUART Uart,
    _In_ ULONG BaudRate,
    _In_ PSERIAL_LINE_CONTROL LineControl)
{
    ULONG Divisor, Actual;
    UCHAR Lcr;

    if (!BaudRate || !Uart->ClockHz)
        return STATUS_INVALID_PARAMETER;
    Divisor = (Uart->ClockHz + 8 * BaudRate) / (16 * BaudRate);
    if (!Divisor || Divisor > 0xFFFF)
        return STATUS_NOT_SUPPORTED;
    Actual = Uart->ClockHz / (16 * Divisor);
    if ((Actual > BaudRate ? Actual - BaudRate : BaudRate - Actual) > BaudRate / 33)
        return STATUS_NOT_SUPPORTED;
    if (LineControl->WordLength < 5 || LineControl->WordLength > 8 ||
        LineControl->StopBits > STOP_BITS_2 || LineControl->Parity > SPACE_PARITY ||
        (LineControl->StopBits == STOP_BITS_1_5 && LineControl->WordLength != 5))
        return STATUS_INVALID_PARAMETER;

    Lcr = (UCHAR)(LineControl->WordLength - 5);
    if (LineControl->StopBits != STOP_BIT_1)
        Lcr |= K1XUART_LCR_STOP;
    switch (LineControl->Parity)
    {
        case ODD_PARITY:   Lcr |= K1XUART_LCR_PARITY; break;
        case EVEN_PARITY:  Lcr |= K1XUART_LCR_PARITY | K1XUART_LCR_EVEN; break;
        case MARK_PARITY:  Lcr |= K1XUART_LCR_PARITY | K1XUART_LCR_STICK; break;
        case SPACE_PARITY: Lcr |= K1XUART_LCR_PARITY | K1XUART_LCR_EVEN | K1XUART_LCR_STICK; break;
        default: break;
    }
    Lcr |= Uart->Lcr & K1XUART_LCR_BREAK;

    WdfInterruptAcquireLock(Uart->Interrupt);
    K1xUartWrite(Uart, K1XUART_LCR, Lcr | K1XUART_LCR_DLAB);
    K1xUartWrite(Uart, K1XUART_DLL, (UCHAR)Divisor);
    K1xUartWrite(Uart, K1XUART_DLH, (UCHAR)(Divisor >> 8));
    K1xUartWrite(Uart, K1XUART_LCR, Lcr);
    K1xUartWrite(Uart, K1XUART_IER, Uart->Ier);
    WdfInterruptReleaseLock(Uart->Interrupt);

    Uart->Lcr = Lcr;
    Uart->BaudRate = BaudRate;
    Uart->LineControl = *LineControl;
    return STATUS_SUCCESS;
}

static
VOID
K1xUartProgramHandFlow(
    _In_ PK1XUART Uart,
    _In_ PSERIAL_HANDFLOW HandFlow)
{
    UCHAR Mcr = Uart->Mcr & ~(K1XUART_MCR_AFE | K1XUART_MCR_RTS | K1XUART_MCR_DTR);

    if ((HandFlow->ControlHandShake & SERIAL_CTS_HANDSHAKE) &&
        (HandFlow->FlowReplace & SERIAL_RTS_MASK) == SERIAL_RTS_HANDSHAKE)
        Mcr |= K1XUART_MCR_AFE | K1XUART_MCR_RTS;
    else if ((HandFlow->FlowReplace & SERIAL_RTS_MASK) == SERIAL_RTS_CONTROL)
        Mcr |= K1XUART_MCR_RTS;
    if ((HandFlow->ControlHandShake & SERIAL_DTR_MASK) == SERIAL_DTR_CONTROL)
        Mcr |= K1XUART_MCR_DTR;
    Uart->Mcr = Mcr;
    Uart->HandFlow = *HandFlow;
    K1xUartWrite(Uart, K1XUART_MCR, Mcr);
}

static
VOID
K1xUartResetHardware(
    _In_ PK1XUART Uart)
{
    WdfInterruptAcquireLock(Uart->Interrupt);
    Uart->Ier = K1XUART_IER_UUE;
    Uart->RxPending = FALSE;
    Uart->TxPending = FALSE;
    K1xUartWrite(Uart, K1XUART_IER, Uart->Ier);
    K1xUartWrite(Uart, K1XUART_FCR, K1XUART_FCR_ENABLE | K1XUART_FCR_CLEAR_RX | K1XUART_FCR_CLEAR_TX);
    K1xUartWrite(Uart, K1XUART_FCR, K1XUART_FCR_ENABLE | K1XUART_FCR_RX_TRIG8);
    (VOID)K1xUartRead(Uart, K1XUART_LSR);
    (VOID)K1xUartRead(Uart, K1XUART_RBR);
    (VOID)K1xUartRead(Uart, K1XUART_IIR);
    (VOID)K1xUartRead(Uart, K1XUART_MSR);
    WdfInterruptReleaseLock(Uart->Interrupt);
}

static
NTSTATUS
K1xUartApplyDefaults(
    _In_ PK1XUART Uart)
{
    SERIAL_LINE_CONTROL LineControl = { STOP_BIT_1, NO_PARITY, 8 };
    SERIAL_HANDFLOW HandFlow;

    RtlZeroMemory(&HandFlow, sizeof(HandFlow));
    HandFlow.ControlHandShake = SERIAL_DTR_CONTROL;
    HandFlow.FlowReplace = SERIAL_RTS_CONTROL;
    K1xUartProgramHandFlow(Uart, &HandFlow);
    return K1xUartProgramLine(Uart, K1XUART_DEFAULT_BAUD, &LineControl);
}

static
NTSTATUS
K1xUartEvtApplyConfig(
    _In_ WDFDEVICE Device,
    _In_ PVOID ConnectionParameters)
{
    PK1XUART Uart = K1xUartGetContext(Device);
    PRH_QUERY_CONNECTION_PROPERTIES_OUTPUT_BUFFER Properties = ConnectionParameters;
    PK1XUART_ACPI_UART_DESCRIPTOR Descriptor;
    SERIAL_LINE_CONTROL LineControl;
    SERIAL_HANDFLOW HandFlow;
    USHORT Flags;

    if (!Properties || Properties->PropertiesLength < sizeof(*Descriptor))
        return STATUS_INVALID_PARAMETER;
    Descriptor = (PK1XUART_ACPI_UART_DESCRIPTOR)Properties->ConnectionProperties;
    if (Descriptor->Tag != K1XUART_ACPI_SERIAL_BUS_TAG || Descriptor->SerialBusType != K1XUART_ACPI_SERIAL_BUS_UART)
        return STATUS_NOT_SUPPORTED;

    Flags = Descriptor->TypeSpecificFlags;
    LineControl.WordLength = (UCHAR)(5 + ((Flags >> 4) & 7));
    LineControl.StopBits = ((Flags >> 2) & 3) == 3 ? STOP_BITS_2 : (((Flags >> 2) & 3) == 2 ? STOP_BITS_1_5 : STOP_BIT_1);
    switch (Descriptor->Parity)
    {
        case 1:  LineControl.Parity = EVEN_PARITY; break;
        case 2:  LineControl.Parity = ODD_PARITY; break;
        case 3:  LineControl.Parity = MARK_PARITY; break;
        case 4:  LineControl.Parity = SPACE_PARITY; break;
        default: LineControl.Parity = NO_PARITY; break;
    }
    RtlZeroMemory(&HandFlow, sizeof(HandFlow));
    if ((Flags & 3) == 1)
    {
        HandFlow.ControlHandShake = SERIAL_CTS_HANDSHAKE;
        HandFlow.FlowReplace = SERIAL_RTS_HANDSHAKE;
    }
    else
    {
        HandFlow.FlowReplace = SERIAL_RTS_CONTROL;
    }
    K1xUartProgramHandFlow(Uart, &HandFlow);
    return K1xUartProgramLine(Uart, Descriptor->BaudRate ? Descriptor->BaudRate : K1XUART_DEFAULT_BAUD, &LineControl);
}

static
NTSTATUS
K1xUartEvtControl(
    _In_ WDFDEVICE Device,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    PK1XUART Uart = K1xUartGetContext(Device);
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG_PTR Information = 0;
    PVOID Buffer;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    switch (IoControlCode)
    {
        case IOCTL_SERIAL_SET_BAUD_RATE:
            Status = WdfRequestRetrieveInputBuffer(Request, sizeof(SERIAL_BAUD_RATE), &Buffer, NULL);
            if (NT_SUCCESS(Status))
                Status = K1xUartProgramLine(Uart, ((PSERIAL_BAUD_RATE)Buffer)->BaudRate, &Uart->LineControl);
            break;

        case IOCTL_SERIAL_GET_BAUD_RATE:
            Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(SERIAL_BAUD_RATE), &Buffer, NULL);
            if (NT_SUCCESS(Status))
            {
                ((PSERIAL_BAUD_RATE)Buffer)->BaudRate = Uart->BaudRate;
                Information = sizeof(SERIAL_BAUD_RATE);
            }
            break;

        case IOCTL_SERIAL_SET_LINE_CONTROL:
            Status = WdfRequestRetrieveInputBuffer(Request, sizeof(SERIAL_LINE_CONTROL), &Buffer, NULL);
            if (NT_SUCCESS(Status))
                Status = K1xUartProgramLine(Uart, Uart->BaudRate, Buffer);
            break;

        case IOCTL_SERIAL_GET_LINE_CONTROL:
            Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(SERIAL_LINE_CONTROL), &Buffer, NULL);
            if (NT_SUCCESS(Status))
            {
                *(PSERIAL_LINE_CONTROL)Buffer = Uart->LineControl;
                Information = sizeof(SERIAL_LINE_CONTROL);
            }
            break;

        case IOCTL_SERIAL_SET_HANDFLOW:
            Status = WdfRequestRetrieveInputBuffer(Request, sizeof(SERIAL_HANDFLOW), &Buffer, NULL);
            if (NT_SUCCESS(Status))
                K1xUartProgramHandFlow(Uart, Buffer);
            break;

        case IOCTL_SERIAL_GET_HANDFLOW:
            Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(SERIAL_HANDFLOW), &Buffer, NULL);
            if (NT_SUCCESS(Status))
            {
                *(PSERIAL_HANDFLOW)Buffer = Uart->HandFlow;
                Information = sizeof(SERIAL_HANDFLOW);
            }
            break;

        case IOCTL_SERIAL_SET_CHARS:
            Status = WdfRequestRetrieveInputBuffer(Request, sizeof(SERIAL_CHARS), &Buffer, NULL);
            if (NT_SUCCESS(Status))
                Uart->Chars = *(PSERIAL_CHARS)Buffer;
            break;

        case IOCTL_SERIAL_GET_CHARS:
            Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(SERIAL_CHARS), &Buffer, NULL);
            if (NT_SUCCESS(Status))
            {
                *(PSERIAL_CHARS)Buffer = Uart->Chars;
                Information = sizeof(SERIAL_CHARS);
            }
            break;

        case IOCTL_SERIAL_SET_DTR:
        case IOCTL_SERIAL_CLR_DTR:
        case IOCTL_SERIAL_SET_RTS:
        case IOCTL_SERIAL_CLR_RTS:
        {
            UCHAR Bit = (IoControlCode == IOCTL_SERIAL_SET_DTR || IoControlCode == IOCTL_SERIAL_CLR_DTR) ?
                        K1XUART_MCR_DTR : K1XUART_MCR_RTS;

            if (IoControlCode == IOCTL_SERIAL_SET_DTR || IoControlCode == IOCTL_SERIAL_SET_RTS)
                Uart->Mcr |= Bit;
            else
                Uart->Mcr &= ~Bit;
            K1xUartWrite(Uart, K1XUART_MCR, Uart->Mcr);
            break;
        }

        case IOCTL_SERIAL_GET_DTRRTS:
            Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(ULONG), &Buffer, NULL);
            if (NT_SUCCESS(Status))
            {
                *(PULONG)Buffer = ((Uart->Mcr & K1XUART_MCR_DTR) ? SERIAL_DTR_STATE : 0) |
                                  ((Uart->Mcr & K1XUART_MCR_RTS) ? SERIAL_RTS_STATE : 0);
                Information = sizeof(ULONG);
            }
            break;

        case IOCTL_SERIAL_GET_MODEMSTATUS:
            Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(ULONG), &Buffer, NULL);
            if (NT_SUCCESS(Status))
            {
                *(PULONG)Buffer = K1xUartRead(Uart, K1XUART_MSR);
                Information = sizeof(ULONG);
            }
            break;

        case IOCTL_SERIAL_SET_BREAK_ON:
        case IOCTL_SERIAL_SET_BREAK_OFF:
            if (IoControlCode == IOCTL_SERIAL_SET_BREAK_ON)
                Uart->Lcr |= K1XUART_LCR_BREAK;
            else
                Uart->Lcr &= ~K1XUART_LCR_BREAK;
            K1xUartWrite(Uart, K1XUART_LCR, Uart->Lcr);
            break;

        case IOCTL_SERIAL_GET_COMMSTATUS:
            Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(SERIAL_STATUS), &Buffer, NULL);
            if (NT_SUCCESS(Status))
            {
                RtlZeroMemory(Buffer, sizeof(SERIAL_STATUS));
                Information = sizeof(SERIAL_STATUS);
            }
            break;

        case IOCTL_SERIAL_GET_PROPERTIES:
            Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(SERIAL_COMMPROP), &Buffer, NULL);
            if (NT_SUCCESS(Status))
            {
                PSERIAL_COMMPROP Properties = Buffer;

                RtlZeroMemory(Properties, sizeof(*Properties));
                Properties->PacketLength = sizeof(*Properties);
                Properties->PacketVersion = 2;
                Properties->ServiceMask = SERIAL_SP_SERIALCOMM;
                Properties->MaxTxQueue = K1XUART_FIFO_DEPTH;
                Properties->MaxRxQueue = K1XUART_FIFO_DEPTH;
                Properties->MaxBaud = SERIAL_BAUD_USER;
                Properties->ProvSubType = SERIAL_SP_RS232;
                Properties->ProvCapabilities = SERIAL_PCF_DTRDSR | SERIAL_PCF_RTSCTS | SERIAL_PCF_TOTALTIMEOUTS |
                                               SERIAL_PCF_INTTIMEOUTS | SERIAL_PCF_PARITY_CHECK;
                Properties->SettableParams = SERIAL_SP_PARITY | SERIAL_SP_BAUD | SERIAL_SP_DATABITS |
                                             SERIAL_SP_STOPBITS | SERIAL_SP_HANDSHAKING;
                Properties->SettableBaud = SERIAL_BAUD_USER | SERIAL_BAUD_9600 | SERIAL_BAUD_19200 |
                                           SERIAL_BAUD_38400 | SERIAL_BAUD_57600 | SERIAL_BAUD_115200;
                Properties->SettableData = SERIAL_DATABITS_5 | SERIAL_DATABITS_6 | SERIAL_DATABITS_7 | SERIAL_DATABITS_8;
                Properties->SettableStopParity = SERIAL_STOPBITS_10 | SERIAL_STOPBITS_15 | SERIAL_STOPBITS_20 |
                                                 SERIAL_PARITY_NONE | SERIAL_PARITY_ODD | SERIAL_PARITY_EVEN |
                                                 SERIAL_PARITY_MARK | SERIAL_PARITY_SPACE;
                Properties->CurrentTxQueue = K1XUART_FIFO_DEPTH;
                Properties->CurrentRxQueue = K1XUART_FIFO_DEPTH;
                Information = sizeof(*Properties);
            }
            break;

        default:
            Status = STATUS_NOT_SUPPORTED;
            break;
    }

    WdfRequestCompleteWithInformation(Request, Status, Information);
    return Status;
}

static
VOID
K1xUartEvtPurgeFifos(
    _In_ WDFDEVICE Device,
    _In_ BOOLEAN PurgeRxFifo,
    _In_ BOOLEAN PurgeTxFifo)
{
    PK1XUART Uart = K1xUartGetContext(Device);
    UCHAR Fcr = K1XUART_FCR_ENABLE | K1XUART_FCR_RX_TRIG8;

    if (PurgeRxFifo)
        Fcr |= K1XUART_FCR_CLEAR_RX;
    if (PurgeTxFifo)
        Fcr |= K1XUART_FCR_CLEAR_TX;
    K1xUartWrite(Uart, K1XUART_FCR, Fcr);
}

static
NTSTATUS
K1xUartEvtFileOpen(
    _In_ WDFDEVICE Device)
{
    PK1XUART Uart = K1xUartGetContext(Device);

    if (!Uart->BaudRate)
        return K1xUartApplyDefaults(Uart);
    return STATUS_SUCCESS;
}

static
VOID
K1xUartEvtFileClose(
    _In_ WDFDEVICE Device)
{
    PK1XUART Uart = K1xUartGetContext(Device);

    K1xUartSetIer(Uart, 0, K1XUART_IER_RX | K1XUART_IER_THRI);
    Uart->BaudRate = 0;
}

static
ULONG
K1xUartEvtPioReceiveReadBuffer(
    _In_ SERCX2PIORECEIVE PioReceive,
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length)
{
    PK1XUART Uart = K1xUartGetPio(PioReceive)->Uart;
    ULONG Read = 0;

    while (Read < Length && (K1xUartRead(Uart, K1XUART_LSR) & K1XUART_LSR_DR))
        Buffer[Read++] = K1xUartRead(Uart, K1XUART_RBR);
    return Read;
}

static
VOID
K1xUartEvtPioReceiveEnableReady(
    _In_ SERCX2PIORECEIVE PioReceive)
{
    PK1XUART Uart = K1xUartGetPio(PioReceive)->Uart;

    WdfInterruptAcquireLock(Uart->Interrupt);
    Uart->RxPending = FALSE;
    Uart->Ier |= K1XUART_IER_RX;
    K1xUartWrite(Uart, K1XUART_IER, Uart->Ier);
    WdfInterruptReleaseLock(Uart->Interrupt);
}

static
BOOLEAN
K1xUartEvtPioReceiveCancelReady(
    _In_ SERCX2PIORECEIVE PioReceive)
{
    PK1XUART Uart = K1xUartGetPio(PioReceive)->Uart;
    BOOLEAN Cancelled;

    WdfInterruptAcquireLock(Uart->Interrupt);
    Cancelled = (Uart->Ier & K1XUART_IER_RDI) != 0;
    Uart->Ier &= ~K1XUART_IER_RX;
    K1xUartWrite(Uart, K1XUART_IER, Uart->Ier);
    WdfInterruptReleaseLock(Uart->Interrupt);
    return Cancelled;
}

static
ULONG
K1xUartEvtPioTransmitWriteBuffer(
    _In_ SERCX2PIOTRANSMIT PioTransmit,
    _In_reads_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length)
{
    PK1XUART Uart = K1xUartGetPio(PioTransmit)->Uart;
    UCHAR Lsr = K1xUartRead(Uart, K1XUART_LSR);
    ULONG Space, Written = 0;

    if (Lsr & K1XUART_LSR_TEMT)
        Space = K1XUART_FIFO_DEPTH;
    else if (Lsr & K1XUART_LSR_THRE)
        Space = K1XUART_FIFO_DEPTH / 2;
    else
        Space = 0;
    while (Written < Length && Written < Space)
        K1xUartWrite(Uart, K1XUART_THR, Buffer[Written++]);
    return Written;
}

static
VOID
K1xUartEvtPioTransmitEnableReady(
    _In_ SERCX2PIOTRANSMIT PioTransmit)
{
    PK1XUART Uart = K1xUartGetPio(PioTransmit)->Uart;

    WdfInterruptAcquireLock(Uart->Interrupt);
    Uart->TxPending = FALSE;
    Uart->Ier |= K1XUART_IER_THRI;
    K1xUartWrite(Uart, K1XUART_IER, Uart->Ier);
    WdfInterruptReleaseLock(Uart->Interrupt);
}

static
BOOLEAN
K1xUartEvtPioTransmitCancelReady(
    _In_ SERCX2PIOTRANSMIT PioTransmit)
{
    PK1XUART Uart = K1xUartGetPio(PioTransmit)->Uart;
    BOOLEAN Cancelled;

    WdfInterruptAcquireLock(Uart->Interrupt);
    Cancelled = (Uart->Ier & K1XUART_IER_THRI) != 0;
    Uart->Ier &= ~K1XUART_IER_THRI;
    K1xUartWrite(Uart, K1XUART_IER, Uart->Ier);
    WdfInterruptReleaseLock(Uart->Interrupt);
    return Cancelled;
}

static
VOID
K1xUartEvtDrainTimer(
    _In_ WDFTIMER Timer)
{
    PK1XUART Uart = K1xUartGetContext(WdfTimerGetParentObject(Timer));

    if (!Uart->Draining)
        return;
    if (K1xUartRead(Uart, K1XUART_LSR) & K1XUART_LSR_TEMT)
    {
        Uart->Draining = FALSE;
        SerCx2PioTransmitDrainFifoComplete(Uart->PioTransmit);
        return;
    }
    WdfTimerStart(Timer, WDF_REL_TIMEOUT_IN_MS(1));
}

static
VOID
K1xUartEvtPioTransmitDrainFifo(
    _In_ SERCX2PIOTRANSMIT PioTransmit)
{
    PK1XUART Uart = K1xUartGetPio(PioTransmit)->Uart;

    Uart->Draining = TRUE;
    WdfTimerStart(Uart->DrainTimer, 0);
}

static
BOOLEAN
K1xUartEvtPioTransmitCancelDrainFifo(
    _In_ SERCX2PIOTRANSMIT PioTransmit)
{
    PK1XUART Uart = K1xUartGetPio(PioTransmit)->Uart;
    BOOLEAN Cancelled = Uart->Draining;

    Uart->Draining = FALSE;
    WdfTimerStop(Uart->DrainTimer, FALSE);
    return Cancelled;
}

static
VOID
K1xUartEvtPioTransmitPurgeFifo(
    _In_ SERCX2PIOTRANSMIT PioTransmit,
    _In_ ULONG BytesAlreadyTransmittedToFifo)
{
    PK1XUART Uart = K1xUartGetPio(PioTransmit)->Uart;

    UNREFERENCED_PARAMETER(BytesAlreadyTransmittedToFifo);
    K1xUartWrite(Uart, K1XUART_FCR, K1XUART_FCR_ENABLE | K1XUART_FCR_RX_TRIG8 | K1XUART_FCR_CLEAR_TX);
    SerCx2PioTransmitPurgeFifoComplete(PioTransmit, 0);
}

static
BOOLEAN
K1xUartEvtInterruptIsr(
    _In_ WDFINTERRUPT Interrupt,
    _In_ ULONG MessageId)
{
    PK1XUART Uart = K1xUartGetContext(WdfInterruptGetDevice(Interrupt));
    BOOLEAN Claimed = FALSE;
    UCHAR Iir;

    UNREFERENCED_PARAMETER(MessageId);
    for (;;)
    {
        Iir = K1xUartRead(Uart, K1XUART_IIR);
        if (Iir & K1XUART_IIR_NO_INT)
            break;
        Claimed = TRUE;
        switch (Iir & K1XUART_IIR_ID_MASK)
        {
            case K1XUART_IIR_THRE:
                Uart->Ier &= ~K1XUART_IER_THRI;
                Uart->TxPending = TRUE;
                break;
            case K1XUART_IIR_RLS:
                (VOID)K1xUartRead(Uart, K1XUART_LSR);
                Uart->Ier &= ~K1XUART_IER_RX;
                Uart->RxPending = TRUE;
                break;
            case 0x00:
                (VOID)K1xUartRead(Uart, K1XUART_MSR);
                break;
            default:
                Uart->Ier &= ~K1XUART_IER_RX;
                Uart->RxPending = TRUE;
                break;
        }
        K1xUartWrite(Uart, K1XUART_IER, Uart->Ier);
    }
    if (Uart->RxPending || Uart->TxPending)
        WdfInterruptQueueDpcForIsr(Interrupt);
    return Claimed;
}

static
VOID
K1xUartEvtInterruptDpc(
    _In_ WDFINTERRUPT Interrupt,
    _In_ WDFOBJECT AssociatedObject)
{
    PK1XUART Uart = K1xUartGetContext(WdfInterruptGetDevice(Interrupt));
    BOOLEAN Rx, Tx;

    UNREFERENCED_PARAMETER(AssociatedObject);
    WdfInterruptAcquireLock(Interrupt);
    Rx = Uart->RxPending;
    Tx = Uart->TxPending;
    Uart->RxPending = FALSE;
    Uart->TxPending = FALSE;
    WdfInterruptReleaseLock(Interrupt);
    if (Rx)
        SerCx2PioReceiveReady(Uart->PioReceive);
    if (Tx)
        SerCx2PioTransmitReady(Uart->PioTransmit);
}

static
NTSTATUS
K1xUartEvtPrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    PK1XUART Uart = K1xUartGetContext(Device);
    ULONG Index;

    UNREFERENCED_PARAMETER(ResourcesRaw);
    for (Index = 0; Index < WdfCmResourceListGetCount(ResourcesTranslated); Index++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Resource = WdfCmResourceListGetDescriptor(ResourcesTranslated, Index);

        if (!Resource || Resource->Type != CmResourceTypeMemory || Uart->Base)
            continue;
        if (Resource->u.Memory.Length < K1XUART_SIZE)
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        Uart->PhysicalBase = (ULONG64)Resource->u.Memory.Start.QuadPart;
        Uart->Base = MmMapIoSpace(Resource->u.Memory.Start, Resource->u.Memory.Length, MmNonCached);
        if (!Uart->Base)
            return STATUS_INSUFFICIENT_RESOURCES;
        Uart->Length = Resource->u.Memory.Length;
    }
    if (!Uart->Base)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    return K1xUartEnableClock(Uart);
}

static
NTSTATUS
K1xUartEvtReleaseHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    PK1XUART Uart = K1xUartGetContext(Device);

    UNREFERENCED_PARAMETER(ResourcesTranslated);
    if (Uart->Base)
        MmUnmapIoSpace(Uart->Base, Uart->Length);
    Uart->Base = NULL;
    return STATUS_SUCCESS;
}

static
NTSTATUS
K1xUartEvtD0Entry(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState)
{
    PK1XUART Uart = K1xUartGetContext(Device);

    UNREFERENCED_PARAMETER(PreviousState);
    Uart->Mcr = K1XUART_MCR_OUT2;
    K1xUartWrite(Uart, K1XUART_MCR, Uart->Mcr);
    K1xUartResetHardware(Uart);
    DPRINT1("K1XUART: %I64x ready, %lu Hz clock\n", Uart->PhysicalBase, Uart->ClockHz);
    return STATUS_SUCCESS;
}

static
NTSTATUS
K1xUartEvtD0Exit(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState)
{
    PK1XUART Uart = K1xUartGetContext(Device);

    UNREFERENCED_PARAMETER(TargetState);
    K1xUartSetIer(Uart, 0, (UCHAR)~K1XUART_IER_UUE);
    return STATUS_SUCCESS;
}

static
NTSTATUS
K1xUartEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    WDF_PNPPOWER_EVENT_CALLBACKS PnpPower;
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDF_INTERRUPT_CONFIG InterruptConfig;
    WDF_TIMER_CONFIG TimerConfig;
    SERCX2_CONFIG Config;
    SERCX2_PIO_RECEIVE_CONFIG ReceiveConfig;
    SERCX2_PIO_TRANSMIT_CONFIG TransmitConfig;
    WDFDEVICE Device;
    PK1XUART Uart;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Driver);
    Status = SerCx2InitializeDeviceInit(DeviceInit);
    if (!NT_SUCCESS(Status))
        return Status;

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&PnpPower);
    PnpPower.EvtDevicePrepareHardware = K1xUartEvtPrepareHardware;
    PnpPower.EvtDeviceReleaseHardware = K1xUartEvtReleaseHardware;
    PnpPower.EvtDeviceD0Entry = K1xUartEvtD0Entry;
    PnpPower.EvtDeviceD0Exit = K1xUartEvtD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &PnpPower);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, K1XUART);
    Status = WdfDeviceCreate(&DeviceInit, &Attributes, &Device);
    if (!NT_SUCCESS(Status))
        return Status;
    Uart = K1xUartGetContext(Device);
    Uart->Device = Device;
    Uart->LineControl.StopBits = STOP_BIT_1;
    Uart->LineControl.Parity = NO_PARITY;
    Uart->LineControl.WordLength = 8;

    SERCX2_CONFIG_INIT(&Config, K1xUartEvtApplyConfig, K1xUartEvtControl, K1xUartEvtPurgeFifos);
    Config.EvtSerCx2FileOpen = K1xUartEvtFileOpen;
    Config.EvtSerCx2FileClose = K1xUartEvtFileClose;
    Status = SerCx2InitializeDevice(Device, &Config);
    if (!NT_SUCCESS(Status))
        return Status;

    WDF_INTERRUPT_CONFIG_INIT(&InterruptConfig, K1xUartEvtInterruptIsr, K1xUartEvtInterruptDpc);
    Status = WdfInterruptCreate(Device, &InterruptConfig, WDF_NO_OBJECT_ATTRIBUTES, &Uart->Interrupt);
    if (!NT_SUCCESS(Status))
        return Status;

    WDF_TIMER_CONFIG_INIT(&TimerConfig, K1xUartEvtDrainTimer);
    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ParentObject = Device;
    Status = WdfTimerCreate(&TimerConfig, &Attributes, &Uart->DrainTimer);
    if (!NT_SUCCESS(Status))
        return Status;

    SERCX2_PIO_RECEIVE_CONFIG_INIT(&ReceiveConfig, K1xUartEvtPioReceiveReadBuffer,
                                   K1xUartEvtPioReceiveEnableReady, K1xUartEvtPioReceiveCancelReady);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, K1XUART_PIO);
    Status = SerCx2PioReceiveCreate(Device, &ReceiveConfig, &Attributes, &Uart->PioReceive);
    if (!NT_SUCCESS(Status))
        return Status;
    K1xUartGetPio(Uart->PioReceive)->Uart = Uart;

    SERCX2_PIO_TRANSMIT_CONFIG_INIT(&TransmitConfig, K1xUartEvtPioTransmitWriteBuffer,
                                    K1xUartEvtPioTransmitEnableReady, K1xUartEvtPioTransmitCancelReady);
    TransmitConfig.EvtSerCx2PioTransmitDrainFifo = K1xUartEvtPioTransmitDrainFifo;
    TransmitConfig.EvtSerCx2PioTransmitCancelDrainFifo = K1xUartEvtPioTransmitCancelDrainFifo;
    TransmitConfig.EvtSerCx2PioTransmitPurgeFifo = K1xUartEvtPioTransmitPurgeFifo;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, K1XUART_PIO);
    Status = SerCx2PioTransmitCreate(Device, &TransmitConfig, &Attributes, &Uart->PioTransmit);
    if (!NT_SUCCESS(Status))
        return Status;
    K1xUartGetPio(Uart->PioTransmit)->Uart = Uart;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG Config;

    WDF_DRIVER_CONFIG_INIT(&Config, K1xUartEvtDeviceAdd);
    Config.DriverPoolTag = K1XUART_TAG;
    return WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &Config, WDF_NO_HANDLE);
}
