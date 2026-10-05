/*
 * PROJECT:     LiberNT DirectX Graphics Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     PnP dispatch declarations and child PDO device extension
 * COPYRIGHT:   Copyright 2024 LiberNT Team
 *
 * WDDM types shared across dxgkrnl translation units (DXGK_INTERFACE,
 * DXGK_START_INFO, DXGK_CHILD_DESCRIPTOR, etc.) are defined in
 * dxgkrnl_private.h.  This header declares only the child PDO extension
 * structure and the public PnP/Power entry points.
 */

#ifndef _DXGKRNL_PNP_H_
#define _DXGKRNL_PNP_H_

/*
 * DXGK_CHILD_PDO_EXTENSION
 *
 * Device extension for a child PDO created by dxgkrnl (one per child
 * device reported by DxgkDdiQueryChildRelations).  Mirrors the pattern
 * used by videoprt's VIDEO_PORT_CHILD_EXTENSION.
 *
 * The Signature field is the first ULONG; DxgkpMiniportPnpDispatch reads
 * it to distinguish child PDOs from the GPU FDO.
 */
/* EDID extension blocks cached beside the base block.  Displays rarely
 * carry more than a CTA-861 block and a DisplayID or block map. */
#define DXGKP_EDID_MAX_EXTENSIONS   3

#define DXGKP_DESCRIPTOR_SET_SIGNATURE      'SDxD'
#define DXGKP_FREQUENCY_RANGE_SET_SIGNATURE 'SFxD'

typedef struct _DXGKP_MONITOR_DESCRIPTOR_SET
{
    ULONG                       Signature;
    SIZE_T                      NumDescriptors;
    D3DKMDT_MONITOR_DESCRIPTOR  Descriptors[1 + DXGKP_EDID_MAX_EXTENSIONS];
    UCHAR                       Data[1 + DXGKP_EDID_MAX_EXTENSIONS][128];
} DXGKP_MONITOR_DESCRIPTOR_SET, *PDXGKP_MONITOR_DESCRIPTOR_SET;

typedef struct _DXGKP_MONITOR_FREQUENCY_RANGE_SET
{
    ULONG                           Signature;
    SIZE_T                          NumRanges;
    D3DKMDT_MONITOR_FREQUENCY_RANGE Ranges[1];
} DXGKP_MONITOR_FREQUENCY_RANGE_SET, *PDXGKP_MONITOR_FREQUENCY_RANGE_SET;

typedef struct _DXGK_CHILD_PDO_EXTENSION
{
    /* Signature / type tag — must be first field */
    ULONG                   Signature;  /* DXGK_CHILD_PDO_SIGNATURE */

    /* Back-pointer to the parent GPU FDO adapter context */
    PDXGKRNL_ADAPTER        ParentAdapter;

    /* The PDO device object itself */
    PDEVICE_OBJECT          DeviceObject;

    /* Descriptor as returned by DxgkDdiQueryChildRelations */
    DXGK_CHILD_DESCRIPTOR   Descriptor;

    /* Link into ParentAdapter->ChildListHead (guarded by ChildListLock) */
    LIST_ENTRY              ListEntry;

    /* TRUE while this child is still reported by the miniport */
    BOOLEAN                 Present;

    /* Cached connector status (hot-plug aware children only) */
    BOOLEAN                 Connected;

    /* Incremented for every presence, descriptor, or connection change. */
    ULONG64                 StateGeneration;

    /* Adapter start epoch that most recently reported this child. */
    ULONG64                 EnumerationEpoch;

    ULONG64                 ConnectSequence;

    /* Cached EDID blob (if obtained from DxgkDdiQueryDeviceDescriptor) */
    UCHAR                   Edid[128];
    BOOLEAN                 EdidValid;

    /* Device power state last applied to this child (monitor sleep/wake).
     * Written only while handling this PDO's set-power IRPs. */
    DEVICE_POWER_STATE      DevicePowerState;

    /* EDID extension blocks that follow Edid, read with it.  Meaningful
     * only while EdidValid is set. */
    UCHAR                   EdidExtensions[DXGKP_EDID_MAX_EXTENSIONS][128];
    UCHAR                   EdidExtensionCount;

    /* Sets handed to the miniport through DXGK_MONITOR_INTERFACE.  The
     * interface has no call to release them, so -- like the monitor objects
     * Windows keeps -- they belong to the monitor and live with this child.
     * Built from the cached EDID; never by calling back into the miniport. */
    DXGKP_MONITOR_DESCRIPTOR_SET      DescriptorSet;
    DXGKP_MONITOR_FREQUENCY_RANGE_SET FrequencyRangeSet;

} DXGK_CHILD_PDO_EXTENSION, *PDXGK_CHILD_PDO_EXTENSION;

/*
 * DxgkPnpReadEdidExtensions
 *   Reads the extension blocks a cached EDID base block announces (byte 126),
 *   up to DXGKP_EDID_MAX_EXTENSIONS, through DxgkDdiQueryDeviceDescriptor.
 *   PASSIVE_LEVEL, never from inside a miniport callback.  Returns the number
 *   of consecutive blocks read; a block that cannot be read ends the run.
 */
UCHAR
DxgkPnpReadEdidExtensions(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG ChildUid,
    _In_reads_bytes_(128) CONST UCHAR *BaseBlock,
    _Out_writes_(DXGKP_EDID_MAX_EXTENSIONS) UCHAR (*Extensions)[128]);

/* Pool tag for child PDO extensions */
#define TAG_DXGK_CHILD_PDO  'CxgD'

/*
 * Signature stored in DXGK_CHILD_PDO_EXTENSION::Signature.
 * DxgkpMiniportPnpDispatch compares *(PULONG)DevExt against this to
 * determine whether a device object is a child PDO or a GPU FDO.
 * The GPU FDO's first field is a PDXGKRNL_MINIPORT_CONTEXT pointer,
 * which will never equal this constant.
 */
#define DXGK_CHILD_PDO_SIGNATURE  0x43786744UL  /* 'DgxC' */

/* -------------------------------------------------------------------------
 * PnP / Power dispatch entry points
 *
 * DxgkpMiniportPnpDispatch   — installed as IRP_MJ_PNP handler for both
 *                              the GPU FDO and child PDOs created by dxgkrnl.
 * DxgkpMiniportPowerDispatch — installed as IRP_MJ_POWER handler.
 *
 * Both are registered by DxgkInitialize (in adapter.c) after the miniport's
 * own dispatch table has been replaced.
 * ------------------------------------------------------------------------- */

DRIVER_DISPATCH DxgkpMiniportPnpDispatch;
DRIVER_DISPATCH DxgkpMiniportPowerDispatch;

/* -------------------------------------------------------------------------
 * Internal PnP helpers (defined in pnp.c, used within dxgkrnl only)
 * ------------------------------------------------------------------------- */

/*
 * DxgkpQueryBusRelations
 *   Build DEVICE_RELATIONS for BusRelations by calling
 *   DxgkDdiQueryChildRelations on the miniport, creating child PDOs
 *   as needed.
 */
NTSTATUS
DxgkpQueryBusRelations(
    _In_  PDXGKRNL_ADAPTER  Adapter,
    _Out_ PDEVICE_RELATIONS *Relations);

NTSTATUS
DxgkPnpCacheInitialChildRelations(
    _In_ PDXGKRNL_ADAPTER Adapter);

/*
 * DxgkpPollDisplayChildrenRequest
 *   Execute or enqueue the D3DKMT connector-poll contract for one adapter or
 *   every started adapter.  The asynchronous path retains adapter rundown
 *   until its worker has finished querying and publishing child state.
 */
NTSTATUS
DxgkpPollDisplayChildrenRequest(
    _In_ CONST D3DKMT_POLLDISPLAYCHILDREN *PollRequest);

/*
 * DxgkPnpQueuePollDisplayChildren
 *   Queue a non-destructive child-connectivity poll of one adapter from
 *   kernel context, as DXGK_ACPI_POLL_DISPLAY_CHILDREN requests.
 */
NTSTATUS
DxgkPnpQueuePollDisplayChildren(
    _In_ PDXGKRNL_ADAPTER Adapter);

NTSTATUS
DxgkPnpIndicateChildConnection(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG ChildUid,
    _In_ BOOLEAN Connected,
    _Out_ PBOOLEAN Changed);

NTSTATUS
DxgkPnpResolveChildAcpiUid(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG ChildUid,
    _Out_ PULONG AcpiUid);

VOID
DxgkPnpBeginChildEnumerationEpoch(
    _In_ PDXGKRNL_ADAPTER Adapter);

/*
 * DxgkpCreateChildPdo
 *   Allocate and initialise a PDO for one child device.
 */
NTSTATUS
DxgkpCreateChildPdo(
    _In_  PDXGKRNL_ADAPTER          Adapter,
    _In_  PDXGK_CHILD_DESCRIPTOR    Descriptor,
    _In_  BOOLEAN                   ConnectionKnown,
    _In_  BOOLEAN                   Connected,
    _In_  ULONG64                   EnumerationEpoch,
    _Out_ PDXGK_CHILD_PDO_EXTENSION *ChildExtension);

/*
 * DxgkpDeleteChildPdo
 *   Detach and free a previously created child PDO.
 */
VOID
DxgkpDeleteChildPdo(
    _In_ PDXGK_CHILD_PDO_EXTENSION ChildExtension);

/*
 * DxgkpChildPdoPnpDispatch
 *   IRP_MJ_PNP handler for child PDOs.  Called from
 *   DxgkpMiniportPnpDispatch after signature-based dispatch.
 */
NTSTATUS
DxgkpChildPdoPnpDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp);

/*
 * DxgkpBuildDeviceInfo
 *   Populate a DXGK_DEVICE_INFO from the adapter's PCI resources and the
 *   miniport registry path.  Called by DxgkCbGetDeviceInformation (adapter.c).
 */
VOID
DxgkpBuildDeviceInfo(
    _In_  PDXGKRNL_ADAPTER  Adapter,
    _Out_ PDXGK_DEVICE_INFO DeviceInfo);

#endif /* _DXGKRNL_PNP_H_ */
