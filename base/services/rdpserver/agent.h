/*
 * PROJECT:     LiberNT RDP console server
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Messages between the service and the agent that shows a session's desktop to it
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#pragma once

#include <windows.h>
#include <drivers/directx/rdpvdd.h>

#define AGENT_PIPE_FORMAT L"\\\\.\\pipe\\LiberNT-RDPServer-%lu-%ls"
#define AGENT_PIPE_KEY_CHARS 32

#define AGENT_ATTACH  1
#define AGENT_DETACH  2
#define AGENT_INPUT   3
#define AGENT_CAPTURE 4
#define AGENT_POINTER 5
#define AGENT_FRAME   6

#define AGENT_KEYBOARD 1
#define AGENT_UNICODE  2
#define AGENT_MOUSE    3

#define AGENT_POINTER_HIDDEN 1
#define AGENT_POINTER_SHAPE  2
#define AGENT_POINTER_CACHED 3

#define AGENT_FRAME_NONE    0
#define AGENT_FRAME_CHANGED 1
#define AGENT_FRAME_GONE    2

typedef struct AGENT_ATTACH_REQUEST
{
    ULONG Code;
    LONG ClientWidth, ClientHeight;
    UINT32 PerformanceFlags, ConnectionType;
    BYTE Sid[SECURITY_MAX_SID_SIZE];
    WCHAR UserName[257];
    WCHAR Domain[256];
    WCHAR Password[256];
} AGENT_ATTACH_REQUEST;

typedef struct AGENT_INPUT_REQUEST
{
    ULONG Code;
    ULONG Kind;
    UINT16 Flags, Value, X, Y;
} AGENT_INPUT_REQUEST;

typedef struct AGENT_CAPTURE_REQUEST
{
    ULONG Code;
    BOOL Frame, ResendPointer;
    LONG RequestedSize;
    UINT32 LargePointerFlag, PointerCacheSize;
} AGENT_CAPTURE_REQUEST;

typedef struct AGENT_POINTER_REPLY
{
    ULONG Code;
    ULONG Kind;
    UINT16 CacheIndex, HotSpotX, HotSpotY, Width, Height;
    UINT32 AndLength, XorLength;
    BYTE Data[RDPVDD_POINTER_MAX * RDPVDD_POINTER_MAX * 4 + RDPVDD_POINTER_MAX * RDPVDD_POINTER_MAX / 8];
} AGENT_POINTER_REPLY;

typedef struct AGENT_FRAME_REPLY
{
    ULONG Code;
    ULONG Status;
    UINT32 Error;
    ULONG ConsoleStatus;
    BOOL TakenOver;
    ULONG Serial;
    ULONG64 Section;
    LONG Width, Height;
    ULONG RectCount;
    RECT Rects[RDPVDD_FRAME_MAX_RECTS];
    ULONG MoveCount;
    RDPVDD_MOVE Moves[RDPVDD_FRAME_MAX_MOVES];
} AGENT_FRAME_REPLY;

typedef void (*AGENT_LOG)(const char *fmt, ...);

int AgentMain(DWORD service, const WCHAR *pipe, const WCHAR *display, AGENT_LOG log);
