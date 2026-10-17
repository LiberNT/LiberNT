/*
 * PROJECT:     LiberNT Disk Management
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H

#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <wincon.h>
#include <winreg.h>
#include <winnls.h>
#include <winerror.h>
#include <winioctl.h>
#include <ntddstor.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <strsafe.h>
#include <diskguid.h>

#define NTOS_MODE_USER
#include <ndk/rtlfuncs.h>

#include <fmifs/fmifs.h>
#include <fslib/volresize.h>
#include <mountmgrutil.h>

#include "resource.h"

#define DU_MAX_DISKS        32
#define DU_MAX_PARTS        64
#define DU_MAX_VOLUMES      64
#define DU_MEGABYTE         (1024ull * 1024ull)

#define WM_DU_WORK_PROGRESS (WM_APP + 1)
#define WM_DU_WORK_OUTPUT   (WM_APP + 2)
#define WM_DU_WORK_DONE     (WM_APP + 3)

typedef enum _DU_CATEGORY
{
    DuCategoryInternal,
    DuCategoryExternal,
    DuCategoryImage,
    DuCategoryCount
} DU_CATEGORY;

typedef enum _DU_PART_KIND
{
    DuPartData,
    DuPartEsp,
    DuPartMsr,
    DuPartRecovery,
    DuPartOther,
    DuPartFree
} DU_PART_KIND;

typedef struct _DU_VOLUME
{
    WCHAR GuidPath[64];
    WCHAR DeviceName[MAX_PATH];
    WCHAR MountPath[MAX_PATH];
    WCHAR Label[MAX_PATH];
    WCHAR FileSystem[32];
    ULONGLONG Capacity;
    ULONGLONG Free;
    DWORD Serial;
    ULONG DiskNumber;
    ULONGLONG Offset;
    BOOL HasFileSystem;
    BOOL Boot;
} DU_VOLUME, *PDU_VOLUME;

typedef struct _DU_PART
{
    ULONGLONG Offset;
    ULONGLONG Length;
    ULONG Number;
    DU_PART_KIND Kind;
    GUID Type;
    GUID Id;
    UCHAR MbrType;
    BOOL Active;
    WCHAR Name[40];
    INT Volume;
} DU_PART, *PDU_PART;

typedef struct _DU_DISK
{
    ULONG Number;
    WCHAR Model[128];
    WCHAR Serial[64];
    STORAGE_BUS_TYPE Bus;
    BOOL Removable;
    DU_CATEGORY Category;
    ULONGLONG Size;
    ULONG BytesPerSector;
    PARTITION_STYLE Style;
    GUID DiskId;
    ULONG Signature;
    ULONGLONG UsableStart;
    ULONGLONG UsableEnd;
    ULONG PartCount;
    DU_PART Parts[DU_MAX_PARTS];
} DU_DISK, *PDU_DISK;

typedef struct _DU_MODEL
{
    ULONG DiskCount;
    DU_DISK Disks[DU_MAX_DISKS];
    ULONG VolumeCount;
    DU_VOLUME Volumes[DU_MAX_VOLUMES];
} DU_MODEL, *PDU_MODEL;

typedef enum _DU_SEL_TYPE
{
    DuSelNone,
    DuSelDisk,
    DuSelPart
} DU_SEL_TYPE;

typedef struct _DU_SELECTION
{
    DU_SEL_TYPE Type;
    ULONG Disk;
    ULONG Part;
} DU_SELECTION;

extern HINSTANCE g_hInstance;
extern DU_MODEL g_Model;

VOID DuModelRefresh(_Out_ PDU_MODEL Model);
PDU_VOLUME DuPartVolume(_In_ PDU_MODEL Model, _In_ const DU_PART *Part);
VOID DuPartTitle(_In_ PDU_MODEL Model, _In_ const DU_PART *Part, _Out_writes_(cch) PWSTR Text, _In_ SIZE_T cch);
VOID DuPartKindName(_In_ const DU_DISK *Disk, _In_ const DU_PART *Part, _Out_writes_(cch) PWSTR Text, _In_ SIZE_T cch);
VOID DuDiskTitle(_In_ const DU_DISK *Disk, _Out_writes_(cch) PWSTR Text, _In_ SIZE_T cch);
VOID DuBusName(_In_ STORAGE_BUS_TYPE Bus, _Out_writes_(cch) PWSTR Text, _In_ SIZE_T cch);
VOID DuFormatSize(_In_ ULONGLONG Bytes, _Out_writes_(cch) PWSTR Text, _In_ SIZE_T cch);
VOID DuLoadString(_In_ UINT Id, _Out_writes_(cch) PWSTR Text, _In_ SIZE_T cch);
VOID DuPrintf(_Out_writes_(cch) PWSTR Text, _In_ SIZE_T cch, _In_ UINT Id, ...);
BOOL DuDiskHasProtectedVolume(_In_ PDU_MODEL Model, _In_ const DU_DISK *Disk);
BOOL DuPartIsProtected(_In_ PDU_MODEL Model, _In_ const DU_DISK *Disk, _In_ const DU_PART *Part);
COLORREF DuPartColor(_In_ const DU_DISK *Disk, _In_ ULONG Index);
INT DuMessage(_In_ HWND hwnd, _In_ PCWSTR Icon, _In_ TASKDIALOG_COMMON_BUTTON_FLAGS Buttons, _In_ PCWSTR Main, _In_opt_ PCWSTR Content);
VOID DuError(_In_ HWND hwnd, _In_ UINT MainId, _In_ DWORD Error);

BOOL DuCanNewVolume(_In_ const DU_SELECTION *Sel);
BOOL DuCanDelete(_In_ const DU_SELECTION *Sel);
BOOL DuCanResize(_In_ const DU_SELECTION *Sel);
BOOL DuCanErase(_In_ const DU_SELECTION *Sel);
BOOL DuCanCheck(_In_ const DU_SELECTION *Sel);
BOOL DuCanMount(_In_ const DU_SELECTION *Sel);
BOOL DuCanInfo(_In_ const DU_SELECTION *Sel);
BOOL DuIsMounted(_In_ const DU_SELECTION *Sel);

BOOL DuNewVolume(_In_ HWND hwnd, _In_ const DU_SELECTION *Sel);
BOOL DuDelete(_In_ HWND hwnd, _In_ const DU_SELECTION *Sel);
BOOL DuResize(_In_ HWND hwnd, _In_ const DU_SELECTION *Sel);
BOOL DuErase(_In_ HWND hwnd, _In_ const DU_SELECTION *Sel);
BOOL DuCheck(_In_ HWND hwnd, _In_ const DU_SELECTION *Sel);
BOOL DuMount(_In_ HWND hwnd, _In_ const DU_SELECTION *Sel);
VOID DuInfo(_In_ HWND hwnd, _In_ const DU_SELECTION *Sel);
