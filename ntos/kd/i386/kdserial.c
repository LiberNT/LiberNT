/*
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

//#include <cportlib/cportlib.h>
#include <cportlib/uartinfo.h>

BOOLEAN
NTAPI
KdPortIsComPortNumberValid(
    _In_ ULONG ComPortNumber)
{
    return ComPortNumber > 0 && ComPortNumber <= MAX_COM_PORTS;
}

BOOLEAN
NTAPI
KdPortInitializeEx(
{
    NTSTATUS Status;

    Status = CpInitialize(PortInformation,
                          (ComPortNumber == 0 ? PortInformation->Address
                                              : UlongToPtr(BaseArray[ComPortNumber])),
                          (PortInformation->BaudRate == 0 ? DEFAULT_DEBUG_BAUD_RATE
                                                          : PortInformation->BaudRate));
    if (!NT_SUCCESS(Status))
    {
        HalDisplayString("\r\nKernel Debugger: Serial port not available\r\n\r\n");
        return FALSE;
    }
    else
    {
#ifndef NDEBUG
        int Length;
        CHAR Buffer[82];

        Length = snprintf(Buffer, sizeof(Buffer),
                          "\r\nKernel Debugger: Using COM%lu (Port 0x%p) BaudRate %lu\r\n\r\n",
                          ComPortNumber,
                          PortInformation->Address,
                          PortInformation->BaudRate);
        if (Length == -1)
        {
            Buffer[sizeof(Buffer) - 1] = ANSI_NULL;
        }

        HalDisplayString(Buffer);
#endif
        return TRUE;
    }
}

BOOLEAN
NTAPI
KdPortGetByteEx(
{
    return (CpGetByte(PortInformation, ByteReceived, FALSE, FALSE) == CP_GET_SUCCESS);
}

VOID
NTAPI
KdPortPutByteEx(
{
    CpPutByte(PortInformation, ByteToSend);
}

VOID
NTAPI
KdPortPutBufferEx(
{
    ULONG Index;

    for (Index = 0; Index < Length; Index++)
    {
        CpPutByte(PortInformation, (UCHAR)Buffer[Index]);
    }
}

BOOLEAN
KdpArchKeyboardControllerPresent(VOID)
{
    return TRUE;
}
