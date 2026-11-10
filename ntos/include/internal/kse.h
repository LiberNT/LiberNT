/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel Shim Engine internal definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define KSE_HOOK_COLLECTION_END 4
#define KSE_HOOK_END            2

#define KSE_ENGINE_DRIVER_SHIMS_DISABLED  0x00000001
#define KSE_ENGINE_DEVICE_FLAGS_DISABLED  0x00000002

#define KSE_DEVICE_DATA_SKIP_REGISTRY     0x20000000
#define KSE_DEVICE_DATA_FROM_REGISTRY     0x10000000

typedef struct _KSE_HOOK
{
    ULONG Type;
    PVOID Name;
    PVOID HookFunction;
    PVOID OriginalFunction;
} KSE_HOOK, *PKSE_HOOK;

typedef struct _KSE_HOOK_COLLECTION
{
    ULONG Type;
    PCWSTR ExportDriverName;
    PKSE_HOOK HookArray;
} KSE_HOOK_COLLECTION, *PKSE_HOOK_COLLECTION;

typedef struct _KSE_CALLBACK_ROUTINES
{
    PVOID (NTAPI *GetIoCallbacks)(PDRIVER_OBJECT DriverObject);
    NTSTATUS (NTAPI *SetCompletionHook)(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_COMPLETION_ROUTINE CompletionRoutine, PVOID Context);
} KSE_CALLBACK_ROUTINES, *PKSE_CALLBACK_ROUTINES;

typedef struct _KSE_SHIM
{
    SIZE_T Size;
    PGUID ShimGuid;
    PCWSTR ShimName;
    PKSE_CALLBACK_ROUTINES KseCallbackRoutines;
    PVOID ShimmedDriverTargetedNotification;
    PVOID ShimmedDriverUntargetedNotification;
    PKSE_HOOK_COLLECTION HookCollectionsArray;
} KSE_SHIM, *PKSE_SHIM;

C_ASSERT(sizeof(KSE_HOOK) == 4 * sizeof(PVOID));
C_ASSERT(sizeof(KSE_HOOK_COLLECTION) == 3 * sizeof(PVOID));
C_ASSERT(sizeof(KSE_SHIM) == 7 * sizeof(PVOID));

CODE_SEG("INIT")
VOID
NTAPI
KseInitialize(VOID);
