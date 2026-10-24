/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Control interface between lxrun and the lxcore driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define LX_DEVICE_NAME        L"\\Device\\lxcore"
#define LX_DOS_DEVICE_NAME    L"\\DosDevices\\lxcore"
#define LX_USER_DEVICE_NAME   L"\\\\.\\lxcore"
#define LX_USER_PTY_NAME      L"\\\\.\\lxcore\\pty"
#define LX_PTY_FILE_NAME      L"\\pty"
#define LX_SERVICE_NAME       L"lxcore"

#define IOCTL_LX_CREATE_PROCESS CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_LX_PTY_RESIZE     CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define LX_CREATE_PROCESS_VERSION 5

#define LX_CREATE_BOOT          0x00000001
#define LX_CREATE_NEW_INSTANCE  0x00000002
#define LX_CREATE_TRACE         0x00000004
#define LX_CREATE_TRACE_ALL     0x00000008
#define LX_CREATE_JOIN          0x00000010
#define LX_CREATE_USER          0x00000020
#define LX_CREATE_SEARCH_PATH   0x00000040
#define LX_CREATE_RUNTIME_DIR   0x00000080

#define LX_CREATE_MAX_GROUPS    32

typedef struct _LX_CREATE_PROCESS_IN
{
    ULONG Size;
    ULONG Version;
    HANDLE StdHandles[3];
    HANDLE Pty;
    ULONG RootOffset;
    ULONG RootLength;
    ULONG CwdOffset;
    ULONG CwdLength;
    ULONG FileOffset;
    ULONG FileLength;
    ULONG ArgOffset;
    ULONG ArgLength;
    ULONG ArgCount;
    ULONG EnvOffset;
    ULONG EnvLength;
    ULONG EnvCount;
    ULONG TtyMask;
    USHORT Rows;
    USHORT Columns;
    ULONG Flags;
    ULONG Reserved;
    ULONG Uid;
    ULONG Gid;
    ULONG GroupCount;
    ULONG Groups[LX_CREATE_MAX_GROUPS];
} LX_CREATE_PROCESS_IN, *PLX_CREATE_PROCESS_IN;

typedef struct _LX_PTY_SIZE
{
    USHORT Rows;
    USHORT Columns;
} LX_PTY_SIZE, *PLX_PTY_SIZE;

typedef struct _LX_CREATE_PROCESS_OUT
{
    HANDLE ProcessHandle;
    ULONG ProcessId;
    ULONG Reserved;
} LX_CREATE_PROCESS_OUT, *PLX_CREATE_PROCESS_OUT;

#define IOCTL_LX_WIN_ATTACH     CTL_CODE(FILE_DEVICE_UNKNOWN, 0x810, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_LX_WIN_RECEIVE    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x811, METHOD_OUT_DIRECT, FILE_ANY_ACCESS)
#define IOCTL_LX_WIN_SEND       CTL_CODE(FILE_DEVICE_UNKNOWN, 0x812, METHOD_BUFFERED, FILE_ANY_ACCESS)
