/*
 * COPYRIGHT:   Copyright 2023 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
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


VOID
KdpFlushTerminalInput(VOID);

CHAR
KdpReadTermKey(
    _Out_ PULONG ScanCode);

BOOLEAN
KdpTerminalPollBreakIn(VOID);


CHAR
KdbpTryGetCharKeyboard(
    _Out_ PULONG ScanCode,
    _In_ ULONG Retry);

BOOLEAN
KdpKeyboardChordHeld(VOID);


BOOLEAN
KdpUsbKeyboardChordHeld(VOID);

