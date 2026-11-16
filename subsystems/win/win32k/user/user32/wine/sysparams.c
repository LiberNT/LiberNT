
#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#include <windef.h>
#include <wingdi.h>
#include <winuser.h>
#include <winbase.h>
#include <d3dkmthk.h>
#include <reactos/rddm/rxgkadvcolor.h>
#include <reactos/rddm/rxgktarget.h>
#include "wine/debug.h"

#ifndef NT_SUCCESS
#define NT_SUCCESS(status) ((NTSTATUS)(status) >= 0)
#endif

WINE_DEFAULT_DEBUG_CHANNEL(win);

/* Only define DISPLAYCONFIG types locally when system headers do not provide them. */


/*
 * Display configuration.
 *
 * Every attached GDI display is one active path.  The WDDM adapter LUID and
 * VidPn source of a display come from D3DKMTOpenAdapterFromGdiDisplayName,
 * which is also what a user-mode driver uses when it later asks
 * DisplayConfigGetDeviceInfo about the same (adapterId, id) pair.  One target
 * per source is assumed and the target takes the source ordinal; clone and
 * multi-target topologies are not described.
 */

#define DISPLAYCONFIG_MAX_PATHS 16

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#endif

typedef struct _DISPLAYCONFIG_LOCAL_PATH
{
    WCHAR GdiDeviceName[CCHDEVICENAME];
    WCHAR AdapterDeviceId[128];
    LUID AdapterId;
    UINT32 SourceId;
    UINT32 TargetId;
    DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY OutputTechnology;
    UINT32 ConnectorInstance;
    UINT32 TargetFlags;
    BYTE Edid[128];
    DEVMODEW Mode;
} DISPLAYCONFIG_LOCAL_PATH;

static BOOL
DisplayConfigResolveAdapter(
    DISPLAYCONFIG_LOCAL_PATH *Path)
{
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME Open;
    D3DKMT_CLOSEADAPTER Close;
    D3DKMT_ESCAPE Escape;
    RXGK_TARGET_INFO_PACKET Target;

    memset(&Open, 0, sizeof(Open));
    lstrcpynW(Open.DeviceName, Path->GdiDeviceName, ARRAY_SIZE(Open.DeviceName));
    if (!NT_SUCCESS(D3DKMTOpenAdapterFromGdiDisplayName(&Open)))
        return FALSE;
    Path->AdapterId = Open.AdapterLuid;
    Path->SourceId = Open.VidPnSourceId;

    memset(&Target, 0, sizeof(Target));
    Target.Size = sizeof(Target);
    Target.Version = RXGK_TARGET_INFO_VERSION_1;
    Target.VidPnSourceId = Open.VidPnSourceId;
    memset(&Escape, 0, sizeof(Escape));
    Escape.hAdapter = Open.hAdapter;
    Escape.Type = (D3DKMT_ESCAPETYPE)RXGK_ESCAPE_TARGET_INFO;
    Escape.pPrivateDriverData = &Target;
    Escape.PrivateDriverDataSize = sizeof(Target);
    if (NT_SUCCESS(D3DKMTEscape(&Escape)))
    {
        Path->TargetId = Target.VidPnTargetId;
        Path->OutputTechnology = (DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY)Target.OutputTechnology;
        Path->ConnectorInstance = Target.ConnectorInstance;
        Path->TargetFlags = Target.Flags;
        memcpy(Path->Edid, Target.Edid, sizeof(Path->Edid));
    }
    else
    {
        /* FIXME: an adapter without a committed topology reports its source id as the target id */
        Path->TargetId = Open.VidPnSourceId;
        Path->OutputTechnology = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_OTHER;
    }

    Close.hAdapter = Open.hAdapter;
    D3DKMTCloseAdapter(&Close);
    return TRUE;
}

/* Collect the attached displays; returns the count or -1 when the WDDM
 * adapter of a display cannot be resolved. */
static int
DisplayConfigCollectPaths(
    DISPLAYCONFIG_LOCAL_PATH *Paths,
    UINT32 Capacity)
{
    DISPLAY_DEVICEW Device;
    DWORD Index;
    UINT32 Count = 0;

    for (Index = 0; Count < Capacity; Index++)
    {
        DISPLAYCONFIG_LOCAL_PATH *Path = &Paths[Count];

        memset(&Device, 0, sizeof(Device));
        Device.cb = sizeof(Device);
        if (!EnumDisplayDevicesW(NULL, Index, &Device, 0))
            break;
        if (!(Device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) ||
            (Device.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER))
            continue;

        memset(Path, 0, sizeof(*Path));
        lstrcpynW(Path->GdiDeviceName, Device.DeviceName, ARRAY_SIZE(Path->GdiDeviceName));
        lstrcpynW(Path->AdapterDeviceId, Device.DeviceID, ARRAY_SIZE(Path->AdapterDeviceId));
        Path->Mode.dmSize = sizeof(Path->Mode);
        if (!EnumDisplaySettingsW(Device.DeviceName, ENUM_CURRENT_SETTINGS, &Path->Mode))
            continue;
        if (!DisplayConfigResolveAdapter(Path))
            return -1;
        Count++;
    }
    return (int)Count;
}

static BOOL
DisplayConfigValidQueryFlags(
    UINT32 flags)
{
    UINT32 scope = flags & (QDC_ALL_PATHS | QDC_ONLY_ACTIVE_PATHS | QDC_DATABASE_CURRENT);

    if (scope != QDC_ALL_PATHS && scope != QDC_ONLY_ACTIVE_PATHS && scope != QDC_DATABASE_CURRENT)
        return FALSE;
    return (flags & ~(scope | QDC_VIRTUAL_MODE_AWARE | QDC_INCLUDE_HMD | QDC_VIRTUAL_REFRESH_RATE_AWARE)) == 0;
}

static DISPLAYCONFIG_PIXELFORMAT
DisplayConfigPixelFormat(
    DWORD BitsPerPel)
{
    switch (BitsPerPel)
    {
        case 8:  return DISPLAYCONFIG_PIXELFORMAT_8BPP;
        case 16: return DISPLAYCONFIG_PIXELFORMAT_16BPP;
        case 24: return DISPLAYCONFIG_PIXELFORMAT_24BPP;
        case 32: return DISPLAYCONFIG_PIXELFORMAT_32BPP;
        default: return DISPLAYCONFIG_PIXELFORMAT_NONGDI;
    }
}

static void
DisplayConfigFillSignal(
    const DEVMODEW *Mode,
    DISPLAYCONFIG_VIDEO_SIGNAL_INFO *Signal)
{
    UINT32 Refresh = Mode->dmDisplayFrequency > 1 ? Mode->dmDisplayFrequency : 60;

    memset(Signal, 0, sizeof(*Signal));
    Signal->activeSize.cx = Mode->dmPelsWidth;
    Signal->activeSize.cy = Mode->dmPelsHeight;
    Signal->totalSize = Signal->activeSize;
    Signal->pixelRate = (UINT64)Mode->dmPelsWidth * Mode->dmPelsHeight * Refresh;
    Signal->hSyncFreq.Numerator = Mode->dmPelsHeight * Refresh;
    Signal->hSyncFreq.Denominator = 1;
    Signal->vSyncFreq.Numerator = Refresh;
    Signal->vSyncFreq.Denominator = 1;
    Signal->videoStandard = 0; /* D3DKMDT_VSS_OTHER */
    Signal->scanLineOrdering = (Mode->dmDisplayFlags & DM_INTERLACED) ?
        DISPLAYCONFIG_SCANLINE_ORDERING_INTERLACED : DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;
}

LONG
WINAPI
GetDisplayConfigBufferSizes(
    UINT32 flags,
    UINT32 *numPathArrayElements,
    UINT32 *numModeInfoArrayElements)
{
    DISPLAYCONFIG_LOCAL_PATH Paths[DISPLAYCONFIG_MAX_PATHS];
    int Count;

    if (numPathArrayElements == NULL || numModeInfoArrayElements == NULL)
        return ERROR_INVALID_PARAMETER;
    *numPathArrayElements = 0;
    if (!DisplayConfigValidQueryFlags(flags))
        return ERROR_INVALID_PARAMETER;

    Count = DisplayConfigCollectPaths(Paths, ARRAY_SIZE(Paths));
    if (Count < 0)
        return ERROR_GEN_FAILURE;
    *numPathArrayElements = Count;
    *numModeInfoArrayElements = Count * ((flags & QDC_VIRTUAL_MODE_AWARE) ? 3 : 2);
    return ERROR_SUCCESS;
}

LONG
WINAPI
QueryDisplayConfig(
    UINT32                    flags,
    UINT32                    *numPathArrayElements,
    DISPLAYCONFIG_PATH_INFO   *pathArray,
    UINT32                    *numModeInfoArrayElements,
    DISPLAYCONFIG_MODE_INFO   *modeInfoArray,
    DISPLAYCONFIG_TOPOLOGY_ID *currentTopologyId)
{
    DISPLAYCONFIG_LOCAL_PATH Paths[DISPLAYCONFIG_MAX_PATHS];
    UINT32 ModesPerPath = (flags & QDC_VIRTUAL_MODE_AWARE) ? 3 : 2;
    int Count;
    int Index;

    if (numPathArrayElements == NULL || numModeInfoArrayElements == NULL)
        return ERROR_INVALID_PARAMETER;
    if (!*numPathArrayElements || !*numModeInfoArrayElements)
        return ERROR_INVALID_PARAMETER;
    if (pathArray == NULL || modeInfoArray == NULL)
        return ERROR_INVALID_PARAMETER;
    if (!DisplayConfigValidQueryFlags(flags))
        return ERROR_INVALID_PARAMETER;
    /* The topology id is defined only for the database query. */
    if ((flags & QDC_DATABASE_CURRENT) ? currentTopologyId == NULL : currentTopologyId != NULL)
        return ERROR_INVALID_PARAMETER;

    Count = DisplayConfigCollectPaths(Paths, ARRAY_SIZE(Paths));
    if (Count < 0)
        return ERROR_GEN_FAILURE;
    if (*numPathArrayElements < (UINT32)Count || *numModeInfoArrayElements < (UINT32)Count * ModesPerPath)
        return ERROR_INSUFFICIENT_BUFFER;

    for (Index = 0; Index < Count; Index++)
    {
        const DISPLAYCONFIG_LOCAL_PATH *Local = &Paths[Index];
        DISPLAYCONFIG_PATH_INFO *Path = &pathArray[Index];
        DISPLAYCONFIG_MODE_INFO *SourceMode = &modeInfoArray[Index * ModesPerPath];
        DISPLAYCONFIG_MODE_INFO *TargetMode = &modeInfoArray[Index * ModesPerPath + 1];
        UINT32 Refresh = Local->Mode.dmDisplayFrequency > 1 ? Local->Mode.dmDisplayFrequency : 60;

        memset(Path, 0, sizeof(*Path));
        Path->sourceInfo.adapterId = Local->AdapterId;
        Path->sourceInfo.id = Local->SourceId;
        if (flags & QDC_VIRTUAL_MODE_AWARE)
        {
            Path->sourceInfo.sourceModeInfoIdx = Index * ModesPerPath;
            Path->sourceInfo.cloneGroupId = DISPLAYCONFIG_PATH_CLONE_GROUP_INVALID;
        }
        else
        {
            Path->sourceInfo.modeInfoIdx = Index * ModesPerPath;
        }
        Path->sourceInfo.statusFlags = DISPLAYCONFIG_SOURCE_IN_USE;
        Path->targetInfo.adapterId = Local->AdapterId;
        Path->targetInfo.id = Local->TargetId;
        if (flags & QDC_VIRTUAL_MODE_AWARE)
        {
            Path->targetInfo.targetModeInfoIdx = Index * ModesPerPath + 1;
            Path->targetInfo.desktopModeInfoIdx = Index * ModesPerPath + 2;
        }
        else
        {
            Path->targetInfo.modeInfoIdx = Index * ModesPerPath + 1;
        }
        Path->targetInfo.outputTechnology = Local->OutputTechnology;
        Path->targetInfo.rotation = DISPLAYCONFIG_ROTATION_IDENTITY;
        Path->targetInfo.scaling = DISPLAYCONFIG_SCALING_IDENTITY;
        Path->targetInfo.refreshRate.Numerator = Refresh;
        Path->targetInfo.refreshRate.Denominator = 1;
        Path->targetInfo.scanLineOrdering = (Local->Mode.dmDisplayFlags & DM_INTERLACED) ?
            DISPLAYCONFIG_SCANLINE_ORDERING_INTERLACED : DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;
        Path->targetInfo.targetAvailable = TRUE;
        Path->targetInfo.statusFlags = DISPLAYCONFIG_TARGET_IN_USE;
        Path->flags = DISPLAYCONFIG_PATH_ACTIVE;
        if (flags & QDC_VIRTUAL_MODE_AWARE)
            Path->flags |= DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE;

        memset(SourceMode, 0, sizeof(*SourceMode));
        SourceMode->infoType = DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE;
        SourceMode->id = Local->SourceId;
        SourceMode->adapterId = Local->AdapterId;
        SourceMode->sourceMode.width = Local->Mode.dmPelsWidth;
        SourceMode->sourceMode.height = Local->Mode.dmPelsHeight;
        SourceMode->sourceMode.pixelFormat = DisplayConfigPixelFormat(Local->Mode.dmBitsPerPel);
        SourceMode->sourceMode.position.x = Local->Mode.dmPosition.x;
        SourceMode->sourceMode.position.y = Local->Mode.dmPosition.y;

        memset(TargetMode, 0, sizeof(*TargetMode));
        TargetMode->infoType = DISPLAYCONFIG_MODE_INFO_TYPE_TARGET;
        TargetMode->id = Local->TargetId;
        TargetMode->adapterId = Local->AdapterId;
        DisplayConfigFillSignal(&Local->Mode, &TargetMode->targetMode.targetVideoSignalInfo);
        if (flags & QDC_DATABASE_CURRENT)
        {
            TargetMode->targetMode.targetVideoSignalInfo.totalSize.cx = 0;
            TargetMode->targetMode.targetVideoSignalInfo.totalSize.cy = 0;
        }

        if (flags & QDC_VIRTUAL_MODE_AWARE)
        {
            DISPLAYCONFIG_MODE_INFO *DesktopMode = &modeInfoArray[Index * ModesPerPath + 2];

            memset(DesktopMode, 0, sizeof(*DesktopMode));
            DesktopMode->infoType = DISPLAYCONFIG_MODE_INFO_TYPE_DESKTOP_IMAGE;
            DesktopMode->id = Local->TargetId;
            DesktopMode->adapterId = Local->AdapterId;
            DesktopMode->desktopImageInfo.PathSourceSize.x = Local->Mode.dmPelsWidth;
            DesktopMode->desktopImageInfo.PathSourceSize.y = Local->Mode.dmPelsHeight;
            DesktopMode->desktopImageInfo.DesktopImageRegion.right = Local->Mode.dmPelsWidth;
            DesktopMode->desktopImageInfo.DesktopImageRegion.bottom = Local->Mode.dmPelsHeight;
            DesktopMode->desktopImageInfo.DesktopImageClip = DesktopMode->desktopImageInfo.DesktopImageRegion;
        }
    }

    *numPathArrayElements = Count;
    *numModeInfoArrayElements = Count * ModesPerPath;
    if (currentTopologyId != NULL)
        *currentTopologyId = Count > 1 ? DISPLAYCONFIG_TOPOLOGY_EXTEND : DISPLAYCONFIG_TOPOLOGY_INTERNAL;
    return ERROR_SUCCESS;
}

/* The advanced colour state of the output a path's source drives, from
 * dxgkrnl (RXGK_ESCAPE_ADVANCED_COLOR). */
static NTSTATUS
DisplayConfigAdvancedColor(
    const DISPLAYCONFIG_LOCAL_PATH *Path,
    RXGK_ADVANCED_COLOR_PACKET *Packet)
{
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME Open;
    D3DKMT_CLOSEADAPTER Close;
    D3DKMT_ESCAPE Escape;
    NTSTATUS Status;

    memset(&Open, 0, sizeof(Open));
    lstrcpynW(Open.DeviceName, Path->GdiDeviceName, ARRAY_SIZE(Open.DeviceName));
    Status = D3DKMTOpenAdapterFromGdiDisplayName(&Open);
    if (!NT_SUCCESS(Status))
        return Status;

    Packet->Size = sizeof(*Packet);
    Packet->Version = RXGK_ADVANCED_COLOR_VERSION_1;
    Packet->VidPnSourceId = Open.VidPnSourceId;
    memset(&Escape, 0, sizeof(Escape));
    Escape.hAdapter = Open.hAdapter;
    Escape.Type = (D3DKMT_ESCAPETYPE)RXGK_ESCAPE_ADVANCED_COLOR;
    Escape.pPrivateDriverData = Packet;
    Escape.PrivateDriverDataSize = sizeof(*Packet);
    Status = D3DKMTEscape(&Escape);

    Close.hAdapter = Open.hAdapter;
    D3DKMTCloseAdapter(&Close);
    return Status;
}

/* The display name descriptor (tag 0xFC) of a base EDID block, without its
 * line feed and padding. */
static BOOL
DisplayConfigEdidName(
    const BYTE *Edid,
    WCHAR *Name,
    UINT32 Capacity)
{
    UINT32 Descriptor;
    UINT32 Length;

    for (Descriptor = 0; Descriptor < 4; Descriptor++)
    {
        const BYTE *Block = &Edid[54 + Descriptor * 18];

        if (Block[0] != 0 || Block[1] != 0 || Block[3] != 0xFC)
            continue;
        for (Length = 0; Length < 13 && Length + 1 < Capacity && Block[5 + Length] != 0 &&
             Block[5 + Length] != 0x0A; Length++)
        {
            Name[Length] = Block[5 + Length];
        }
        while (Length != 0 && Name[Length - 1] == L' ')
            Length--;
        Name[Length] = 0;
        return Length != 0;
    }
    return FALSE;
}

static LONG
DisplayConfigStatusToError(
    NTSTATUS Status)
{
    if (NT_SUCCESS(Status))
        return ERROR_SUCCESS;
    if (Status == (NTSTATUS)0xC00000BBL) /* STATUS_NOT_SUPPORTED */
        return ERROR_NOT_SUPPORTED;
    if (Status == (NTSTATUS)0xC000000DL) /* STATUS_INVALID_PARAMETER */
        return ERROR_INVALID_PARAMETER;
    return ERROR_GEN_FAILURE;
}

typedef enum ORIENTATION_PREFERENCE {
    ORIENTATION_PREFERENCE_NONE              = 0x0,
    ORIENTATION_PREFERENCE_LANDSCAPE         = 0x1,
    ORIENTATION_PREFERENCE_PORTRAIT          = 0x2,
    ORIENTATION_PREFERENCE_LANDSCAPE_FLIPPED = 0x4,
    ORIENTATION_PREFERENCE_PORTRAIT_FLIPPED  = 0x8
} ORIENTATION_PREFERENCE;

/***********************************************************************
 *              DisplayConfigGetDeviceInfo (USER32.@)
 */
LONG WINAPI DisplayConfigGetDeviceInfo(DISPLAYCONFIG_DEVICE_INFO_HEADER *packet)
{
    DISPLAYCONFIG_LOCAL_PATH Paths[DISPLAYCONFIG_MAX_PATHS];
    const DISPLAYCONFIG_LOCAL_PATH *Local = NULL;
    BOOL AdapterFound = FALSE;
    UINT32 ExpectedSize;
    int Count;
    int Index;

    if (packet == NULL || packet->size < sizeof(*packet))
        return ERROR_GEN_FAILURE;

    if (packet->type == DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_DPI_SCALE)
        ExpectedSize = sizeof(DISPLAYCONFIG_GET_SOURCE_DPI_SCALE);
    else switch (packet->type)
    {
        case DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME:
            ExpectedSize = sizeof(DISPLAYCONFIG_SOURCE_DEVICE_NAME);
            break;
        case DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME:
            ExpectedSize = sizeof(DISPLAYCONFIG_TARGET_DEVICE_NAME);
            break;
        case DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_PREFERRED_MODE:
            ExpectedSize = sizeof(DISPLAYCONFIG_TARGET_PREFERRED_MODE);
            break;
        case DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME:
            ExpectedSize = sizeof(DISPLAYCONFIG_ADAPTER_NAME);
            break;
        case DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_BASE_TYPE:
            ExpectedSize = sizeof(DISPLAYCONFIG_TARGET_BASE_TYPE);
            break;
        case DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO:
            ExpectedSize = sizeof(DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO);
            break;
        case DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL:
            ExpectedSize = sizeof(DISPLAYCONFIG_SDR_WHITE_LEVEL);
            break;
        default:
            FIXME("DisplayConfigGetDeviceInfo: type %d not supported\n", packet->type);
            return ERROR_INVALID_PARAMETER;
    }
    if (packet->size != ExpectedSize)
        return ERROR_INVALID_PARAMETER;

    Count = DisplayConfigCollectPaths(Paths, ARRAY_SIZE(Paths));
    if (Count < 0)
        return ERROR_GEN_FAILURE;
    for (Index = 0; Index < Count; Index++)
    {
        UINT32 Id = (packet->type == DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME ||
                     packet->type == DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_DPI_SCALE) ?
            Paths[Index].SourceId : Paths[Index].TargetId;

        if (Paths[Index].AdapterId.LowPart != packet->adapterId.LowPart ||
            Paths[Index].AdapterId.HighPart != packet->adapterId.HighPart)
        {
            continue;
        }
        AdapterFound = TRUE;
        if (packet->type == DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME || Id == packet->id)
        {
            Local = &Paths[Index];
            break;
        }
    }
    if (Local == NULL && AdapterFound &&
        (packet->type == DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME ||
         packet->type == DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_BASE_TYPE))
    {
        return ERROR_INVALID_PARAMETER;
    }
    if (Local == NULL)
        return ERROR_GEN_FAILURE;

    if (packet->type == DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_DPI_SCALE)
    {
        DISPLAYCONFIG_GET_SOURCE_DPI_SCALE *Scale = (DISPLAYCONFIG_GET_SOURCE_DPI_SCALE *)packet;

        Scale->minRelativeScaleStep = 0;
        Scale->curRelativeScaleStep = 0;
        Scale->maxRelativeScaleStep = 0;
        return ERROR_SUCCESS;
    }

    switch (packet->type)
    {
        case DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME:
        {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME *Name = (DISPLAYCONFIG_SOURCE_DEVICE_NAME *)packet;

            lstrcpynW(Name->viewGdiDeviceName, Local->GdiDeviceName, ARRAY_SIZE(Name->viewGdiDeviceName));
            return ERROR_SUCCESS;
        }
        case DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME:
        {
            DISPLAYCONFIG_TARGET_DEVICE_NAME *Name = (DISPLAYCONFIG_TARGET_DEVICE_NAME *)packet;
            DISPLAY_DEVICEW Monitor;

            memset(&Name->flags, 0, (char *)(Name + 1) - (char *)&Name->flags);
            Name->outputTechnology = Local->OutputTechnology;
            Name->connectorInstance = Local->ConnectorInstance;
            if (!(Local->TargetFlags & RXGK_TARGET_INFO_CONNECTED))
                Name->flags.friendlyNameForced = 1;
            else if (Local->TargetFlags & RXGK_TARGET_INFO_EDID)
            {
                Name->edidManufactureId = (UINT16)(Local->Edid[8] | (Local->Edid[9] << 8));
                Name->edidProductCodeId = (UINT16)(Local->Edid[10] | (Local->Edid[11] << 8));
                Name->flags.edidIdsValid = 1;
                if (DisplayConfigEdidName(Local->Edid, Name->monitorFriendlyDeviceName,
                                          ARRAY_SIZE(Name->monitorFriendlyDeviceName)))
                {
                    Name->flags.friendlyNameFromEdid = 1;
                }
            }
            memset(&Monitor, 0, sizeof(Monitor));
            Monitor.cb = sizeof(Monitor);
            if (EnumDisplayDevicesW(Local->GdiDeviceName, 0, &Monitor, EDD_GET_DEVICE_INTERFACE_NAME))
                lstrcpynW(Name->monitorDevicePath, Monitor.DeviceID, ARRAY_SIZE(Name->monitorDevicePath));
            return ERROR_SUCCESS;
        }
        case DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_PREFERRED_MODE:
        {
            DISPLAYCONFIG_TARGET_PREFERRED_MODE *Preferred = (DISPLAYCONFIG_TARGET_PREFERRED_MODE *)packet;

            Preferred->width = Local->Mode.dmPelsWidth;
            Preferred->height = Local->Mode.dmPelsHeight;
            DisplayConfigFillSignal(&Local->Mode, &Preferred->targetMode.targetVideoSignalInfo);
            return ERROR_SUCCESS;
        }
        case DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME:
        {
            DISPLAYCONFIG_ADAPTER_NAME *Name = (DISPLAYCONFIG_ADAPTER_NAME *)packet;

            lstrcpynW(Name->adapterDevicePath, Local->AdapterDeviceId, ARRAY_SIZE(Name->adapterDevicePath));
            return ERROR_SUCCESS;
        }
        case DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_BASE_TYPE:
        {
            DISPLAYCONFIG_TARGET_BASE_TYPE *Base = (DISPLAYCONFIG_TARGET_BASE_TYPE *)packet;

            Base->baseOutputTechnology = Local->OutputTechnology;
            return ERROR_SUCCESS;
        }
        case DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO:
        {
            DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO *Info = (DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO *)packet;
            RXGK_ADVANCED_COLOR_PACKET Color;
            NTSTATUS Status;

            memset(&Color, 0, sizeof(Color));
            Color.Operation = RXGK_ADVANCED_COLOR_GET;
            Status = DisplayConfigAdvancedColor(Local, &Color);
            if (!NT_SUCCESS(Status))
                return DisplayConfigStatusToError(Status);
            Info->value = 0;
            Info->advancedColorSupported = (Color.Flags & RXGK_ADVANCED_COLOR_SUPPORTED) != 0;
            Info->advancedColorEnabled = (Color.Flags & RXGK_ADVANCED_COLOR_ENABLED) != 0;
            Info->colorEncoding = (DISPLAYCONFIG_COLOR_ENCODING)Color.ColorEncoding;
            Info->bitsPerColorChannel = Color.BitsPerColorChannel;
            return ERROR_SUCCESS;
        }
        case DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL:
        {
            DISPLAYCONFIG_SDR_WHITE_LEVEL *White = (DISPLAYCONFIG_SDR_WHITE_LEVEL *)packet;
            RXGK_ADVANCED_COLOR_PACKET Color;
            NTSTATUS Status;

            memset(&Color, 0, sizeof(Color));
            Color.Operation = RXGK_ADVANCED_COLOR_GET;
            Status = DisplayConfigAdvancedColor(Local, &Color);
            if (!NT_SUCCESS(Status))
                return DisplayConfigStatusToError(Status);
            /* In thousandths of 80 nits: 1000 is 80 nits. */
            White->SDRWhiteLevel = Color.SdrWhiteLevel * 1000 / 80;
            return ERROR_SUCCESS;
        }
        default:
            return ERROR_INVALID_PARAMETER;
    }
}

/***********************************************************************
 *              DisplayConfigSetDeviceInfo (USER32.@)
 */
LONG WINAPI DisplayConfigSetDeviceInfo( DISPLAYCONFIG_DEVICE_INFO_HEADER *packet )
{
    DISPLAYCONFIG_LOCAL_PATH Paths[DISPLAYCONFIG_MAX_PATHS];
    const DISPLAYCONFIG_LOCAL_PATH *Local = NULL;
    int Count;
    int Index;

    if (packet == NULL || packet->size < sizeof(*packet))
        return ERROR_GEN_FAILURE;

    if (packet->type == DISPLAYCONFIG_DEVICE_INFO_SET_SOURCE_DPI_SCALE)
    {
        DISPLAYCONFIG_SET_SOURCE_DPI_SCALE *Scale = (DISPLAYCONFIG_SET_SOURCE_DPI_SCALE *)packet;

        if (packet->size != sizeof(*Scale))
            return ERROR_INVALID_PARAMETER;
        Count = DisplayConfigCollectPaths(Paths, ARRAY_SIZE(Paths));
        if (Count < 0)
            return ERROR_GEN_FAILURE;
        for (Index = 0; Index < Count; Index++)
        {
            if (Paths[Index].AdapterId.LowPart == packet->adapterId.LowPart &&
                Paths[Index].AdapterId.HighPart == packet->adapterId.HighPart &&
                Paths[Index].SourceId == packet->id)
            {
                Local = &Paths[Index];
                break;
            }
        }
        if (Local == NULL)
            return ERROR_GEN_FAILURE;
        if (Scale->relativeScaleStep != 0)
            return ERROR_INVALID_PARAMETER;
        return ERROR_SUCCESS;
    }

    switch (packet->type)
    {
        case DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE:
        {
            DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE *State = (DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE *)packet;
            RXGK_ADVANCED_COLOR_PACKET Color;

            if (packet->size != sizeof(*State))
                return ERROR_INVALID_PARAMETER;
            Count = DisplayConfigCollectPaths(Paths, ARRAY_SIZE(Paths));
            if (Count < 0)
                return ERROR_GEN_FAILURE;
            for (Index = 0; Index < Count; Index++)
            {
                if (Paths[Index].AdapterId.LowPart == packet->adapterId.LowPart &&
                    Paths[Index].AdapterId.HighPart == packet->adapterId.HighPart &&
                    Paths[Index].TargetId == packet->id)
                {
                    Local = &Paths[Index];
                    break;
                }
            }
            if (Local == NULL)
                return ERROR_GEN_FAILURE;

            memset(&Color, 0, sizeof(Color));
            Color.Operation = RXGK_ADVANCED_COLOR_SET;
            Color.Enable = State->enableAdvancedColor ? 1 : 0;
            return DisplayConfigStatusToError(
                DisplayConfigAdvancedColor(Local, &Color));
        }
        default:
            FIXME("DisplayConfigSetDeviceInfo: type %d not supported\n", packet->type);
            return ERROR_NOT_SUPPORTED;
    }
}

/**********************************************************************
 *              GetDisplayAutoRotationPreferences (USER32.@)
 */
BOOL WINAPI GetDisplayAutoRotationPreferences( ORIENTATION_PREFERENCE *orientation )
{
    FIXME("(%p): stub\n", orientation);
    *orientation = ORIENTATION_PREFERENCE_NONE;
    return TRUE;
}

/***********************************************************************
 *              SetDisplayConfig (USER32.@)
 */
static LONG
DisplayConfigApplySource(
    const DISPLAYCONFIG_LOCAL_PATH *Local,
    const DISPLAYCONFIG_SOURCE_MODE *Source,
    DWORD ChangeFlags)
{
    DEVMODEW Mode;
    LONG Change;

    memset(&Mode, 0, sizeof(Mode));
    Mode.dmSize = sizeof(Mode);
    Mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_POSITION;
    Mode.dmPelsWidth = Source->width;
    Mode.dmPelsHeight = Source->height;
    Mode.dmPosition.x = Source->position.x;
    Mode.dmPosition.y = Source->position.y;
    switch (Source->pixelFormat)
    {
        case DISPLAYCONFIG_PIXELFORMAT_8BPP:  Mode.dmBitsPerPel = 8;  break;
        case DISPLAYCONFIG_PIXELFORMAT_16BPP: Mode.dmBitsPerPel = 16; break;
        case DISPLAYCONFIG_PIXELFORMAT_24BPP: Mode.dmBitsPerPel = 24; break;
        case DISPLAYCONFIG_PIXELFORMAT_32BPP: Mode.dmBitsPerPel = 32; break;
        default: break;
    }
    if (Mode.dmBitsPerPel)
        Mode.dmFields |= DM_BITSPERPEL;

    Change = ChangeDisplaySettingsExW(Local->GdiDeviceName, &Mode, NULL, ChangeFlags, NULL);
    if (Change == DISP_CHANGE_SUCCESSFUL || Change == DISP_CHANGE_RESTART)
        return ERROR_SUCCESS;
    if (Change == DISP_CHANGE_BADMODE)
        return ERROR_BAD_CONFIGURATION;
    return ERROR_GEN_FAILURE;
}

LONG WINAPI SetDisplayConfig(UINT32 path_info_count, DISPLAYCONFIG_PATH_INFO *path_info, UINT32 mode_info_count,
        DISPLAYCONFIG_MODE_INFO *mode_info, UINT32 flags)
{
    DISPLAYCONFIG_LOCAL_PATH Paths[DISPLAYCONFIG_MAX_PATHS];
    DWORD ChangeFlags;
    UINT32 Index, ModeIndex;
    LONG Result;
    int Count, Local;

    TRACE("path_info_count %u, path_info %p, mode_info_count %u, mode_info %p, flags %#x\n",
          path_info_count, path_info, mode_info_count, mode_info, flags);

    if (!(flags & (SDC_APPLY | SDC_VALIDATE)))
        return ERROR_INVALID_PARAMETER;

    if (!(flags & SDC_USE_SUPPLIED_DISPLAY_CONFIG))
    {
        if (!(flags & SDC_TOPOLOGY_SUPPLIED) && (path_info_count || path_info || mode_info_count || mode_info))
            return ERROR_INVALID_PARAMETER;
        return ERROR_SUCCESS;
    }

    if (!path_info_count || !path_info || (mode_info_count && !mode_info))
        return ERROR_INVALID_PARAMETER;

    Count = DisplayConfigCollectPaths(Paths, ARRAY_SIZE(Paths));
    if (Count < 0)
        return ERROR_GEN_FAILURE;

    if (flags & SDC_VALIDATE)
        ChangeFlags = CDS_TEST;
    else
        ChangeFlags = (flags & SDC_SAVE_TO_DATABASE) ? CDS_UPDATEREGISTRY : 0;

    for (Index = 0; Index < path_info_count; Index++)
    {
        const DISPLAYCONFIG_PATH_INFO *Path = &path_info[Index];
        const DISPLAYCONFIG_MODE_INFO *Source;

        if (!(Path->flags & DISPLAYCONFIG_PATH_ACTIVE))
            continue;

        for (Local = 0; Local < Count; Local++)
        {
            if (Paths[Local].AdapterId.LowPart == Path->sourceInfo.adapterId.LowPart &&
                Paths[Local].AdapterId.HighPart == Path->sourceInfo.adapterId.HighPart &&
                Paths[Local].SourceId == Path->sourceInfo.id)
                break;
        }
        if (Local == Count)
            return ERROR_INVALID_PARAMETER;

        if (flags & SDC_VIRTUAL_MODE_AWARE)
        {
            ModeIndex = Path->sourceInfo.sourceModeInfoIdx;
            if (ModeIndex == DISPLAYCONFIG_PATH_SOURCE_MODE_IDX_INVALID)
                continue;
        }
        else
        {
            ModeIndex = Path->sourceInfo.modeInfoIdx;
            if (ModeIndex == DISPLAYCONFIG_PATH_MODE_IDX_INVALID)
                continue;
        }
        if (ModeIndex >= mode_info_count)
            return ERROR_INVALID_PARAMETER;

        Source = &mode_info[ModeIndex];
        if (Source->infoType != DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE)
            return ERROR_INVALID_PARAMETER;

        Result = DisplayConfigApplySource(&Paths[Local], &Source->sourceMode, ChangeFlags);
        if (Result != ERROR_SUCCESS)
            return Result;
    }

    return ERROR_SUCCESS;
}

/**********************************************************************
 *              SetDisplayAutoRotationPreferences (USER32.@)
 */
BOOL WINAPI SetDisplayAutoRotationPreferences( ORIENTATION_PREFERENCE orientation )
{
    FIXME("(%d): stub\n", orientation);
    return TRUE;
}
