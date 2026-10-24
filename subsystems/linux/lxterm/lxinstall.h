/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Download and installation of a Linux root file system
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

typedef struct _LX_XZ LX_XZ, *PLX_XZ;
typedef struct _LX_GZ LX_GZ, *PLX_GZ;

PLX_XZ LxXzOpen(_In_ HANDLE File);
LONG LxXzRead(_Inout_ PLX_XZ Xz, _Out_writes_bytes_(Size) PVOID Buffer, _In_ ULONG Size);
ULONG64 LxXzPosition(_In_ PLX_XZ Xz);
VOID LxXzClose(_In_ PLX_XZ Xz);
ULONG64 LxXzContentSize(_In_reads_bytes_(TailLength) const UCHAR *Tail, _In_ ULONG TailLength, _In_ ULONG64 FileSize);

PLX_GZ LxGzOpen(_In_ HANDLE File);
LONG LxGzRead(_Inout_ PLX_GZ Gz, _Out_writes_bytes_(Size) PVOID Buffer, _In_ ULONG Size);
ULONG64 LxGzPosition(_In_ PLX_GZ Gz);
VOID LxGzClose(_In_ PLX_GZ Gz);

BOOL LxRootInstalled(_In_ PCWSTR Root);
BOOL LxAppendRootFile(_In_ PCWSTR Root, _In_ PCWSTR Relative, _In_ PCSTR Text);
int LxInstallRoot(_In_ PCWSTR Root, _In_opt_ PCWSTR Url, _In_ BOOL AssumeYes);
