/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel debugger terminal declarations
 * COPYRIGHT:   Copyright 2023 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once
#define KEY_BS          8
#define KEY_ESC         27
#define KEY_DEL         127
#define KEY_SCAN_UP     72
#define KEY_SCAN_DOWN   80
#define KEYSC_END       0x004f
#define KEYSC_PAGEUP    0x0049
#define KEYSC_PAGEDOWN  0x0051
#define KEYSC_HOME      0x0047
#define KEYSC_ARROWUP   0x0048

typedef struct _SIZE
{
    LONG cx;
    LONG cy;
} SIZE, *PSIZE;

typedef enum _KDB_OUTPUT_SETTINGS
{
    KD_DEBUG_KDSERIAL = 1,
    KD_DEBUG_KDNOECHO = 2
} KDB_OUTPUT_SETTINGS;

extern ULONG KdbDebugState;

extern SIZE KdTermSize;

extern BOOLEAN KdTermConnected;

extern BOOLEAN KdTermSerial;

extern BOOLEAN KdTermReportsSize;

extern BOOLEAN KdTermCrashMode;

BOOLEAN
KdpArchKeyboardControllerPresent(VOID);

VOID
KdbpSendCommandSerial(
    _In_ PCSTR Command);

CHAR
KdbpTryGetCharSerial(
    _In_ ULONG Retry);

VOID
KdpFlushTerminalInput(VOID);

BOOLEAN
KdpInitTerminal(VOID);

CHAR
KdpReadTermKey(
    _Out_ PULONG ScanCode);

BOOLEAN
KdpTerminalPollBreakIn(VOID);

BOOLEAN
KdpUpdateTerminalSize(
    _Out_ PSIZE TermSize);

CHAR
KdbpTryGetCharKeyboard(
    _Out_ PULONG ScanCode,
    _In_ ULONG Retry);

BOOLEAN
KdpKeyboardChordHeld(VOID);

CHAR
KdpScanCodeToChar(
    _In_ UCHAR ScanCode,
    _In_ BOOLEAN Shift);

CHAR
KdpControlChar(
    _In_ CHAR Char);

BOOLEAN
KdpUsbKeyboardChordHeld(VOID);

VOID
KdpUsbKeyboardInitialize(VOID);

CHAR
KdpUsbKeyboardTryGetChar(
    _Out_ PULONG ScanCode);
