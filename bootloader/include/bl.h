/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Boot manager common definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <ntifs.h>
#include <ioaccess.h>
#include <arc/arc.h>
#include <ketypes.h>
#include <mmtypes.h>
#include <ndk/asm.h>
#include <ndk/rtlfuncs.h>
#include <ndk/ldrtypes.h>
#include <ndk/ldrfuncs.h>
#include <ndk/halfuncs.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <ntdddisk.h>
#include <winerror.h>
#include <ntstrsafe.h>
#include <cmlib.h>
#include <debug.h>

#define DBG_DEFAULT_CHANNEL(Channel)
#define ERR(Format, ...)   DbgPrint("err: " Format, ##__VA_ARGS__)
#define WARN(Format, ...)  DbgPrint("warn: " Format, ##__VA_ARGS__)
#define FIXME(Format, ...) DbgPrint("fixme: " Format, ##__VA_ARGS__)
#define TRACE(Format, ...) do { if (0) DbgPrint(Format, ##__VA_ARGS__); } while (0)
#define BugCheck(Format, ...) do { DbgPrint(Format, ##__VA_ARGS__); FwHalt(); } while (0)

#ifndef IMAGE_SUBSYSTEM_WINDOWS_BOOT_APPLICATION
#define IMAGE_SUBSYSTEM_WINDOWS_BOOT_APPLICATION 16
#endif

FORCEINLINE
ULONG
LeUlong(
    _In_reads_bytes_(4) const UCHAR* Data)
{
    return Data[0] | (Data[1] << 8) | (Data[2] << 16) | ((ULONG)Data[3] << 24);
}

FORCEINLINE
ULONGLONG
LeUlonglong(
    _In_reads_bytes_(8) const UCHAR* Data)
{
    return LeUlong(Data) | ((ULONGLONG)LeUlong(Data + 4) << 32);
}

#include <bcd.h>
#include <disk.h>
#include <fw.h>
#include <mm.h>

PVOID
VaToPa(
    PVOID Va);

PVOID
PaToVa(
    PVOID Pa);

BOOLEAN
PeLdrCheckForLoadedDll(
    _Inout_ PLIST_ENTRY ModuleListHead,
    _In_ PCSTR DllName,
    _Out_ PLDR_DATA_TABLE_ENTRY *LoadedEntry);

VOID
PeSetSystemRoot(
    _In_ PCSTR SystemRoot);

PVOID
PeLoadImage(
    _In_ PDK_DEVICE Device,
    _In_ PCSTR Path,
    _In_ TYPE_OF_MEMORY MemoryType);

BOOLEAN
PeLdrAllocateDataTableEntry(
    _Inout_ PLIST_ENTRY ModuleListHead,
    _In_ PCSTR BaseDllName,
    _In_ PCSTR FullDllName,
    _In_ PVOID BaseVA,
    _Out_ PLDR_DATA_TABLE_ENTRY *NewEntry);

VOID
PeInitializeSecurityCookie(
    _In_ PLDR_DATA_TABLE_ENTRY Entry);

BOOLEAN
PeResolveImports(
    _Inout_ PLIST_ENTRY ModuleListHead,
    _In_ PDK_DEVICE Device,
    _In_ PCSTR Directory,
    _In_ PLDR_DATA_TABLE_ENTRY Entry);

BOOLEAN
PeLoadModule(
    _Inout_ PLIST_ENTRY ModuleListHead,
    _In_ PDK_DEVICE Device,
    _In_ PCSTR Directory,
    _In_ PCSTR FileName,
    _In_ PCSTR ModuleName,
    _In_ TYPE_OF_MEMORY MemoryType,
    _In_ ULONG Flags,
    _Out_ PLDR_DATA_TABLE_ENTRY *Entry);

extern LIST_ENTRY FrLdrModuleList;

extern PVOID (*BlHeapAllocator)(SIZE_T Size);

PVOID
BlAllocate(
    _In_ SIZE_T Size);

VOID
BlFree(
    _In_ PVOID Memory);

VOID
UefiSerialDisableFirmware(VOID);

VOID
UiMessageBoxCritical(
    _In_ PCSTR MessageText);

VOID
UiMessageBox(
    _In_ PCSTR Format,
    ...);

#define BM_DESCRIPTION_CHARS  64
#define BM_PATH_CHARS         260
#define BM_LOAD_OPTIONS_CHARS 512

typedef struct _BM_ENTRY
{
    BCD_OBJECT Object;
    WCHAR Id[BCD_ID_CHARS];
    WCHAR Description[BM_DESCRIPTION_CHARS];
} BM_ENTRY, *PBM_ENTRY;

#define BM_FILE_NAME_CHARS 64
#define BM_STARTUP_OPTION  L"STARTUP="

typedef struct _BM_STARTUP
{
    ULONG Profile;
} BM_STARTUP, *PBM_STARTUP;

typedef struct _OSL_PARAMETERS
{
    BCD_DEVICE OsDevice;
    WCHAR SystemRoot[BM_PATH_CHARS];
    WCHAR LoadOptions[BM_LOAD_OPTIONS_CHARS];
    WCHAR KernelFile[BM_FILE_NAME_CHARS];
    WCHAR HalFile[BM_FILE_NAME_CHARS];
    BOOLEAN Verbose;
} OSL_PARAMETERS, *POSL_PARAMETERS;

VOID
BlMain(VOID);

ULONG
BmEnumerateEntries(
    _Out_writes_(MaxEntries) PBM_ENTRY Entries,
    _In_ ULONG MaxEntries,
    _Out_ PULONG DefaultEntry);

BOOLEAN
BmStartApplication(
    _In_ PBM_ENTRY Entry,
    _In_ PCWSTR StorePath,
    _In_ PBM_STARTUP Startup);

BOOLEAN
BmGetDisplayBootMenu(VOID);

BOOLEAN
BmGetTimeout(
    _Out_ PULONGLONG Seconds);

BOOLEAN
BmGetDescription(
    _Out_writes_z_(Chars) PWSTR Description,
    _In_ ULONG Chars);

BOOLEAN
BmChooseStartupSettings(
    _In_ BOOLEAN Graphical,
    _Out_ PBM_STARTUP Startup);

BOOLEAN
OslGetParameters(
    _In_ PBM_ENTRY Entry,
    _Out_ POSL_PARAMETERS Parameters);
