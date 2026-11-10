/*
 * PROJECT:     LiberNT Apple Display Coprocessor Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Display coprocessor (DCP) interface of the Apple silicon display miniport
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <reactos/drivers/applertkit.h>
#include <reactos/drivers/appledart.h>
#include <reactos/drivers/fdtdrv.h>

#define APPLE_DCP_TAG                       'pcDA'
#define APPLE_DCP_WORKING_SURFACE_COUNT     12UL
#define DCP_MAX_SHARED                      24
#define DCP_MAX_TIMING_MODES                32
#define DCP_MAX_COLOR_MODES                 16
#define DCP_DART_PAGE_SIZE                  0x4000

#define DCP_SURFACE_FORMAT_BGRA             1
#define DCP_COLORSPACE_SRGB                 1
#define DCP_EOTF_GAMMA_SDR                  1
#define DCP_ADDRESS_FORMAT_PLANAR           1

#pragma pack(push, 1)
typedef struct _DCP_TIMING_MODE
{
    ULONG Valid;
    ULONG Width;
    ULONG Height;
    USHORT FpsFraction;
    USHORT FpsInteger;
    UCHAR Padding[8];
} DCP_TIMING_MODE, *PDCP_TIMING_MODE;

typedef struct _DCP_COLOR_MODE
{
    ULONG Valid;
    ULONG Colorimetry;
    ULONG Eotf;
    ULONG Encoding;
    ULONG Bpp;
    UCHAR Padding[4];
} DCP_COLOR_MODE, *PDCP_COLOR_MODE;

typedef struct _DCP_PLANE
{
    ULONG Unknown1;
    ULONG64 Address;
    ULONG TileSize;
    ULONG Stride;
    ULONG Unknown2[4];
    ULONG AddressFormat;
    ULONG Unknown3;
} DCP_PLANE, *PDCP_PLANE;

typedef struct _DCP_LAYER
{
    DCP_PLANE Planes[3];
    ULONG Unknown;
    ULONG PlaneCount;
    ULONG Width;
    ULONG Height;
    ULONG SurfaceFormat;
    ULONG Colorspace;
    ULONG Eotf;
    UCHAR Transform;
    UCHAR Padding[3];
} DCP_LAYER, *PDCP_LAYER;
#pragma pack(pop)

C_ASSERT(sizeof(DCP_TIMING_MODE) == 24);
C_ASSERT(sizeof(DCP_COLOR_MODE) == 24);
C_ASSERT(sizeof(DCP_PLANE) == 44);
C_ASSERT(sizeof(DCP_LAYER) == 164);

typedef struct _DCP_SHARED
{
    PVOID Allocation;
    SIZE_T AllocationSize;
    PUCHAR Virtual;
    ULONG64 Physical;
    SIZE_T Size;
    ULONG64 Iova;
} DCP_SHARED, *PDCP_SHARED;

typedef struct _DCP_RING
{
    PUCHAR Header;
    PUCHAR Data;
    ULONG Size;
    BOOLEAN Ready;
} DCP_RING, *PDCP_RING;

typedef struct _APPLE_DCP
{
    FDT_DRIVER_TREE Tree;
    BOOLEAN TreeOpen;
    PUCHAR Coproc;
    ULONG CoprocSize;
    PUCHAR Mailbox;
    ULONG MailboxSize;
    APPLE_DART_ADOPTED DcpDart;
    APPLE_DART_ADOPTED DispDart;
    APPLE_RTKIT Rtkit;
    FAST_MUTEX Lock;
    DCP_SHARED Shared[DCP_MAX_SHARED];
    ULONG SharedCount;
    PDCP_SHARED EpicBuffer;
    PDCP_SHARED CommandTx;
    PDCP_SHARED CommandRx;
    DCP_RING Tx;
    DCP_RING Rx;
    USHORT EpicTag;
    USHORT EpicSequence;
    volatile BOOLEAN EpicStarted;
    volatile BOOLEAN EpicFailed;
    BOOLEAN ServiceFound;
    BOOLEAN CommandBusy;
    ULONG Channel;
    KEVENT Stop;
    PKTHREAD Thread;
} APPLE_DCP, *PAPPLE_DCP;

NTSTATUS
DcpOpen(
    _In_ PDEVICE_OBJECT Pdo,
    _Outptr_ PAPPLE_DCP *Dcp);

VOID
DcpClose(
    _In_ PAPPLE_DCP Dcp);

NTSTATUS
DcpStartDisplayService(
    _Inout_ PAPPLE_DCP Dcp);

NTSTATUS
DcpGetHotPlug(
    _Inout_ PAPPLE_DCP Dcp,
    _Out_ PBOOLEAN Connected,
    _Out_ PULONG TimingModes,
    _Out_ PULONG ColorModes);

NTSTATUS
DcpGetTimingModes(
    _Inout_ PAPPLE_DCP Dcp,
    _Out_writes_to_(Capacity, *Count) PDCP_TIMING_MODE Modes,
    _In_ ULONG Capacity,
    _Out_ PULONG Count);

NTSTATUS
DcpGetColorModes(
    _Inout_ PAPPLE_DCP Dcp,
    _Out_writes_to_(Capacity, *Count) PDCP_COLOR_MODE Modes,
    _In_ ULONG Capacity,
    _Out_ PULONG Count);

NTSTATUS
DcpMapSurface(
    _Inout_ PAPPLE_DCP Dcp,
    _In_ ULONG64 Physical,
    _In_ SIZE_T Size,
    _Out_ PULONG64 Iova);

NTSTATUS
DcpSetSurface(
    _Inout_ PAPPLE_DCP Dcp,
    _In_ const DCP_LAYER *Layer);

NTSTATUS
DcpSwapLayer(
    _Inout_ PAPPLE_DCP Dcp,
    _In_ const DCP_LAYER *Layer);
