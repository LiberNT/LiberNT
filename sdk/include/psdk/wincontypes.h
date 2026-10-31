/*
 * Copyright (C) the Wine project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef _WINCONTYPES_
#define _WINCONTYPES_

typedef struct _SMALL_RECT {
    SHORT Left;
    SHORT Top;
    SHORT Right;
    SHORT Bottom;
} SMALL_RECT,*PSMALL_RECT;

typedef struct _COORD {
    SHORT X;
    SHORT Y;
} COORD, *PCOORD;

/*
 * Event types
 */
#define KEY_EVENT                       0x0001
#define MOUSE_EVENT                     0x0002
#define WINDOW_BUFFER_SIZE_EVENT        0x0004
#define MENU_EVENT                      0x0008
#define FOCUS_EVENT                     0x0010

/*
 * ControlKeyState flags
 */
#define RIGHT_ALT_PRESSED               0x0001
#define LEFT_ALT_PRESSED                0x0002
#define RIGHT_CTRL_PRESSED              0x0004
#define LEFT_CTRL_PRESSED               0x0008
#define SHIFT_PRESSED                   0x0010
#define NUMLOCK_ON                      0x0020
#define SCROLLLOCK_ON                   0x0040
#define CAPSLOCK_ON                     0x0080
#define ENHANCED_KEY                    0x0100

/*
 * ButtonState flags
 */
#define FROM_LEFT_1ST_BUTTON_PRESSED    0x0001
#define RIGHTMOST_BUTTON_PRESSED        0x0002
#define FROM_LEFT_2ND_BUTTON_PRESSED    0x0004
#define FROM_LEFT_3RD_BUTTON_PRESSED    0x0008
#define FROM_LEFT_4TH_BUTTON_PRESSED    0x0010

/*
 * Mouse event flags
 */
#define MOUSE_MOVED                     0x0001
#define DOUBLE_CLICK                    0x0002
#define MOUSE_WHEELED                   0x0004
#if (_WIN32_WINNT >= _WIN32_WINNT_VISTA)
#define MOUSE_HWHEELED                  0x0008
#endif

typedef struct _CHAR_INFO {
    union {
        WCHAR UnicodeChar;
        CHAR AsciiChar;
    } Char;
    WORD Attributes;
} CHAR_INFO,*PCHAR_INFO;

typedef struct _CONSOLE_FONT_INFO {
    DWORD nFont;
    COORD dwFontSize;
} CONSOLE_FONT_INFO, *PCONSOLE_FONT_INFO;

typedef struct _KEY_EVENT_RECORD {
    BOOL bKeyDown;
    WORD wRepeatCount;
    WORD wVirtualKeyCode;
    WORD wVirtualScanCode;
    union {
        WCHAR UnicodeChar;
        CHAR  AsciiChar;
    } uChar;
    DWORD dwControlKeyState;
} KEY_EVENT_RECORD, *PKEY_EVENT_RECORD;

C_ASSERT(FIELD_OFFSET(KEY_EVENT_RECORD, uChar) == 0xA);

typedef struct _MOUSE_EVENT_RECORD {
    COORD dwMousePosition;
    DWORD dwButtonState;
    DWORD dwControlKeyState;
    DWORD dwEventFlags;
} MOUSE_EVENT_RECORD, *PMOUSE_EVENT_RECORD;

typedef struct _WINDOW_BUFFER_SIZE_RECORD {
    COORD dwSize;
} WINDOW_BUFFER_SIZE_RECORD, *PWINDOW_BUFFER_SIZE_RECORD;

typedef struct _MENU_EVENT_RECORD {
    UINT dwCommandId;
} MENU_EVENT_RECORD, *PMENU_EVENT_RECORD;

typedef struct _FOCUS_EVENT_RECORD {
    BOOL bSetFocus;
} FOCUS_EVENT_RECORD, *PFOCUS_EVENT_RECORD;

typedef struct _INPUT_RECORD {
    WORD EventType;
    union {
        KEY_EVENT_RECORD KeyEvent;
        MOUSE_EVENT_RECORD MouseEvent;
        WINDOW_BUFFER_SIZE_RECORD WindowBufferSizeEvent;
        MENU_EVENT_RECORD MenuEvent;
        FOCUS_EVENT_RECORD FocusEvent;
    } Event;
} INPUT_RECORD, *PINPUT_RECORD;

#if (NTDDI_VERSION >= NTDDI_WIN10_RS5) || defined(_KERNELBASE_)
typedef VOID *HPCON;
#endif

#endif /* _WINCONTYPES_ */
