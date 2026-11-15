/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later),
 *              with portions under the BSD license in COPYING.ARM
 * PURPOSE:     Headless terminal dispatch interface shared with the SAC driver
 * COPYRIGHT:   Copyright ReactOS Portable Systems Group
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ndk/extypes.h>

typedef enum _HEADLESS_CMD
{
    HeadlessCmdEnableTerminal = 1,
    HeadlessCmdCheckForReboot = 2,
    HeadlessCmdPutString = 3,
    HeadlessCmdClearDisplay = 4,
    HeadlessCmdClearToEndOfDisplay = 5,
    HeadlessCmdClearToEndOfLine = 6,
    HeadlessCmdDisplayAttributesOff = 7,
    HeadlessCmdDisplayInverseVideo = 8,
    HeadlessCmdSetColor = 9,
    HeadlessCmdPositionCursor = 10,
    HeadlessCmdTerminalPoll = 11,
    HeadlessCmdGetByte = 12,
    HeadlessCmdGetLine = 13,
    HeadlessCmdStartBugCheck = 14,
    HeadlessCmdDoBugCheckProcessing = 15,
    HeadlessCmdQueryInformation = 16,
    HeadlessCmdAddLogEntry = 17,
    HeadlessCmdDisplayLog = 18,
    HeadlessCmdSetBlueScreenData = 19,
    HeadlessCmdSendBlueScreenData = 20,
    HeadlessCmdQueryGUID = 21,
    HeadlessCmdPutData = 22,
} HEADLESS_CMD, *PHEADLESS_CMD;

typedef enum _HEADLESS_TERM_PORT_TYPE
{
    HeadlessUndefinedPortType = 0,
    HeadlessSerialPort = 1,
} HEADLESS_TERM_PORT_TYPE, *PHEADLESS_TERM_PORT_TYPE;

typedef enum _HEADLESS_TERM_SERIAL_PORT
{
    SerialPortUndefined = 0,
    ComPort1 = 1,
    ComPort2 = 2,
    ComPort3 = 3,
    ComPort4 = 4,
} HEADLESS_TERM_SERIAL_PORT, *PHEADLESS_TERM_SERIAL_PORT;

typedef struct _HEADLESS_CMD_ENABLE_TERMINAL
{
    BOOLEAN Enable;
} HEADLESS_CMD_ENABLE_TERMINAL, *PHEADLESS_CMD_ENABLE_TERMINAL;

typedef struct _HEADLESS_CMD_PUT_STRING
{
    UCHAR String[1];
} HEADLESS_CMD_PUT_STRING, *PHEADLESS_CMD_PUT_STRING;

typedef struct _HEADLESS_CMD_SET_COLOR
{
    ULONG TextColor;
    ULONG BkgdColor;
} HEADLESS_CMD_SET_COLOR, *PHEADLESS_CMD_SET_COLOR;

typedef struct _HEADLESS_CMD_POSITION_CURSOR
{
    ULONG CursorCol;
    ULONG CursorRow;
} HEADLESS_CMD_POSITION_CURSOR, *PHEADLESS_CMD_POSITION_CURSOR;

typedef struct _HEADLESS_CMD_SET_BLUE_SCREEN_DATA
{
    ULONG ValueIndex;
    UCHAR Data[ANYSIZE_ARRAY];
} HEADLESS_CMD_SET_BLUE_SCREEN_DATA, *PHEADLESS_CMD_SET_BLUE_SCREEN_DATA;

typedef struct _HEADLESS_RSP_GET_BYTE
{
    UCHAR Value;
} HEADLESS_RSP_GET_BYTE, *PHEADLESS_RSP_GET_BYTE;

typedef struct _HEADLESS_RSP_QUERY_INFO
{
    HEADLESS_TERM_PORT_TYPE PortType;
    union
    {
        struct
        {
            BOOLEAN TerminalAttached;
            BOOLEAN UsedBiosSettings;
            HEADLESS_TERM_SERIAL_PORT TerminalPort;
            PUCHAR TerminalPortBaseAddress;
            ULONG TerminalBaudRate;
            UCHAR TerminalType;
        } Serial;
    };
} HEADLESS_RSP_QUERY_INFO, *PHEADLESS_RSP_QUERY_INFO;

NTSTATUS
NTAPI
HeadlessDispatch(
    _In_ HEADLESS_CMD Command,
    _In_ PVOID InputBuffer,
    _In_ SIZE_T InputBufferSize,
    _Out_ PVOID OutputBuffer,
    _Out_ PSIZE_T OutputBufferSize);
