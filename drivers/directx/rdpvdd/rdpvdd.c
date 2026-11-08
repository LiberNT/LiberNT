/*
 * PROJECT:     LiberNT Remote Display Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WDDM display-only miniport of the remote display with a system memory scanout
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <ntifs.h>
#include <windef.h>
#include <dispmprt.h>
#include <drivers/directx/rdpvdd.h>

#define NDEBUG
#include <debug.h>

#define RDPVDD_POOL_TAG        'dVdR'
#define RDPVDD_SOURCE_COUNT    1
#define RDPVDD_CHILD_COUNT     1
#define RDPVDD_BYTES_PER_PIXEL 4
#define RDPVDD_DEFAULT_WIDTH   1024
#define RDPVDD_DEFAULT_HEIGHT  768

typedef struct _RDPVDD_ADAPTER
{
    PDEVICE_OBJECT PhysicalDeviceObject;
    DXGKRNL_INTERFACE DxgkInterface;
    DXGK_START_INFO StartInfo;
    DXGK_DEVICE_INFO DeviceInfo;
    DEVICE_POWER_STATE AdapterPowerState;
    DEVICE_POWER_STATE MonitorPowerState;
    BOOLEAN Started;
    BOOLEAN Connected;
    BOOLEAN SourceActive;
    BOOLEAN SourceVisible;
    ULONG Width;
    ULONG Height;
    ULONG Pitch;
    ULONG PreferredWidth;
    ULONG PreferredHeight;
    PUCHAR Scanout;
    SIZE_T ScanoutSize;
    KMUTEX FrameLock;
    PKEVENT FrameEvent;
    ULONG DirtyCount;
    RECT Dirty[RDPVDD_FRAME_MAX_RECTS];
} RDPVDD_ADAPTER, *PRDPVDD_ADAPTER;

static
BOOLEAN
RdpVddIsSupportedSourceMode(
    _In_ PRDPVDD_ADAPTER Adapter,
    _In_ const D3DKMDT_VIDPN_SOURCE_MODE *Mode)
{
    if (Mode->Type != D3DKMDT_RMT_GRAPHICS)
        return FALSE;
    if (Mode->Format.Graphics.PixelFormat != D3DDDIFMT_A8R8G8B8)
        return FALSE;
    if (Mode->Format.Graphics.ColorBasis != D3DKMDT_CB_SRGB &&
        Mode->Format.Graphics.ColorBasis != D3DKMDT_CB_SCRGB &&
        Mode->Format.Graphics.ColorBasis != D3DKMDT_CB_UNINITIALIZED)
        return FALSE;
    if (Mode->Format.Graphics.PixelValueAccessMode != D3DKMDT_PVAM_DIRECT)
        return FALSE;
    UNREFERENCED_PARAMETER(Adapter);
    return Mode->Format.Graphics.PrimSurfSize.cx >= RDPVDD_MIN_WIDTH &&
           Mode->Format.Graphics.PrimSurfSize.cx <= RDPVDD_MAX_WIDTH &&
           Mode->Format.Graphics.PrimSurfSize.cy >= RDPVDD_MIN_HEIGHT &&
           Mode->Format.Graphics.PrimSurfSize.cy <= RDPVDD_MAX_HEIGHT;
}

static
NTSTATUS
RdpVddValidatePath(
    _In_ const D3DKMDT_VIDPN_PRESENT_PATH *Path)
{
    if (Path->VidPnSourceId >= RDPVDD_SOURCE_COUNT)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
    if (Path->VidPnTargetId >= RDPVDD_CHILD_COUNT)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET;
    if (Path->GammaRamp.Type != D3DDDI_GAMMARAMP_DEFAULT)
        return STATUS_GRAPHICS_GAMMA_RAMP_NOT_SUPPORTED;
    if (Path->ContentTransformation.Scaling != D3DKMDT_VPPS_IDENTITY &&
        Path->ContentTransformation.Scaling != D3DKMDT_VPPS_NOTSPECIFIED &&
        Path->ContentTransformation.Scaling != D3DKMDT_VPPS_UNPINNED &&
        Path->ContentTransformation.Scaling != D3DKMDT_VPPS_UNINITIALIZED)
        return STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
    if (Path->ContentTransformation.Rotation != D3DKMDT_VPPR_IDENTITY &&
        Path->ContentTransformation.Rotation != D3DKMDT_VPPR_NOTSPECIFIED &&
        Path->ContentTransformation.Rotation != D3DKMDT_VPPR_UNPINNED &&
        Path->ContentTransformation.Rotation != D3DKMDT_VPPR_UNINITIALIZED)
        return STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
    return STATUS_SUCCESS;
}

static
VOID
RdpVddBlackOut(
    _In_ PRDPVDD_ADAPTER Adapter)
{
    if (Adapter->Scanout != NULL)
        RtlZeroMemory(Adapter->Scanout, Adapter->ScanoutSize);
}

static
VOID
RdpVddLockFrame(
    _In_ PRDPVDD_ADAPTER Adapter)
{
    (VOID)KeWaitForSingleObject(&Adapter->FrameLock, Executive, KernelMode, FALSE, NULL);
}

static
VOID
RdpVddUnlockFrame(
    _In_ PRDPVDD_ADAPTER Adapter)
{
    KeReleaseMutex(&Adapter->FrameLock, FALSE);
}

static
VOID
RdpVddAddDirty(
    _In_ PRDPVDD_ADAPTER Adapter,
    _In_opt_ const RECT *Rect)
{
    RECT Clipped;
    ULONG Index;

    if (Rect == NULL)
    {
        Clipped.left = Clipped.top = 0;
        Clipped.right = (LONG)Adapter->Width;
        Clipped.bottom = (LONG)Adapter->Height;
        Adapter->DirtyCount = 0;
    }
    else
    {
        Clipped.left = max(Rect->left, 0);
        Clipped.top = max(Rect->top, 0);
        Clipped.right = min(Rect->right, (LONG)Adapter->Width);
        Clipped.bottom = min(Rect->bottom, (LONG)Adapter->Height);
    }
    if (Clipped.left >= Clipped.right || Clipped.top >= Clipped.bottom)
        return;

    for (Index = 0; Index < Adapter->DirtyCount; Index++)
    {
        RECT *Dirty = &Adapter->Dirty[Index];

        if (Clipped.left >= Dirty->left && Clipped.top >= Dirty->top &&
            Clipped.right <= Dirty->right && Clipped.bottom <= Dirty->bottom)
            return;
    }
    if (Adapter->DirtyCount == RDPVDD_FRAME_MAX_RECTS)
    {
        RECT *Dirty = &Adapter->Dirty[0];

        for (Index = 1; Index < Adapter->DirtyCount; Index++)
        {
            Dirty->left = min(Dirty->left, Adapter->Dirty[Index].left);
            Dirty->top = min(Dirty->top, Adapter->Dirty[Index].top);
            Dirty->right = max(Dirty->right, Adapter->Dirty[Index].right);
            Dirty->bottom = max(Dirty->bottom, Adapter->Dirty[Index].bottom);
        }
        Adapter->DirtyCount = 1;
        Clipped.left = min(Dirty->left, Clipped.left);
        Clipped.top = min(Dirty->top, Clipped.top);
        Clipped.right = max(Dirty->right, Clipped.right);
        Clipped.bottom = max(Dirty->bottom, Clipped.bottom);
        *Dirty = Clipped;
        return;
    }
    Adapter->Dirty[Adapter->DirtyCount++] = Clipped;
}

static
VOID
RdpVddSignalFrame(
    _In_ PRDPVDD_ADAPTER Adapter)
{
    if (Adapter->FrameEvent != NULL && Adapter->DirtyCount != 0)
        KeSetEvent(Adapter->FrameEvent, IO_NO_INCREMENT, FALSE);
}

static
VOID
RdpVddCloseFrame(
    _In_ PRDPVDD_ADAPTER Adapter)
{
    PKEVENT Event;

    RdpVddLockFrame(Adapter);
    Event = Adapter->FrameEvent;
    Adapter->FrameEvent = NULL;
    Adapter->DirtyCount = 0;
    RdpVddUnlockFrame(Adapter);
    if (Event != NULL)
    {
        KeSetEvent(Event, IO_NO_INCREMENT, FALSE);
        ObDereferenceObject(Event);
    }
}

static
VOID
RdpVddCopyRect(
    _In_ PRDPVDD_ADAPTER Adapter,
    _In_ const UCHAR *Source,
    _In_ LONG SourcePitch,
    _In_ LONG SourceOriginX,
    _In_ LONG SourceOriginY,
    _In_ const RECT *Rect)
{
    LONG Left = max(Rect->left, 0);
    LONG Top = max(Rect->top, 0);
    LONG Right = min(Rect->right, (LONG)Adapter->Width);
    LONG Bottom = min(Rect->bottom, (LONG)Adapter->Height);
    SIZE_T RowBytes;
    LONG Y;

    if (Left >= Right || Top >= Bottom)
        return;

    RowBytes = (SIZE_T)(Right - Left) * RDPVDD_BYTES_PER_PIXEL;
    for (Y = Top; Y < Bottom; Y++)
    {
        RtlCopyMemory(Adapter->Scanout + (SIZE_T)Y * Adapter->Pitch + (SIZE_T)Left * RDPVDD_BYTES_PER_PIXEL,
                      Source + (LONG_PTR)(Y - SourceOriginY) * SourcePitch + (LONG_PTR)(Left - SourceOriginX) * RDPVDD_BYTES_PER_PIXEL,
                      RowBytes);
    }
}

static
NTSTATUS
APIENTRY
RdpVddDdiAddDevice(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_ PVOID *MiniportDeviceContext)
{
    PRDPVDD_ADAPTER Adapter;

    if (PhysicalDeviceObject == NULL || MiniportDeviceContext == NULL)
        return STATUS_INVALID_PARAMETER;
    *MiniportDeviceContext = NULL;

    Adapter = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Adapter), RDPVDD_POOL_TAG);
    if (Adapter == NULL)
        return STATUS_NO_MEMORY;
    RtlZeroMemory(Adapter, sizeof(*Adapter));
    Adapter->PhysicalDeviceObject = PhysicalDeviceObject;
    KeInitializeMutex(&Adapter->FrameLock, 0);
    Adapter->AdapterPowerState = PowerDeviceD0;
    Adapter->MonitorPowerState = PowerDeviceD0;
    *MiniportDeviceContext = Adapter;
    return STATUS_SUCCESS;
}

static
NTSTATUS
APIENTRY
RdpVddDdiStartDevice(
    _In_ PVOID MiniportDeviceContext,
    _In_ PDXGK_START_INFO DxgkStartInfo,
    _In_ PDXGK_INTERFACE DxgkInterface,
    _Out_ PULONG NumberOfVideoPresentSources,
    _Out_ PULONG NumberOfChildren)
{
    PRDPVDD_ADAPTER Adapter = MiniportDeviceContext;
    NTSTATUS Status;

    if (Adapter == NULL || DxgkStartInfo == NULL || DxgkInterface == NULL ||
        NumberOfVideoPresentSources == NULL || NumberOfChildren == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlCopyMemory(&Adapter->StartInfo, DxgkStartInfo, sizeof(Adapter->StartInfo));
    RtlCopyMemory(&Adapter->DxgkInterface, DxgkInterface, sizeof(Adapter->DxgkInterface));

    Status = Adapter->DxgkInterface.DxgkCbGetDeviceInformation(Adapter->DxgkInterface.DeviceHandle,
                                                               &Adapter->DeviceInfo);
    if (!NT_SUCCESS(Status))
        return Status;

    Adapter->Width = Adapter->PreferredWidth = RDPVDD_DEFAULT_WIDTH;
    Adapter->Height = Adapter->PreferredHeight = RDPVDD_DEFAULT_HEIGHT;
    Adapter->Pitch = RDPVDD_DEFAULT_WIDTH * RDPVDD_BYTES_PER_PIXEL;
    Adapter->ScanoutSize = (SIZE_T)Adapter->Pitch * Adapter->Height;
    Adapter->Scanout = ExAllocatePoolWithTag(NonPagedPool, Adapter->ScanoutSize, RDPVDD_POOL_TAG);
    if (Adapter->Scanout == NULL)
        return STATUS_NO_MEMORY;
    RtlZeroMemory(Adapter->Scanout, Adapter->ScanoutSize);

    Adapter->SourceActive = FALSE;
    Adapter->SourceVisible = FALSE;
    Adapter->Connected = TRUE;
    Adapter->Started = TRUE;
    *NumberOfVideoPresentSources = RDPVDD_SOURCE_COUNT;
    *NumberOfChildren = RDPVDD_CHILD_COUNT;
    return STATUS_SUCCESS;
}

static
NTSTATUS
APIENTRY
RdpVddDdiStopDevice(
    _In_ PVOID MiniportDeviceContext)
{
    PRDPVDD_ADAPTER Adapter = MiniportDeviceContext;

    RdpVddCloseFrame(Adapter);
    RdpVddLockFrame(Adapter);
    Adapter->Started = FALSE;
    Adapter->SourceActive = FALSE;
    if (Adapter->Scanout != NULL)
    {
        ExFreePoolWithTag(Adapter->Scanout, RDPVDD_POOL_TAG);
        Adapter->Scanout = NULL;
        Adapter->ScanoutSize = 0;
    }
    RdpVddUnlockFrame(Adapter);
    return STATUS_SUCCESS;
}

static
NTSTATUS
APIENTRY
RdpVddDdiRemoveDevice(
    _In_ PVOID MiniportDeviceContext)
{
    PRDPVDD_ADAPTER Adapter = MiniportDeviceContext;

    if (Adapter != NULL)
    {
        RdpVddCloseFrame(Adapter);
        if (Adapter->Scanout != NULL)
            ExFreePoolWithTag(Adapter->Scanout, RDPVDD_POOL_TAG);
        ExFreePoolWithTag(Adapter, RDPVDD_POOL_TAG);
    }
    return STATUS_SUCCESS;
}

static DXGKDDI_NOTIFY_SURPRISE_REMOVAL RdpVddDdiNotifySurpriseRemoval;

static
NTSTATUS
RdpVddDdiNotifySurpriseRemoval(
    _In_ PVOID MiniportDeviceContext,
    _In_ DXGK_SURPRISE_REMOVAL_TYPE RemovalType)
{
    PRDPVDD_ADAPTER Adapter = MiniportDeviceContext;

    UNREFERENCED_PARAMETER(RemovalType);

    RdpVddCloseFrame(Adapter);
    RdpVddLockFrame(Adapter);
    Adapter->Started = FALSE;
    Adapter->SourceActive = FALSE;
    RdpVddUnlockFrame(Adapter);
    return STATUS_SUCCESS;
}

static
NTSTATUS
APIENTRY
RdpVddDdiDispatchIoRequest(
    _In_ PVOID MiniportDeviceContext,
    _In_ ULONG VidPnSourceId,
    _In_ PVIDEO_REQUEST_PACKET VideoRequestPacket)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(VidPnSourceId);
    UNREFERENCED_PARAMETER(VideoRequestPacket);
    return STATUS_NOT_IMPLEMENTED;
}

static
NTSTATUS
APIENTRY
RdpVddDdiQueryChildRelations(
    _In_ PVOID MiniportDeviceContext,
    _Out_ PDXGK_CHILD_DESCRIPTOR ChildRelations,
    _In_ ULONG ChildRelationsSize)
{
    ULONG Count;
    ULONG Index;

    UNREFERENCED_PARAMETER(MiniportDeviceContext);

    if (ChildRelations == NULL || ChildRelationsSize < sizeof(DXGK_CHILD_DESCRIPTOR))
        return STATUS_BUFFER_TOO_SMALL;

    Count = min(ChildRelationsSize / sizeof(DXGK_CHILD_DESCRIPTOR) - 1, RDPVDD_CHILD_COUNT);
    for (Index = 0; Index < Count; Index++)
    {
        RtlZeroMemory(&ChildRelations[Index], sizeof(ChildRelations[Index]));
        ChildRelations[Index].ChildDeviceType = TypeVideoOutput;
        ChildRelations[Index].ChildCapabilities.HpdAwareness = HpdAwarenessInterruptible;
        ChildRelations[Index].ChildCapabilities.Type.VideoOutput.InterfaceTechnology = D3DKMDT_VOT_OTHER;
        ChildRelations[Index].ChildCapabilities.Type.VideoOutput.MonitorOrientationAwareness = D3DKMDT_MOA_NONE;
        ChildRelations[Index].ChildCapabilities.Type.VideoOutput.SupportsSdtvModes = FALSE;
        ChildRelations[Index].AcpiUid = 0;
        ChildRelations[Index].ChildUid = Index;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
APIENTRY
RdpVddDdiQueryChildStatus(
    _In_ PVOID MiniportDeviceContext,
    _Inout_ PDXGK_CHILD_STATUS ChildStatus,
    _In_ BOOLEAN NonDestructiveOnly)
{
    PRDPVDD_ADAPTER Adapter = MiniportDeviceContext;

    UNREFERENCED_PARAMETER(NonDestructiveOnly);

    if (ChildStatus == NULL || ChildStatus->ChildUid >= RDPVDD_CHILD_COUNT)
        return STATUS_INVALID_PARAMETER;

    switch (ChildStatus->Type)
    {
        case StatusConnection:
            ChildStatus->HotPlug.Connected = Adapter->Started && Adapter->Connected;
            return STATUS_SUCCESS;

        case StatusRotation:
            return STATUS_INVALID_PARAMETER;

        default:
            return STATUS_NOT_SUPPORTED;
    }
}

static
NTSTATUS
APIENTRY
RdpVddDdiQueryDeviceDescriptor(
    _In_ PVOID MiniportDeviceContext,
    _In_ ULONG ChildUid,
    _Inout_ PDXGK_DEVICE_DESCRIPTOR DeviceDescriptor)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(ChildUid);
    UNREFERENCED_PARAMETER(DeviceDescriptor);
    return STATUS_GRAPHICS_CHILD_DESCRIPTOR_NOT_SUPPORTED;
}

static
NTSTATUS
APIENTRY
RdpVddDdiSetPowerState(
    _In_ PVOID MiniportDeviceContext,
    _In_ ULONG DeviceUid,
    _In_ DEVICE_POWER_STATE DevicePowerState,
    _In_ POWER_ACTION ActionType)
{
    PRDPVDD_ADAPTER Adapter = MiniportDeviceContext;

    UNREFERENCED_PARAMETER(ActionType);

    if (DeviceUid == DISPLAY_ADAPTER_HW_ID)
    {
        if (DevicePowerState == PowerDeviceD0 && Adapter->AdapterPowerState == PowerDeviceD3)
        {
            Adapter->SourceVisible = FALSE;
            RdpVddBlackOut(Adapter);
        }
        Adapter->AdapterPowerState = DevicePowerState;
        return STATUS_SUCCESS;
    }

    if (DeviceUid >= RDPVDD_CHILD_COUNT)
        return STATUS_INVALID_PARAMETER;
    Adapter->MonitorPowerState = DevicePowerState;
    return STATUS_SUCCESS;
}

static
VOID
APIENTRY
RdpVddDdiResetDevice(
    _In_ PVOID MiniportDeviceContext)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
}

static
VOID
APIENTRY
RdpVddDdiUnload(VOID)
{
}

static
NTSTATUS
APIENTRY
RdpVddDdiQueryAdapterInfo(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_QUERYADAPTERINFO pQueryAdapterInfo)
{
    UNREFERENCED_PARAMETER(hAdapter);

    switch (pQueryAdapterInfo->Type)
    {
        case DXGKQAITYPE_DRIVERCAPS:
        {
            DXGK_DRIVERCAPS *Caps = pQueryAdapterInfo->pOutputData;

            if (Caps == NULL ||
                pQueryAdapterInfo->OutputDataSize < RTL_SIZEOF_THROUGH_FIELD(DXGK_DRIVERCAPS, SupportSurpriseRemoval))
                return STATUS_BUFFER_TOO_SMALL;
            RtlZeroMemory(Caps, pQueryAdapterInfo->OutputDataSize);
            Caps->WDDMVersion = DXGKDDI_WDDMv2;
            Caps->HighestAcceptableAddress.QuadPart = -1;
            Caps->SupportNonVGA = TRUE;
            Caps->SupportSurpriseRemoval = TRUE;
            return STATUS_SUCCESS;
        }

        case DXGKQAITYPE_DISPLAY_DRIVERCAPS_EXTENSION:
        {
            if (pQueryAdapterInfo->pOutputData == NULL ||
                pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_DISPLAY_DRIVERCAPS_EXTENSION))
                return STATUS_INVALID_PARAMETER;
            RtlZeroMemory(pQueryAdapterInfo->pOutputData, pQueryAdapterInfo->OutputDataSize);
            return STATUS_SUCCESS;
        }

        default:
            return STATUS_NOT_SUPPORTED;
    }
}

static
NTSTATUS
APIENTRY
RdpVddDdiSetPointerPosition(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETPOINTERPOSITION pSetPointerPosition)
{
    UNREFERENCED_PARAMETER(hAdapter);

    if (pSetPointerPosition->VidPnSourceId >= RDPVDD_SOURCE_COUNT)
        return STATUS_INVALID_PARAMETER;
    return pSetPointerPosition->Flags.Visible ? STATUS_UNSUCCESSFUL : STATUS_SUCCESS;
}

static
NTSTATUS
APIENTRY
RdpVddDdiSetPointerShape(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETPOINTERSHAPE pSetPointerShape)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSetPointerShape);
    return STATUS_NOT_IMPLEMENTED;
}

static
NTSTATUS
RdpVddIndicateConnection(
    _In_ PRDPVDD_ADAPTER Adapter,
    _In_ BOOLEAN Connected)
{
    DXGK_CHILD_STATUS ChildStatus;

    Adapter->Connected = Connected;
    RtlZeroMemory(&ChildStatus, sizeof(ChildStatus));
    ChildStatus.Type = StatusConnection;
    ChildStatus.ChildUid = 0;
    ChildStatus.HotPlug.Connected = Connected;
    return Adapter->DxgkInterface.DxgkCbIndicateChildStatus(Adapter->DxgkInterface.DeviceHandle, &ChildStatus);
}

static
NTSTATUS
RdpVddSetMode(
    _In_ PRDPVDD_ADAPTER Adapter,
    _In_ const RDPVDD_ESCAPE *Request)
{
    NTSTATUS Status;

    if (Request->Width < RDPVDD_MIN_WIDTH || Request->Width > RDPVDD_MAX_WIDTH ||
        Request->Height < RDPVDD_MIN_HEIGHT || Request->Height > RDPVDD_MAX_HEIGHT)
        return STATUS_INVALID_PARAMETER;
    if (!Adapter->Started)
        return STATUS_DEVICE_NOT_READY;
    if (Request->Width == Adapter->PreferredWidth && Request->Height == Adapter->PreferredHeight)
        return STATUS_SUCCESS;

    Status = RdpVddIndicateConnection(Adapter, FALSE);
    if (!NT_SUCCESS(Status))
    {
        Adapter->Connected = TRUE;
        return Status;
    }
    Adapter->PreferredWidth = Request->Width;
    Adapter->PreferredHeight = Request->Height;
    return RdpVddIndicateConnection(Adapter, TRUE);
}

static
BOOLEAN
RdpVddCanReadFrame(VOID)
{
    SECURITY_SUBJECT_CONTEXT Subject;
    BOOLEAN Allowed;

    if (ExGetPreviousMode() != UserMode)
        return FALSE;
    if (SeSinglePrivilegeCheck(RtlConvertLongToLuid(SE_TCB_PRIVILEGE), UserMode))
        return TRUE;
    SeCaptureSubjectContext(&Subject);
    Allowed = SeTokenIsAdmin(SeQuerySubjectContextToken(&Subject));
    SeReleaseSubjectContext(&Subject);
    return Allowed;
}

static
NTSTATUS
RdpVddOpenFrame(
    _In_ PRDPVDD_ADAPTER Adapter,
    _In_ const RDPVDD_FRAME_ESCAPE *Request)
{
    PKEVENT Event, OldEvent;
    NTSTATUS Status;

    Status = ObReferenceObjectByHandle((HANDLE)(ULONG_PTR)Request->Event, EVENT_MODIFY_STATE, *ExEventObjectType,
                                       UserMode, (PVOID *)&Event, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    RdpVddLockFrame(Adapter);
    OldEvent = Adapter->FrameEvent;
    Adapter->FrameEvent = Event;
    RdpVddAddDirty(Adapter, NULL);
    RdpVddSignalFrame(Adapter);
    RdpVddUnlockFrame(Adapter);
    if (OldEvent != NULL)
        ObDereferenceObject(OldEvent);
    return STATUS_SUCCESS;
}

static
NTSTATUS
RdpVddReadFrame(
    _In_ PRDPVDD_ADAPTER Adapter,
    _Inout_ PRDPVDD_FRAME_ESCAPE Request)
{
    PUCHAR Buffer = (PUCHAR)(ULONG_PTR)Request->Buffer;
    SIZE_T Pitch, Size;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Index;
    LONG Y;

    if ((ULONG64)(ULONG_PTR)Buffer != Request->Buffer)
        return STATUS_INVALID_PARAMETER;

    RdpVddLockFrame(Adapter);
    Request->RectCount = 0;
    if (!Adapter->SourceActive || Adapter->Scanout == NULL)
    {
        Request->Width = Request->Height = 0;
        RdpVddUnlockFrame(Adapter);
        return STATUS_SUCCESS;
    }
    Request->Width = Adapter->Width;
    Request->Height = Adapter->Height;
    if (Buffer == NULL || Request->BufferWidth != Adapter->Width || Request->BufferHeight != Adapter->Height ||
        Adapter->DirtyCount == 0)
    {
        RdpVddUnlockFrame(Adapter);
        return STATUS_SUCCESS;
    }

    Pitch = (SIZE_T)Adapter->Width * RDPVDD_BYTES_PER_PIXEL;
    Size = Pitch * Adapter->Height;
    _SEH2_TRY
    {
        ProbeForWrite(Buffer, Size, sizeof(ULONG));
        for (Index = 0; Index < Adapter->DirtyCount; Index++)
        {
            const RECT *Rect = &Adapter->Dirty[Index];

            for (Y = Rect->top; Y < Rect->bottom; Y++)
            {
                RtlCopyMemory(Buffer + (SIZE_T)Y * Pitch + (SIZE_T)Rect->left * RDPVDD_BYTES_PER_PIXEL,
                              Adapter->Scanout + (SIZE_T)Y * Adapter->Pitch + (SIZE_T)Rect->left * RDPVDD_BYTES_PER_PIXEL,
                              (SIZE_T)(Rect->right - Rect->left) * RDPVDD_BYTES_PER_PIXEL);
            }
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    if (NT_SUCCESS(Status))
    {
        Request->RectCount = Adapter->DirtyCount;
        RtlCopyMemory(Request->Rects, Adapter->Dirty, Adapter->DirtyCount * sizeof(Adapter->Dirty[0]));
        Adapter->DirtyCount = 0;
    }
    RdpVddUnlockFrame(Adapter);
    return Status;
}

static
NTSTATUS
APIENTRY
RdpVddDdiEscape(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_ESCAPE pEscape)
{
    PRDPVDD_ADAPTER Adapter = (PRDPVDD_ADAPTER)hAdapter;
    RDPVDD_ESCAPE Request;
    PRDPVDD_FRAME_ESCAPE Frame;
    ULONG Code;

    if (pEscape->pPrivateDriverData == NULL || pEscape->PrivateDriverDataSize < sizeof(Code))
        return STATUS_INVALID_PARAMETER;
    Code = *(const ULONG *)pEscape->pPrivateDriverData;
    if (Code == RDPVDD_ESCAPE_SET_MODE)
    {
        if (pEscape->PrivateDriverDataSize != sizeof(Request))
            return STATUS_INVALID_PARAMETER;
        RtlCopyMemory(&Request, pEscape->pPrivateDriverData, sizeof(Request));
        return RdpVddSetMode(Adapter, &Request);
    }
    if (Code != RDPVDD_ESCAPE_OPEN_FRAME && Code != RDPVDD_ESCAPE_READ_FRAME)
        return STATUS_NOT_SUPPORTED;
    if (pEscape->PrivateDriverDataSize != sizeof(*Frame))
        return STATUS_INVALID_PARAMETER;
    if (!RdpVddCanReadFrame())
        return STATUS_ACCESS_DENIED;
    if (!Adapter->Started)
        return STATUS_DEVICE_NOT_READY;
    Frame = pEscape->pPrivateDriverData;
    return Code == RDPVDD_ESCAPE_OPEN_FRAME ? RdpVddOpenFrame(Adapter, Frame) : RdpVddReadFrame(Adapter, Frame);
}

static
NTSTATUS
RdpVddIsPinnedSourceModeSupported(
    _In_ PRDPVDD_ADAPTER Adapter,
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ const DXGK_VIDPN_INTERFACE *VidPnInterface,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId,
    _Out_ PBOOLEAN Supported)
{
    D3DKMDT_HVIDPNSOURCEMODESET hSourceModeSet;
    const DXGK_VIDPNSOURCEMODESET_INTERFACE *SourceModeSetInterface;
    const D3DKMDT_VIDPN_SOURCE_MODE *PinnedMode = NULL;
    NTSTATUS Status;

    *Supported = TRUE;
    Status = VidPnInterface->pfnAcquireSourceModeSet(hVidPn, SourceId, &hSourceModeSet, &SourceModeSetInterface);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = SourceModeSetInterface->pfnAcquirePinnedModeInfo(hSourceModeSet, &PinnedMode);
    if (NT_SUCCESS(Status) && PinnedMode != NULL)
    {
        *Supported = RdpVddIsSupportedSourceMode(Adapter, PinnedMode);
        (VOID)SourceModeSetInterface->pfnReleaseModeInfo(hSourceModeSet, PinnedMode);
    }
    (VOID)VidPnInterface->pfnReleaseSourceModeSet(hVidPn, hSourceModeSet);
    return NT_SUCCESS(Status) ? STATUS_SUCCESS : Status;
}

static
NTSTATUS
APIENTRY
RdpVddDdiIsSupportedVidPn(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_ISSUPPORTEDVIDPN pIsSupportedVidPn)
{
    PRDPVDD_ADAPTER Adapter = (PRDPVDD_ADAPTER)hAdapter;
    const DXGK_VIDPN_INTERFACE *VidPnInterface;
    const DXGK_VIDPNTOPOLOGY_INTERFACE *TopologyInterface;
    D3DKMDT_HVIDPNTOPOLOGY hTopology;
    const D3DKMDT_VIDPN_PRESENT_PATH *Path;
    const D3DKMDT_VIDPN_PRESENT_PATH *NextPath;
    SIZE_T NumPaths;
    BOOLEAN Supported;
    NTSTATUS Status;

    if (pIsSupportedVidPn->hDesiredVidPn == 0)
    {
        pIsSupportedVidPn->IsVidPnSupported = TRUE;
        return STATUS_SUCCESS;
    }
    pIsSupportedVidPn->IsVidPnSupported = FALSE;

    Status = Adapter->DxgkInterface.DxgkCbQueryVidPnInterface(pIsSupportedVidPn->hDesiredVidPn,
                                                              DXGK_VIDPN_INTERFACE_VERSION_V1,
                                                              &VidPnInterface);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = VidPnInterface->pfnGetTopology(pIsSupportedVidPn->hDesiredVidPn, &hTopology, &TopologyInterface);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = TopologyInterface->pfnGetNumPaths(hTopology, &NumPaths);
    if (!NT_SUCCESS(Status))
        return Status;
    if (NumPaths == 0)
    {
        pIsSupportedVidPn->IsVidPnSupported = TRUE;
        return STATUS_SUCCESS;
    }
    if (NumPaths > RDPVDD_CHILD_COUNT)
        return STATUS_SUCCESS;

    Status = TopologyInterface->pfnAcquireFirstPathInfo(hTopology, &Path);
    if (!NT_SUCCESS(Status))
        return Status;
    while (Status == STATUS_SUCCESS)
    {
        if (!NT_SUCCESS(RdpVddValidatePath(Path)))
        {
            (VOID)TopologyInterface->pfnReleasePathInfo(hTopology, Path);
            return STATUS_SUCCESS;
        }
        Status = TopologyInterface->pfnAcquireNextPathInfo(hTopology, Path, &NextPath);
        (VOID)TopologyInterface->pfnReleasePathInfo(hTopology, Path);
        Path = NextPath;
    }
    if (Status != STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET)
        return Status;

    Status = RdpVddIsPinnedSourceModeSupported(Adapter, pIsSupportedVidPn->hDesiredVidPn, VidPnInterface, 0, &Supported);
    if (!NT_SUCCESS(Status))
        return Status;
    pIsSupportedVidPn->IsVidPnSupported = Supported;
    return STATUS_SUCCESS;
}

static
NTSTATUS
APIENTRY
RdpVddDdiRecommendFunctionalVidPn(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_RECOMMENDFUNCTIONALVIDPN_CONST pRecommendFunctionalVidPn)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pRecommendFunctionalVidPn);
    return STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN;
}

static
NTSTATUS
RdpVddAddSourceModes(
    _In_ PRDPVDD_ADAPTER Adapter,
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ const DXGK_VIDPN_INTERFACE *VidPnInterface,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId)
{
    D3DKMDT_HVIDPNSOURCEMODESET hSourceModeSet;
    const DXGK_VIDPNSOURCEMODESET_INTERFACE *SourceModeSetInterface;
    D3DKMDT_VIDPN_SOURCE_MODE *Mode;
    NTSTATUS Status;

    Status = VidPnInterface->pfnCreateNewSourceModeSet(hVidPn, SourceId, &hSourceModeSet, &SourceModeSetInterface);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = SourceModeSetInterface->pfnCreateNewModeInfo(hSourceModeSet, &Mode);
    if (NT_SUCCESS(Status))
    {
        Mode->Type = D3DKMDT_RMT_GRAPHICS;
        Mode->Format.Graphics.PrimSurfSize.cx = Adapter->PreferredWidth;
        Mode->Format.Graphics.PrimSurfSize.cy = Adapter->PreferredHeight;
        Mode->Format.Graphics.VisibleRegionSize = Mode->Format.Graphics.PrimSurfSize;
        Mode->Format.Graphics.Stride = Adapter->PreferredWidth * RDPVDD_BYTES_PER_PIXEL;
        Mode->Format.Graphics.PixelFormat = D3DDDIFMT_A8R8G8B8;
        Mode->Format.Graphics.ColorBasis = D3DKMDT_CB_SCRGB;
        Mode->Format.Graphics.PixelValueAccessMode = D3DKMDT_PVAM_DIRECT;
        Status = SourceModeSetInterface->pfnAddMode(hSourceModeSet, Mode);
        if (!NT_SUCCESS(Status))
            (VOID)SourceModeSetInterface->pfnReleaseModeInfo(hSourceModeSet, Mode);
        if (Status == STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET)
            Status = STATUS_SUCCESS;
    }

    if (NT_SUCCESS(Status))
        Status = VidPnInterface->pfnAssignSourceModeSet(hVidPn, SourceId, hSourceModeSet);
    if (!NT_SUCCESS(Status))
        (VOID)VidPnInterface->pfnReleaseSourceModeSet(hVidPn, hSourceModeSet);
    return Status;
}

static
NTSTATUS
RdpVddAddTargetModes(
    _In_ PRDPVDD_ADAPTER Adapter,
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ const DXGK_VIDPN_INTERFACE *VidPnInterface,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId)
{
    D3DKMDT_HVIDPNTARGETMODESET hTargetModeSet;
    const DXGK_VIDPNTARGETMODESET_INTERFACE *TargetModeSetInterface;
    D3DKMDT_VIDPN_TARGET_MODE *Mode;
    NTSTATUS Status;

    Status = VidPnInterface->pfnCreateNewTargetModeSet(hVidPn, TargetId, &hTargetModeSet, &TargetModeSetInterface);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = TargetModeSetInterface->pfnCreateNewModeInfo(hTargetModeSet, &Mode);
    if (NT_SUCCESS(Status))
    {
        Mode->VideoSignalInfo.VideoStandard = D3DKMDT_VSS_OTHER;
        Mode->VideoSignalInfo.TotalSize.cx = Adapter->PreferredWidth;
        Mode->VideoSignalInfo.TotalSize.cy = Adapter->PreferredHeight;
        Mode->VideoSignalInfo.ActiveSize = Mode->VideoSignalInfo.TotalSize;
        Mode->VideoSignalInfo.VSyncFreq.Numerator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
        Mode->VideoSignalInfo.VSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
        Mode->VideoSignalInfo.HSyncFreq.Numerator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
        Mode->VideoSignalInfo.HSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
        Mode->VideoSignalInfo.PixelRate = D3DKMDT_FREQUENCY_NOTSPECIFIED;
        Mode->VideoSignalInfo.ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;
        Mode->Preference = D3DKMDT_MP_PREFERRED;
        Status = TargetModeSetInterface->pfnAddMode(hTargetModeSet, Mode);
        if (!NT_SUCCESS(Status))
            (VOID)TargetModeSetInterface->pfnReleaseModeInfo(hTargetModeSet, Mode);
        if (Status == STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET)
            Status = STATUS_SUCCESS;
    }

    if (NT_SUCCESS(Status))
        Status = VidPnInterface->pfnAssignTargetModeSet(hVidPn, TargetId, hTargetModeSet);
    if (!NT_SUCCESS(Status))
        (VOID)VidPnInterface->pfnReleaseTargetModeSet(hVidPn, hTargetModeSet);
    return Status;
}

static
NTSTATUS
RdpVddEnumPathCofuncModality(
    _In_ PRDPVDD_ADAPTER Adapter,
    _In_ const DXGKARG_ENUMVIDPNCOFUNCMODALITY *Enum,
    _In_ const DXGK_VIDPN_INTERFACE *VidPnInterface,
    _In_ D3DKMDT_HVIDPNTOPOLOGY hTopology,
    _In_ const DXGK_VIDPNTOPOLOGY_INTERFACE *TopologyInterface,
    _In_ const D3DKMDT_VIDPN_PRESENT_PATH *Path)
{
    D3DKMDT_HVIDPNSOURCEMODESET hSourceModeSet;
    const DXGK_VIDPNSOURCEMODESET_INTERFACE *SourceModeSetInterface;
    const D3DKMDT_VIDPN_SOURCE_MODE *PinnedSourceMode = NULL;
    D3DKMDT_HVIDPNTARGETMODESET hTargetModeSet;
    const DXGK_VIDPNTARGETMODESET_INTERFACE *TargetModeSetInterface;
    const D3DKMDT_VIDPN_TARGET_MODE *PinnedTargetMode = NULL;
    D3DKMDT_VIDPN_PRESENT_PATH LocalPath;
    BOOLEAN UpdateSupport = FALSE;
    NTSTATUS Status;

    if (!(Enum->EnumPivotType == D3DKMDT_EPT_VIDPNSOURCE &&
          Enum->EnumPivot.VidPnSourceId == Path->VidPnSourceId))
    {
        Status = VidPnInterface->pfnAcquireSourceModeSet(Enum->hConstrainingVidPn, Path->VidPnSourceId,
                                                         &hSourceModeSet, &SourceModeSetInterface);
        if (!NT_SUCCESS(Status))
            return Status;
        Status = SourceModeSetInterface->pfnAcquirePinnedModeInfo(hSourceModeSet, &PinnedSourceMode);
        if (NT_SUCCESS(Status) && PinnedSourceMode != NULL)
            (VOID)SourceModeSetInterface->pfnReleaseModeInfo(hSourceModeSet, PinnedSourceMode);
        (VOID)VidPnInterface->pfnReleaseSourceModeSet(Enum->hConstrainingVidPn, hSourceModeSet);
        if (!NT_SUCCESS(Status))
            return Status;
        if (PinnedSourceMode == NULL)
        {
            Status = RdpVddAddSourceModes(Adapter, Enum->hConstrainingVidPn, VidPnInterface, Path->VidPnSourceId);
            if (!NT_SUCCESS(Status))
                return Status;
        }
    }

    if (!(Enum->EnumPivotType == D3DKMDT_EPT_VIDPNTARGET &&
          Enum->EnumPivot.VidPnTargetId == Path->VidPnTargetId))
    {
        Status = VidPnInterface->pfnAcquireTargetModeSet(Enum->hConstrainingVidPn, Path->VidPnTargetId,
                                                         &hTargetModeSet, &TargetModeSetInterface);
        if (!NT_SUCCESS(Status))
            return Status;
        Status = TargetModeSetInterface->pfnAcquirePinnedModeInfo(hTargetModeSet, &PinnedTargetMode);
        if (NT_SUCCESS(Status) && PinnedTargetMode != NULL)
            (VOID)TargetModeSetInterface->pfnReleaseModeInfo(hTargetModeSet, PinnedTargetMode);
        (VOID)VidPnInterface->pfnReleaseTargetModeSet(Enum->hConstrainingVidPn, hTargetModeSet);
        if (!NT_SUCCESS(Status))
            return Status;
        if (PinnedTargetMode == NULL)
        {
            Status = RdpVddAddTargetModes(Adapter, Enum->hConstrainingVidPn, VidPnInterface, Path->VidPnTargetId);
            if (!NT_SUCCESS(Status))
                return Status;
        }
    }

    LocalPath = *Path;
    if (!(Enum->EnumPivotType == D3DKMDT_EPT_SCALING &&
          Enum->EnumPivot.VidPnSourceId == Path->VidPnSourceId &&
          Enum->EnumPivot.VidPnTargetId == Path->VidPnTargetId) &&
        Path->ContentTransformation.Scaling == D3DKMDT_VPPS_UNPINNED)
    {
        RtlZeroMemory(&LocalPath.ContentTransformation.ScalingSupport, sizeof(LocalPath.ContentTransformation.ScalingSupport));
        LocalPath.ContentTransformation.ScalingSupport.Identity = 1;
        UpdateSupport = TRUE;
    }
    if (!(Enum->EnumPivotType == D3DKMDT_EPT_ROTATION &&
          Enum->EnumPivot.VidPnSourceId == Path->VidPnSourceId &&
          Enum->EnumPivot.VidPnTargetId == Path->VidPnTargetId) &&
        Path->ContentTransformation.Rotation == D3DKMDT_VPPR_UNPINNED)
    {
        RtlZeroMemory(&LocalPath.ContentTransformation.RotationSupport, sizeof(LocalPath.ContentTransformation.RotationSupport));
        LocalPath.ContentTransformation.RotationSupport.Identity = 1;
        LocalPath.ContentTransformation.RotationSupport.Offset0 = 1;
        UpdateSupport = TRUE;
    }
    if (UpdateSupport)
        return TopologyInterface->pfnUpdatePathSupportInfo(hTopology, &LocalPath);
    return STATUS_SUCCESS;
}

static
NTSTATUS
APIENTRY
RdpVddDdiEnumVidPnCofuncModality(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_ENUMVIDPNCOFUNCMODALITY_CONST pEnumCofuncModality)
{
    PRDPVDD_ADAPTER Adapter = (PRDPVDD_ADAPTER)hAdapter;
    const DXGK_VIDPN_INTERFACE *VidPnInterface;
    const DXGK_VIDPNTOPOLOGY_INTERFACE *TopologyInterface;
    D3DKMDT_HVIDPNTOPOLOGY hTopology;
    const D3DKMDT_VIDPN_PRESENT_PATH *Path;
    const D3DKMDT_VIDPN_PRESENT_PATH *NextPath;
    NTSTATUS Status;

    Status = Adapter->DxgkInterface.DxgkCbQueryVidPnInterface(pEnumCofuncModality->hConstrainingVidPn,
                                                              DXGK_VIDPN_INTERFACE_VERSION_V1,
                                                              &VidPnInterface);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = VidPnInterface->pfnGetTopology(pEnumCofuncModality->hConstrainingVidPn, &hTopology, &TopologyInterface);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = TopologyInterface->pfnAcquireFirstPathInfo(hTopology, &Path);
    if (Status == STATUS_GRAPHICS_DATASET_IS_EMPTY)
        return STATUS_SUCCESS;
    if (!NT_SUCCESS(Status))
        return Status;

    while (Status == STATUS_SUCCESS)
    {
        NTSTATUS PathStatus = RdpVddEnumPathCofuncModality(Adapter, pEnumCofuncModality, VidPnInterface,
                                                           hTopology, TopologyInterface, Path);
        if (!NT_SUCCESS(PathStatus))
        {
            (VOID)TopologyInterface->pfnReleasePathInfo(hTopology, Path);
            return PathStatus;
        }
        Status = TopologyInterface->pfnAcquireNextPathInfo(hTopology, Path, &NextPath);
        (VOID)TopologyInterface->pfnReleasePathInfo(hTopology, Path);
        Path = NextPath;
    }
    return Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET ? STATUS_SUCCESS : Status;
}

static
NTSTATUS
APIENTRY
RdpVddDdiSetVidPnSourceVisibility(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETVIDPNSOURCEVISIBILITY pSetVidPnSourceVisibility)
{
    PRDPVDD_ADAPTER Adapter = (PRDPVDD_ADAPTER)hAdapter;

    if (pSetVidPnSourceVisibility->VidPnSourceId >= RDPVDD_SOURCE_COUNT &&
        pSetVidPnSourceVisibility->VidPnSourceId != D3DDDI_ID_ALL)
        return STATUS_INVALID_PARAMETER;

    RdpVddLockFrame(Adapter);
    if (!pSetVidPnSourceVisibility->Visible)
    {
        RdpVddBlackOut(Adapter);
        RdpVddAddDirty(Adapter, NULL);
        RdpVddSignalFrame(Adapter);
    }
    Adapter->SourceVisible = pSetVidPnSourceVisibility->Visible;
    RdpVddUnlockFrame(Adapter);
    return STATUS_SUCCESS;
}

static
NTSTATUS
RdpVddCommitPaths(
    _In_ PRDPVDD_ADAPTER Adapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId,
    _In_ D3DKMDT_HVIDPNTOPOLOGY hTopology,
    _In_ const DXGK_VIDPNTOPOLOGY_INTERFACE *TopologyInterface)
{
    const D3DKMDT_VIDPN_PRESENT_PATH *Path;
    D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId;
    SIZE_T NumPaths;
    SIZE_T Index;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Adapter);

    Status = TopologyInterface->pfnGetNumPathsFromSource(hTopology, SourceId, &NumPaths);
    if (!NT_SUCCESS(Status))
        return Status;
    for (Index = 0; Index < NumPaths; Index++)
    {
        Status = TopologyInterface->pfnEnumPathTargetsFromSource(hTopology, SourceId, Index, &TargetId);
        if (!NT_SUCCESS(Status))
            return Status;
        Status = TopologyInterface->pfnAcquirePathInfo(hTopology, SourceId, TargetId, &Path);
        if (!NT_SUCCESS(Status))
            return Status;
        Status = RdpVddValidatePath(Path);
        (VOID)TopologyInterface->pfnReleasePathInfo(hTopology, Path);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
APIENTRY
RdpVddDdiCommitVidPn(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_COMMITVIDPN_CONST pCommitVidPn)
{
    PRDPVDD_ADAPTER Adapter = (PRDPVDD_ADAPTER)hAdapter;
    const DXGK_VIDPN_INTERFACE *VidPnInterface;
    const DXGK_VIDPNTOPOLOGY_INTERFACE *TopologyInterface;
    D3DKMDT_HVIDPNTOPOLOGY hTopology;
    D3DKMDT_HVIDPNSOURCEMODESET hSourceModeSet;
    const DXGK_VIDPNSOURCEMODESET_INTERFACE *SourceModeSetInterface;
    const D3DKMDT_VIDPN_SOURCE_MODE *PinnedMode = NULL;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId;
    SIZE_T NumPaths;
    ULONG Width = 0, Height = 0;
    NTSTATUS Status;

    if (pCommitVidPn->Flags.PathPoweredOff)
        return STATUS_SUCCESS;

    SourceId = pCommitVidPn->AffectedVidPnSourceId == D3DDDI_ID_ALL ? 0 : pCommitVidPn->AffectedVidPnSourceId;
    if (SourceId >= RDPVDD_SOURCE_COUNT)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;

    Status = Adapter->DxgkInterface.DxgkCbQueryVidPnInterface(pCommitVidPn->hFunctionalVidPn,
                                                              DXGK_VIDPN_INTERFACE_VERSION_V1,
                                                              &VidPnInterface);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = VidPnInterface->pfnGetTopology(pCommitVidPn->hFunctionalVidPn, &hTopology, &TopologyInterface);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = TopologyInterface->pfnGetNumPaths(hTopology, &NumPaths);
    if (!NT_SUCCESS(Status))
        return Status;

    Adapter->SourceActive = FALSE;
    if (NumPaths == 0)
        return STATUS_SUCCESS;

    Status = VidPnInterface->pfnAcquireSourceModeSet(pCommitVidPn->hFunctionalVidPn, SourceId,
                                                     &hSourceModeSet, &SourceModeSetInterface);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = SourceModeSetInterface->pfnAcquirePinnedModeInfo(hSourceModeSet, &PinnedMode);
    if (NT_SUCCESS(Status) && PinnedMode != NULL)
    {
        if (!RdpVddIsSupportedSourceMode(Adapter, PinnedMode))
            Status = STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
        Width = PinnedMode->Format.Graphics.PrimSurfSize.cx;
        Height = PinnedMode->Format.Graphics.PrimSurfSize.cy;
        (VOID)SourceModeSetInterface->pfnReleaseModeInfo(hSourceModeSet, PinnedMode);
    }
    (VOID)VidPnInterface->pfnReleaseSourceModeSet(pCommitVidPn->hFunctionalVidPn, hSourceModeSet);
    if (!NT_SUCCESS(Status) || PinnedMode == NULL)
        return Status;

    Status = RdpVddCommitPaths(Adapter, SourceId, hTopology, TopologyInterface);
    if (!NT_SUCCESS(Status))
        return Status;

    RdpVddLockFrame(Adapter);
    if (Width != Adapter->Width || Height != Adapter->Height)
    {
        PUCHAR Scanout, OldScanout;
        SIZE_T ScanoutSize = (SIZE_T)Width * RDPVDD_BYTES_PER_PIXEL * Height;

        Scanout = ExAllocatePoolWithTag(NonPagedPool, ScanoutSize, RDPVDD_POOL_TAG);
        if (Scanout == NULL)
        {
            RdpVddUnlockFrame(Adapter);
            return STATUS_NO_MEMORY;
        }
        OldScanout = Adapter->Scanout;
        Adapter->Scanout = Scanout;
        Adapter->ScanoutSize = ScanoutSize;
        Adapter->Width = Width;
        Adapter->Height = Height;
        Adapter->Pitch = Width * RDPVDD_BYTES_PER_PIXEL;
        if (OldScanout != NULL)
            ExFreePoolWithTag(OldScanout, RDPVDD_POOL_TAG);
    }

    RdpVddBlackOut(Adapter);
    Adapter->SourceActive = TRUE;
    RdpVddAddDirty(Adapter, NULL);
    RdpVddSignalFrame(Adapter);
    RdpVddUnlockFrame(Adapter);
    return STATUS_SUCCESS;
}

static
NTSTATUS
APIENTRY
RdpVddDdiUpdateActiveVidPnPresentPath(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_UPDATEACTIVEVIDPNPRESENTPATH_CONST pUpdateActiveVidPnPresentPath)
{
    UNREFERENCED_PARAMETER(hAdapter);
    return RdpVddValidatePath(&pUpdateActiveVidPnPresentPath->VidPnPresentPathInfo);
}

static
NTSTATUS
APIENTRY
RdpVddDdiRecommendMonitorModes(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_RECOMMENDMONITORMODES_CONST pRecommendMonitorModes)
{
    PRDPVDD_ADAPTER Adapter = (PRDPVDD_ADAPTER)hAdapter;
    const DXGK_MONITORSOURCEMODESET_INTERFACE *Interface = pRecommendMonitorModes->pMonitorSourceModeSetInterface;
    D3DKMDT_MONITOR_SOURCE_MODE *Mode;
    NTSTATUS Status, ReleaseStatus;

    Status = Interface->pfnCreateNewModeInfo(pRecommendMonitorModes->hMonitorSourceModeSet, &Mode);
    if (!NT_SUCCESS(Status))
        return Status;

    Mode->VideoSignalInfo.VideoStandard = D3DKMDT_VSS_OTHER;
    Mode->VideoSignalInfo.TotalSize.cx = Adapter->PreferredWidth;
    Mode->VideoSignalInfo.TotalSize.cy = Adapter->PreferredHeight;
    Mode->VideoSignalInfo.ActiveSize = Mode->VideoSignalInfo.TotalSize;
    Mode->VideoSignalInfo.VSyncFreq.Numerator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    Mode->VideoSignalInfo.VSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    Mode->VideoSignalInfo.HSyncFreq.Numerator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    Mode->VideoSignalInfo.HSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    Mode->VideoSignalInfo.PixelRate = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    Mode->VideoSignalInfo.ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;
    Mode->Origin = D3DKMDT_MCO_DRIVER;
    Mode->Preference = D3DKMDT_MP_PREFERRED;
    Mode->ColorBasis = D3DKMDT_CB_SRGB;
    Mode->ColorCoeffDynamicRanges.FirstChannel = 8;
    Mode->ColorCoeffDynamicRanges.SecondChannel = 8;
    Mode->ColorCoeffDynamicRanges.ThirdChannel = 8;
    Mode->ColorCoeffDynamicRanges.FourthChannel = 8;

    Status = Interface->pfnAddMode(pRecommendMonitorModes->hMonitorSourceModeSet, Mode);
    if (!NT_SUCCESS(Status))
    {
        ReleaseStatus = Interface->pfnReleaseModeInfo(pRecommendMonitorModes->hMonitorSourceModeSet, Mode);
        if (Status == STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET)
            Status = ReleaseStatus;
    }
    return Status;
}

static
NTSTATUS
APIENTRY
RdpVddDdiQueryVidPnHWCapability(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_QUERYVIDPNHWCAPABILITY pVidPnHWCaps)
{
    UNREFERENCED_PARAMETER(hAdapter);

    if (pVidPnHWCaps->SourceId >= RDPVDD_SOURCE_COUNT || pVidPnHWCaps->TargetId >= RDPVDD_CHILD_COUNT)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(&pVidPnHWCaps->VidPnHWCaps, sizeof(pVidPnHWCaps->VidPnHWCaps));
    return STATUS_SUCCESS;
}

static
NTSTATUS
APIENTRY
RdpVddDdiPresentDisplayOnly(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_PRESENT_DISPLAYONLY pPresentDisplayOnly)
{
    PRDPVDD_ADAPTER Adapter = (PRDPVDD_ADAPTER)hAdapter;
    ULONG Index;

    if (pPresentDisplayOnly->VidPnSourceId >= RDPVDD_SOURCE_COUNT)
        return STATUS_INVALID_PARAMETER;
    if (pPresentDisplayOnly->BytesPerPixel != RDPVDD_BYTES_PER_PIXEL || pPresentDisplayOnly->pSource == NULL)
        return STATUS_INVALID_PARAMETER;
    RdpVddLockFrame(Adapter);
    if (!Adapter->SourceActive || !Adapter->SourceVisible ||
        Adapter->MonitorPowerState > PowerDeviceD0 || Adapter->Scanout == NULL)
    {
        RdpVddUnlockFrame(Adapter);
        return STATUS_SUCCESS;
    }

    for (Index = 0; Index < pPresentDisplayOnly->NumMoves; Index++)
    {
        RdpVddCopyRect(Adapter, pPresentDisplayOnly->pSource, pPresentDisplayOnly->Pitch, 0, 0,
                       &pPresentDisplayOnly->pMoves[Index].DestRect);
        RdpVddAddDirty(Adapter, &pPresentDisplayOnly->pMoves[Index].DestRect);
    }
    for (Index = 0; Index < pPresentDisplayOnly->NumDirtyRects; Index++)
    {
        RdpVddCopyRect(Adapter, pPresentDisplayOnly->pSource, pPresentDisplayOnly->Pitch, 0, 0,
                       &pPresentDisplayOnly->pDirtyRect[Index]);
        RdpVddAddDirty(Adapter, &pPresentDisplayOnly->pDirtyRect[Index]);
    }
    RdpVddSignalFrame(Adapter);
    RdpVddUnlockFrame(Adapter);
    return STATUS_SUCCESS;
}

static
NTSTATUS
APIENTRY
RdpVddDdiStopDeviceAndReleasePostDisplayOwnership(
    _In_ PVOID MiniportDeviceContext,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _Out_ PDXGK_DISPLAY_INFORMATION DisplayInfo)
{
    UNREFERENCED_PARAMETER(TargetId);

    RtlZeroMemory(DisplayInfo, sizeof(*DisplayInfo));
    return RdpVddDdiStopDevice(MiniportDeviceContext);
}

static
NTSTATUS
APIENTRY
RdpVddDdiSystemDisplayEnable(
    _In_ PVOID MiniportDeviceContext,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _In_ PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
    _Out_ PUINT Width,
    _Out_ PUINT Height,
    _Out_ D3DDDIFORMAT *ColorFormat)
{
    PRDPVDD_ADAPTER Adapter = MiniportDeviceContext;

    UNREFERENCED_PARAMETER(Flags);

    if (TargetId != D3DDDI_ID_UNINITIALIZED && TargetId >= RDPVDD_CHILD_COUNT)
        return STATUS_INVALID_PARAMETER;
    if (!Adapter->Started || !Adapter->SourceActive || Adapter->Scanout == NULL)
        return STATUS_UNSUCCESSFUL;

    *Width = Adapter->Width;
    *Height = Adapter->Height;
    *ColorFormat = D3DDDIFMT_A8R8G8B8;
    return STATUS_SUCCESS;
}

static
VOID
APIENTRY
RdpVddDdiSystemDisplayWrite(
    _In_ PVOID MiniportDeviceContext,
    _In_ PVOID Source,
    _In_ UINT SourceWidth,
    _In_ UINT SourceHeight,
    _In_ UINT SourceStride,
    _In_ UINT PositionX,
    _In_ UINT PositionY)
{
    PRDPVDD_ADAPTER Adapter = MiniportDeviceContext;
    RECT Rect;

    if (Adapter->Scanout == NULL || Source == NULL ||
        PositionX >= Adapter->Width || PositionY >= Adapter->Height)
        return;

    Rect.left = (LONG)PositionX;
    Rect.top = (LONG)PositionY;
    Rect.right = (LONG)min((ULONGLONG)PositionX + SourceWidth, (ULONGLONG)Adapter->Width);
    Rect.bottom = (LONG)min((ULONGLONG)PositionY + SourceHeight, (ULONGLONG)Adapter->Height);
    RdpVddCopyRect(Adapter, Source, (LONG)SourceStride, Rect.left, Rect.top, &Rect);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    KMDDOD_INITIALIZATION_DATA InitData;

    RtlZeroMemory(&InitData, sizeof(InitData));
    InitData.Version = DXGKDDI_INTERFACE_VERSION_WDDM2_0;
    InitData.DxgkDdiAddDevice = RdpVddDdiAddDevice;
    InitData.DxgkDdiStartDevice = RdpVddDdiStartDevice;
    InitData.DxgkDdiStopDevice = RdpVddDdiStopDevice;
    InitData.DxgkDdiRemoveDevice = RdpVddDdiRemoveDevice;
    InitData.DxgkDdiDispatchIoRequest = RdpVddDdiDispatchIoRequest;
    InitData.DxgkDdiQueryChildRelations = RdpVddDdiQueryChildRelations;
    InitData.DxgkDdiQueryChildStatus = RdpVddDdiQueryChildStatus;
    InitData.DxgkDdiQueryDeviceDescriptor = RdpVddDdiQueryDeviceDescriptor;
    InitData.DxgkDdiSetPowerState = RdpVddDdiSetPowerState;
    InitData.DxgkDdiResetDevice = RdpVddDdiResetDevice;
    InitData.DxgkDdiUnload = RdpVddDdiUnload;
    InitData.DxgkDdiQueryAdapterInfo = RdpVddDdiQueryAdapterInfo;
    InitData.DxgkDdiSetPointerPosition = RdpVddDdiSetPointerPosition;
    InitData.DxgkDdiSetPointerShape = RdpVddDdiSetPointerShape;
    InitData.DxgkDdiEscape = RdpVddDdiEscape;
    InitData.DxgkDdiIsSupportedVidPn = RdpVddDdiIsSupportedVidPn;
    InitData.DxgkDdiRecommendFunctionalVidPn = RdpVddDdiRecommendFunctionalVidPn;
    InitData.DxgkDdiEnumVidPnCofuncModality = RdpVddDdiEnumVidPnCofuncModality;
    InitData.DxgkDdiSetVidPnSourceVisibility = RdpVddDdiSetVidPnSourceVisibility;
    InitData.DxgkDdiCommitVidPn = RdpVddDdiCommitVidPn;
    InitData.DxgkDdiUpdateActiveVidPnPresentPath = RdpVddDdiUpdateActiveVidPnPresentPath;
    InitData.DxgkDdiRecommendMonitorModes = RdpVddDdiRecommendMonitorModes;
    InitData.DxgkDdiQueryVidPnHWCapability = RdpVddDdiQueryVidPnHWCapability;
    InitData.DxgkDdiPresentDisplayOnly = RdpVddDdiPresentDisplayOnly;
    InitData.DxgkDdiStopDeviceAndReleasePostDisplayOwnership = RdpVddDdiStopDeviceAndReleasePostDisplayOwnership;
    InitData.DxgkDdiSystemDisplayEnable = RdpVddDdiSystemDisplayEnable;
    InitData.DxgkDdiSystemDisplayWrite = RdpVddDdiSystemDisplayWrite;
    InitData.DxgkDdiNotifySurpriseRemoval = RdpVddDdiNotifySurpriseRemoval;

    return DxgkInitializeDisplayOnlyDriver(DriverObject, RegistryPath, &InitData);
}
