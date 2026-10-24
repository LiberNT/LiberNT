/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel configuration exported as /proc/config.gz
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"
#include <ndk/rtlfuncs.h>

#define NDEBUG
#include <debug.h>

static const PCSTR LxKernelConfig[] =
{
    "CONFIG_64BIT=y",
    "CONFIG_MMU=y",
    "CONFIG_SMP=y",
    "CONFIG_MULTIUSER=y",
    "CONFIG_SYSVIPC=y",
    "CONFIG_IKCONFIG=y",
    "CONFIG_IKCONFIG_PROC=y",
    "CONFIG_CGROUPS=y",
    "CONFIG_NAMESPACES=y",
    "CONFIG_UTS_NS=y",
    "CONFIG_IPC_NS=y",
    "CONFIG_PID_NS=y",
    "CONFIG_FUTEX=y",
    "CONFIG_EPOLL=y",
    "CONFIG_SIGNALFD=y",
    "CONFIG_TIMERFD=y",
    "CONFIG_EVENTFD=y",
    "CONFIG_SHMEM=y",
    "CONFIG_MEMFD_CREATE=y",
    "CONFIG_MEMBARRIER=y",
    "CONFIG_POSIX_TIMERS=y",
    "CONFIG_PROC_FS=y",
    "CONFIG_PROC_SYSCTL=y",
    "CONFIG_SYSFS=y",
    "CONFIG_TMPFS=y",
    "CONFIG_DEVTMPFS=y",
    "CONFIG_INOTIFY_USER=y",
    "CONFIG_UNIX=y",
    "CONFIG_INET=y",
    "CONFIG_IPV6=y",
};

static
VOID
LxPutUshort(
    _Out_writes_bytes_(2) PUCHAR Out,
    _In_ USHORT Value)
{
    Out[0] = (UCHAR)Value;
    Out[1] = (UCHAR)(Value >> 8);
}

static
VOID
LxPutUlong(
    _Out_writes_bytes_(4) PUCHAR Out,
    _In_ ULONG Value)
{
    Out[0] = (UCHAR)Value;
    Out[1] = (UCHAR)(Value >> 8);
    Out[2] = (UCHAR)(Value >> 16);
    Out[3] = (UCHAR)(Value >> 24);
}

PUCHAR
LxKernelConfigGzip(
    _Out_ PULONG Length)
{
    static const UCHAR Header[10] = { 0x1F, 0x8B, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03 };
    PCSTR Arch = LxArchKernelConfig();
    SIZE_T TextLength = 0, Blocks, Size, Offset, Used = 0;
    PSTR Text;
    PUCHAR Gzip;
    ULONG Index;

    *Length = 0;
    TextLength = strlen("#\n# Automatically generated file; DO NOT EDIT.\n# Linux/") + strlen(LxArchConfigName()) +
                 strlen(" " LX_KERNEL_RELEASE " Kernel Configuration\n#\n") + strlen(Arch);
    for (Index = 0; Index < RTL_NUMBER_OF(LxKernelConfig); Index++)
        TextLength += strlen(LxKernelConfig[Index]) + 1;

    Text = LxAlloc(TextLength + 1);
    if (Text == NULL)
        return NULL;
    if (!NT_SUCCESS(RtlStringCbPrintfA(Text, TextLength + 1,
                                       "#\n# Automatically generated file; DO NOT EDIT.\n# Linux/%s %s Kernel Configuration\n#\n%s",
                                       LxArchConfigName(), LX_KERNEL_RELEASE, Arch)))
    {
        LxFree(Text);
        return NULL;
    }
    Used = strlen(Text);
    for (Index = 0; Index < RTL_NUMBER_OF(LxKernelConfig); Index++)
    {
        SIZE_T Line = strlen(LxKernelConfig[Index]);

        RtlCopyMemory(Text + Used, LxKernelConfig[Index], Line);
        Used += Line;
        Text[Used++] = '\n';
    }
    Text[Used] = '\0';

    Blocks = (Used / 65535) + 1;
    Size = sizeof(Header) + Blocks * 5 + Used + 8;
    Gzip = LxAlloc(Size);
    if (Gzip == NULL)
    {
        LxFree(Text);
        return NULL;
    }
    RtlCopyMemory(Gzip, Header, sizeof(Header));
    Offset = sizeof(Header);
    for (Index = 0; Index < Blocks; Index++)
    {
        SIZE_T Start = (SIZE_T)Index * 65535, Chunk = min(Used - Start, (SIZE_T)65535);

        Gzip[Offset] = (Index + 1 == Blocks) ? 1 : 0;
        LxPutUshort(Gzip + Offset + 1, (USHORT)Chunk);
        LxPutUshort(Gzip + Offset + 3, (USHORT)~Chunk);
        RtlCopyMemory(Gzip + Offset + 5, Text + Start, Chunk);
        Offset += 5 + Chunk;
    }
    LxPutUlong(Gzip + Offset, RtlComputeCrc32(0, (PUCHAR)Text, (ULONG)Used));
    LxPutUlong(Gzip + Offset + 4, (ULONG)Used);
    Offset += 8;
    LxFree(Text);
    *Length = (ULONG)Offset;
    return Gzip;
}
