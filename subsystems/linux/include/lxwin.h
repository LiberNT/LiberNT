/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Messages between the Linux window compositor and the Windows window host
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define LX_WIN_FILE_NAME            L"\\window"
#define LX_USER_WINDOW_NAME         L"\\\\.\\lxcore\\window"

#define LX_WIN_DEVICE_MAJOR         240
#define LX_WIN_DEVICE_MINOR         0

#define LX_WIN_MAX_MESSAGE          (64 * 1024 * 1024)
#define LX_WIN_MAX_EVENT            (1024 * 1024)

#define LX_WIN_CREATE               1
#define LX_WIN_DESTROY              2
#define LX_WIN_TITLE                3
#define LX_WIN_SHOW                 4
#define LX_WIN_GEOMETRY             5
#define LX_WIN_PIXELS               6
#define LX_WIN_MOVE                 7
#define LX_WIN_RESIZE               8
#define LX_WIN_STATE                9
#define LX_WIN_CURSOR               10
#define LX_WIN_SIZES                11
#define LX_WIN_ACTIVATE             12
#define LX_WIN_CLIPBOARD            13
#define LX_WIN_APP_ID               14
#define LX_WIN_ICON                 15

#define LX_WIN_HELLO                100
#define LX_WIN_CONFIGURE            101
#define LX_WIN_CLOSE                102
#define LX_WIN_FOCUS                103
#define LX_WIN_POINTER              104
#define LX_WIN_BUTTON               105
#define LX_WIN_AXIS                 106
#define LX_WIN_KEY                  107
#define LX_WIN_LEAVE                108
#define LX_WIN_CLIPBOARD_SET        109

#define LX_WIN_HOST_ATTACHED        200
#define LX_WIN_HOST_DETACHED        201
#define LX_WIN_COMPOSITOR_ATTACHED  202
#define LX_WIN_COMPOSITOR_DETACHED  203

#define LX_WIN_FLAG_POPUP           0x00000001
#define LX_WIN_FLAG_DECORATED       0x00000002

#define LX_WIN_STATE_NORMAL         0
#define LX_WIN_STATE_MAXIMIZED      1
#define LX_WIN_STATE_MINIMIZED      2
#define LX_WIN_STATE_FULLSCREEN     3

#define LX_WIN_CONFIGURE_MAXIMIZED  0x00000001
#define LX_WIN_CONFIGURE_FULLSCREEN 0x00000002
#define LX_WIN_CONFIGURE_ACTIVATED  0x00000004
#define LX_WIN_CONFIGURE_RESIZING   0x00000008

#define LX_WIN_CURSOR_DEFAULT       0
#define LX_WIN_CURSOR_TEXT          1
#define LX_WIN_CURSOR_POINTER       2
#define LX_WIN_CURSOR_WAIT          3
#define LX_WIN_CURSOR_CROSSHAIR     4
#define LX_WIN_CURSOR_SIZE_NS       5
#define LX_WIN_CURSOR_SIZE_WE       6
#define LX_WIN_CURSOR_SIZE_NWSE     7
#define LX_WIN_CURSOR_SIZE_NESW     8
#define LX_WIN_CURSOR_MOVE          9
#define LX_WIN_CURSOR_HIDDEN        10
#define LX_WIN_CURSOR_NOT_ALLOWED   11

typedef struct _LX_WIN_HEADER
{
    ULONG Type;
    ULONG Size;
    ULONG Window;
    ULONG Reserved;
} LX_WIN_HEADER, *PLX_WIN_HEADER;

typedef struct _LX_WIN_CREATE_MSG
{
    LX_WIN_HEADER Header;
    LONG X;
    LONG Y;
    ULONG Width;
    ULONG Height;
    ULONG Parent;
    ULONG Flags;
} LX_WIN_CREATE_MSG, *PLX_WIN_CREATE_MSG;

typedef struct _LX_WIN_RECT_MSG
{
    LX_WIN_HEADER Header;
    LONG X;
    LONG Y;
    ULONG Width;
    ULONG Height;
} LX_WIN_RECT_MSG, *PLX_WIN_RECT_MSG;

typedef struct _LX_WIN_VALUE_MSG
{
    LX_WIN_HEADER Header;
    ULONG Value;
    ULONG Value2;
} LX_WIN_VALUE_MSG, *PLX_WIN_VALUE_MSG;

typedef struct _LX_WIN_HELLO_MSG
{
    LX_WIN_HEADER Header;
    ULONG ScreenWidth;
    ULONG ScreenHeight;
    ULONG Dpi;
    ULONG RefreshMilliHz;
} LX_WIN_HELLO_MSG, *PLX_WIN_HELLO_MSG;

typedef struct _LX_WIN_CONFIGURE_MSG
{
    LX_WIN_HEADER Header;
    ULONG Width;
    ULONG Height;
    ULONG States;
    ULONG Reserved;
} LX_WIN_CONFIGURE_MSG, *PLX_WIN_CONFIGURE_MSG;

typedef struct _LX_WIN_POINTER_MSG
{
    LX_WIN_HEADER Header;
    LONG X;
    LONG Y;
} LX_WIN_POINTER_MSG, *PLX_WIN_POINTER_MSG;

typedef struct _LX_WIN_ICON_MSG
{
    LX_WIN_HEADER Header;
    ULONG Count;
    ULONG Reserved;
} LX_WIN_ICON_MSG, *PLX_WIN_ICON_MSG;

typedef struct _LX_WIN_ICON_IMAGE
{
    ULONG Width;
    ULONG Height;
} LX_WIN_ICON_IMAGE, *PLX_WIN_ICON_IMAGE;

typedef struct _LX_WIN_SIZES_MSG
{
    LX_WIN_HEADER Header;
    ULONG MinWidth;
    ULONG MinHeight;
    ULONG MaxWidth;
    ULONG MaxHeight;
} LX_WIN_SIZES_MSG, *PLX_WIN_SIZES_MSG;
