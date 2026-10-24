/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Starts a Linux program through the lxcore driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>

#include "../include/lxioctl.h"
#include "../include/lxlaunch.h"

static
VOID
LxUsage(VOID)
{
    fwprintf(stderr,
             L"Usage: lxrun [-t] [-n|-j] [--boot] [-r rootdir] [-C linuxdir] [-e NAME=VALUE]... program [arguments...]\n"
             L"  -t             Treat standard input and output as a terminal even when they are not a console\n"
             L"  -n             Start a new Linux instance instead of joining the running one\n"
             L"  -j             Only join the running Linux instance; fail when there is none\n"
             L"  --boot         Boot the root as a Linux instance with program (default /sbin/init) as PID 1\n"
             L"  --trace        Log failing Linux system calls of the instance to the kernel debugger\n"
             L"  --trace-all    Log every Linux system call of the instance to the kernel debugger\n"
             L"  -r rootdir     Windows directory used as the Linux root (default: current directory)\n"
             L"  -C linuxdir    Linux working directory (default: /)\n"
             L"  -e NAME=VALUE  Add or replace an environment variable\n");
}

int
wmain(
    int argc,
    WCHAR *argv[])
{
    static WCHAR FullRoot[MAX_PATH];
    LX_LAUNCH Launch;
    PCHAR Utf8;
    BOOL ForceTty = FALSE;
    int Index;

    ZeroMemory(&Launch, sizeof(Launch));
    Launch.Name = L"lxrun";
    Launch.Root = L".";
    Launch.Cwd = "/";

    LxStringsAdd(&Launch.Env, "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin");
    LxStringsAdd(&Launch.Env, "HOME=/root");
    LxStringsAdd(&Launch.Env, "USER=root");
    LxStringsAdd(&Launch.Env, "SHELL=/bin/sh");
    LxStringsAdd(&Launch.Env, "TERM=dumb");
    LxStringsAdd(&Launch.Env, "LANG=C.UTF-8");

    for (Index = 1; Index < argc; Index++)
    {
        if (wcscmp(argv[Index], L"-t") == 0)
        {
            ForceTty = TRUE;
        }
        else if (wcscmp(argv[Index], L"-n") == 0)
        {
            Launch.Flags |= LX_CREATE_NEW_INSTANCE;
        }
        else if (wcscmp(argv[Index], L"-j") == 0)
        {
            Launch.Flags |= LX_CREATE_JOIN;
        }
        else if (wcscmp(argv[Index], L"--boot") == 0)
        {
            Launch.Flags |= LX_CREATE_BOOT | LX_CREATE_NEW_INSTANCE;
        }
        else if (wcscmp(argv[Index], L"--trace") == 0)
        {
            Launch.Flags |= LX_CREATE_TRACE;
        }
        else if (wcscmp(argv[Index], L"--trace-all") == 0)
        {
            Launch.Flags |= LX_CREATE_TRACE_ALL;
        }
        else if ((wcscmp(argv[Index], L"-r") == 0) && (Index + 1 < argc))
        {
            Launch.Root = argv[++Index];
        }
        else if ((wcscmp(argv[Index], L"-C") == 0) && (Index + 1 < argc))
        {
            Launch.Cwd = LxToUtf8(argv[++Index]);
        }
        else if ((wcscmp(argv[Index], L"-e") == 0) && (Index + 1 < argc))
        {
            Utf8 = LxToUtf8(argv[++Index]);
            if ((Utf8 == NULL) || !LxEnvSet(&Launch.Env, Utf8))
            {
                LxUsage();
                return 1;
            }
            free(Utf8);
        }
        else if ((wcscmp(argv[Index], L"-h") == 0) || (wcscmp(argv[Index], L"/?") == 0))
        {
            LxUsage();
            return 0;
        }
        else
        {
            break;
        }
    }

    if (((Index >= argc) && !(Launch.Flags & LX_CREATE_BOOT)) || (Launch.Cwd == NULL))
    {
        LxUsage();
        return 1;
    }

    if (Launch.Flags & LX_CREATE_BOOT)
    {
        LxEnvSet(&Launch.Env, "container=libernt");
        LxEnvSet(&Launch.Env, "TERM=linux");
    }

    if (Index >= argc)
    {
        Launch.FileName = "/sbin/init";
        if (!LxStringsAdd(&Launch.Args, Launch.FileName))
            return 1;
    }
    else
    {
        Launch.FileName = LxToUtf8(argv[Index]);
        for (; Index < argc; Index++)
        {
            Utf8 = LxToUtf8(argv[Index]);
            if ((Utf8 == NULL) || !LxStringsAdd(&Launch.Args, Utf8))
                return 1;
            free(Utf8);
        }
    }

    if (Launch.Flags & LX_CREATE_BOOT)
    {
        if (!GetFullPathNameW(Launch.Root, MAX_PATH, FullRoot, NULL))
        {
            fwprintf(stderr, L"lxrun: invalid root directory '%s'\n", Launch.Root);
            return 1;
        }
        LxKeepResolvConf(FullRoot);
    }

    Launch.Terminal = ForceTty || (LxIsConsole(STD_INPUT_HANDLE) && LxIsConsole(STD_OUTPUT_HANDLE));
    return LxLaunch(&Launch);
}
