/*
 * PROJECT:     LiberNT DirectX Graphics Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Transaction state for POST display ownership handoff rollback
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#ifndef _DXGK_POSTDISPLAY_CORE_H_
#define _DXGK_POSTDISPLAY_CORE_H_

#include <ntddk.h>
#include <dispmprt.h>

typedef enum _DXGK_POST_DISPLAY_COMPLETION_ACTION
{
    DxgkPostDisplayCompletionNone = 0,
    DxgkPostDisplayCompletionCommit,
    DxgkPostDisplayCompletionRollback,
    DxgkPostDisplayCompletionDefer
} DXGK_POST_DISPLAY_COMPLETION_ACTION;

typedef struct _DXGK_POST_DISPLAY_HANDOFF_CORE
{
    BOOLEAN FallbackStopped;
} DXGK_POST_DISPLAY_HANDOFF_CORE, *PDXGK_POST_DISPLAY_HANDOFF_CORE;

FORCEINLINE
BOOLEAN
DxgkPostDisplayCoreArm(
    _Inout_ PDXGK_POST_DISPLAY_HANDOFF_CORE Core)
{
    if (Core->FallbackStopped)
        return FALSE;
    Core->FallbackStopped = TRUE;
    return TRUE;
}

FORCEINLINE
DXGK_POST_DISPLAY_COMPLETION_ACTION
DxgkPostDisplayCoreComplete(
    _Inout_ PDXGK_POST_DISPLAY_HANDOFF_CORE Core,
    _In_ NTSTATUS StartStatus,
    _In_ BOOLEAN Restartable)
{
    if (!Core->FallbackStopped)
        return DxgkPostDisplayCompletionNone;
    if (!NT_SUCCESS(StartStatus) && !Restartable)
        return DxgkPostDisplayCompletionDefer;
    Core->FallbackStopped = FALSE;
    if (!NT_SUCCESS(StartStatus))
        return DxgkPostDisplayCompletionRollback;
    return DxgkPostDisplayCompletionCommit;
}

FORCEINLINE
BOOLEAN
DxgkPostDisplayCoreFirmwareScanout(
    _In_ BOOLEAN DisplayOnlyDriver,
    _In_ BOOLEAN VidPnCommitted)
{
    return DisplayOnlyDriver || !VidPnCommitted;
}

/* Who may hold the boot display, weakest first: a holder gives way to a stronger claimant. */
typedef enum _DXGK_POST_DISPLAY_ROLE
{
    DxgkPostDisplayRoleNone = 0,
    DxgkPostDisplayRoleFallback,
    DxgkPostDisplayRoleBasicDisplay,
    DxgkPostDisplayRoleMiniport
} DXGK_POST_DISPLAY_ROLE;

typedef enum _DXGK_POST_DISPLAY_CLAIM
{
    DxgkPostDisplayClaimEmpty = 0,
    DxgkPostDisplayClaimTakeOver,
    DxgkPostDisplayClaimFramebuffer
} DXGK_POST_DISPLAY_CLAIM;

FORCEINLINE
DXGK_POST_DISPLAY_ROLE
DxgkPostDisplayCoreRole(
    _In_ BOOLEAN HasMiniport,
    _In_ BOOLEAN BasicDisplayDriver,
    _In_ BOOLEAN RootFallback)
{
    ASSERT(!BasicDisplayDriver || HasMiniport);
    ASSERT(!RootFallback || BasicDisplayDriver);

    if (!HasMiniport)
        return DxgkPostDisplayRoleNone;
    if (!BasicDisplayDriver)
        return DxgkPostDisplayRoleMiniport;
    if (RootFallback)
        return DxgkPostDisplayRoleFallback;
    return DxgkPostDisplayRoleBasicDisplay;
}

FORCEINLINE
BOOLEAN
DxgkPostDisplayCoreOutranks(
    _In_ DXGK_POST_DISPLAY_ROLE Claimant,
    _In_ DXGK_POST_DISPLAY_ROLE Owner)
{
    ASSERT(Claimant <= DxgkPostDisplayRoleMiniport);
    ASSERT(Owner <= DxgkPostDisplayRoleMiniport);
    return Claimant != DxgkPostDisplayRoleNone &&
           Owner != DxgkPostDisplayRoleNone &&
           Claimant > Owner;
}

FORCEINLINE
DXGK_POST_DISPLAY_CLAIM
DxgkPostDisplayCoreClaim(
    _In_ BOOLEAN ClaimantOwnsFramebuffer,
    _In_ DXGK_POST_DISPLAY_ROLE ClaimantRole,
    _In_ BOOLEAN OwnerExists,
    _In_ BOOLEAN OwnerIsClaimant,
    _In_ DXGK_POST_DISPLAY_ROLE OwnerRole)
{
    ASSERT(!OwnerIsClaimant || OwnerExists);
    ASSERT(OwnerExists || OwnerRole == DxgkPostDisplayRoleNone);

    if (ClaimantOwnsFramebuffer)
        return DxgkPostDisplayClaimFramebuffer;
    if (ClaimantRole == DxgkPostDisplayRoleMiniport &&
        OwnerExists &&
        !OwnerIsClaimant &&
        DxgkPostDisplayCoreOutranks(ClaimantRole, OwnerRole))
    {
        return DxgkPostDisplayClaimTakeOver;
    }
    return DxgkPostDisplayClaimEmpty;
}

FORCEINLINE
BOOLEAN
DxgkPostDisplayCoreReportsPostDevice(
    _In_ BOOLEAN AdapterOwnsBootDisplay,
    _In_ DXGK_POST_DISPLAY_ROLE Role)
{
    return AdapterOwnsBootDisplay && Role == DxgkPostDisplayRoleMiniport;
}

/* A CPU host aperture run inside the window the firmware framebuffer is seen through makes that window show other memory. */
FORCEINLINE
BOOLEAN
DxgkPostDisplayCoreApertureRunTakesBootWindow(
    _In_ ULONGLONG ApertureBase,
    _In_ ULONG AperturePageSize,
    _In_ ULONG FirstPage,
    _In_ ULONG PageCount,
    _In_ ULONGLONG BootWindowBase,
    _In_ ULONGLONG BootWindowBytes)
{
    ULONGLONG RunBase;
    ULONGLONG RunBytes;

    if (AperturePageSize == 0 || PageCount == 0 || BootWindowBytes == 0)
        return FALSE;
    RunBytes = (ULONGLONG)PageCount * AperturePageSize;
    ASSERT((ULONGLONG)FirstPage * AperturePageSize <= MAXULONGLONG - ApertureBase);
    RunBase = ApertureBase + (ULONGLONG)FirstPage * AperturePageSize;
    ASSERT(RunBytes <= MAXULONGLONG - RunBase);
    ASSERT(BootWindowBytes <= MAXULONGLONG - BootWindowBase);
    return RunBase < BootWindowBase + BootWindowBytes && BootWindowBase < RunBase + RunBytes;
}

/* The firmware framebuffer ranges name a segment by its zero-based position, not by the segment id the other DDIs use. */
FORCEINLINE
USHORT
DxgkPostDisplayCoreUefiRangeSegment(
    _In_ ULONG SegmentId)
{
    ASSERT(SegmentId != 0);
    ASSERT(SegmentId - 1 <= MAXUSHORT);
    return (USHORT)(SegmentId - 1);
}

FORCEINLINE
BOOLEAN
DxgkPostDisplayCoreUefiRangeSegmentIndex(
    _In_ USHORT DriverSegmentId,
    _In_ ULONG SegmentCount,
    _Out_ PULONG SegmentIndex)
{
    *SegmentIndex = 0;
    if (DriverSegmentId >= SegmentCount)
        return FALSE;
    *SegmentIndex = DriverSegmentId;
    return TRUE;
}

/* The block a DXGK_RFVR_FIRMWARE request carries, zeroed; the driver writes what the firmware drives into it. */
typedef struct _DXGK_POST_DISPLAY_FIRMWARE_DATA
{
    D3DDDI_COLOR_SPACE_TYPE ColorSpace;
    D3DKMDT_WIRE_FORMAT_AND_PREFERENCE WireFormat;
    /* TODO: INFO_NEEDED: meaning of the two flag bytes the driver writes here */
    UCHAR Reserved[4];
} DXGK_POST_DISPLAY_FIRMWARE_DATA, *PDXGK_POST_DISPLAY_FIRMWARE_DATA;

C_ASSERT(sizeof(DXGK_POST_DISPLAY_FIRMWARE_DATA) == 12);

FORCEINLINE
VOID
DxgkPostDisplayCoreFirmwareRequest(
    _Out_ PDXGKARG_RECOMMENDFUNCTIONALVIDPN Args,
    _Out_ PDXGK_POST_DISPLAY_FIRMWARE_DATA Data,
    _In_ D3DKMDT_HVIDPN VidPn)
{
    RtlZeroMemory(Data, sizeof(*Data));
    RtlZeroMemory(Args, sizeof(*Args));
    Args->hRecommendedFunctionalVidPn = VidPn;
    Args->RequestReason = DXGK_RFVR_FIRMWARE;
    Args->pPrivateDriverData = Data;
    Args->PrivateDriverDataSize = sizeof(*Data);
    ASSERT(Args->pPrivateDriverData != NULL);
}

FORCEINLINE
BOOLEAN
DxgkPostDisplayCoreFirmwareTopologyUsable(
    _In_ SIZE_T PathCount)
{
    return PathCount == 1;
}

/* A timing record selects one encoding at one depth; any other answer names no wire format. */
FORCEINLINE
D3DKMDT_WIRE_FORMAT_AND_PREFERENCE
DxgkPostDisplayCoreFirmwareWireFormat(
    _In_ const DXGK_POST_DISPLAY_FIRMWARE_DATA *Data)
{
    D3DKMDT_WIRE_FORMAT_AND_PREFERENCE Format = Data->WireFormat;

    Format.Preference = D3DKMDT_MP_UNINITIALIZED;
    if ((Format.Value & (Format.Value - 1)) != 0)
        Format.Value = 0;
    ASSERT(Format.Preference == D3DKMDT_MP_UNINITIALIZED);
    return Format;
}

FORCEINLINE
BOOLEAN
DxgkPostDisplayCoreFirmwareWireFormatUsable(
    _In_ D3DKMDT_WIRE_FORMAT_AND_PREFERENCE Firmware,
    _In_ D3DKMDT_WIRE_FORMAT_AND_PREFERENCE Supported)
{
    ASSERT(Firmware.Preference == D3DKMDT_MP_UNINITIALIZED);
    Supported.Preference = D3DKMDT_MP_UNINITIALIZED;
    return Firmware.Value != 0 && (Firmware.Value & Supported.Value) == Firmware.Value;
}

FORCEINLINE
BOOLEAN
DxgkPostDisplayCoreUefiUnblockDeferred(
    _In_ KIRQL Irql)
{
    ASSERT(Irql <= DISPATCH_LEVEL);
    return Irql > APC_LEVEL;
}

#endif /* _DXGK_POSTDISPLAY_CORE_H_ */
