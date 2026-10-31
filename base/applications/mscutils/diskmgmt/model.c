/*
 * PROJECT:     LiberNT Disk Management
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Disk, partition and volume enumeration
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "diskmgmt.h"

static const GUID DuRecoveryGuid =
    { 0xDE94BBA4, 0x06D1, 0x4D40, { 0xA1, 0x6A, 0xBF, 0xD5, 0x01, 0x79, 0xD6, 0xAC } };

VOID
DuLoadString(
    _In_ UINT Id,
    _Out_writes_(cch) PWSTR Text,
    _In_ SIZE_T cch)
{
    Text[0] = UNICODE_NULL;
    LoadStringW(g_hInstance, Id, Text, (INT)cch);
}

VOID
DuPrintf(
    _Out_writes_(cch) PWSTR Text,
    _In_ SIZE_T cch,
    _In_ UINT Id,
    ...)
{
    WCHAR Format[256];
    va_list Args;

    DuLoadString(Id, Format, ARRAYSIZE(Format));
    va_start(Args, Id);
    StringCchVPrintfW(Text, cch, Format, Args);
    va_end(Args);
}

VOID
DuFormatSize(
    _In_ ULONGLONG Bytes,
    _Out_writes_(cch) PWSTR Text,
    _In_ SIZE_T cch)
{
    if (!StrFormatByteSizeW((LONGLONG)Bytes, Text, (UINT)cch))
        StringCchPrintfW(Text, cch, L"%I64u", Bytes);
}

VOID
DuBusName(
    _In_ STORAGE_BUS_TYPE Bus,
    _Out_writes_(cch) PWSTR Text,
    _In_ SIZE_T cch)
{
    static const struct { STORAGE_BUS_TYPE Bus; PCWSTR Name; } Names[] =
    {
        { BusTypeScsi, L"SCSI" }, { BusTypeAtapi, L"ATAPI" }, { BusTypeAta, L"ATA" },
        { BusType1394, L"1394" }, { BusTypeSsa, L"SSA" }, { BusTypeFibre, L"Fibre Channel" },
        { BusTypeUsb, L"USB" }, { BusTypeRAID, L"RAID" }, { BusTypeiScsi, L"iSCSI" },
        { BusTypeSas, L"SAS" }, { BusTypeSata, L"SATA" }, { BusTypeSd, L"SD" },
        { BusTypeMmc, L"MMC" }, { BusTypeVirtual, L"Virtual" },
        { BusTypeFileBackedVirtual, L"File Backed Virtual" }, { BusTypeNvme, L"NVMe" },
    };
    ULONG i;

    for (i = 0; i < ARRAYSIZE(Names); i++)
    {
        if (Names[i].Bus == Bus)
        {
            StringCchCopyW(Text, cch, Names[i].Name);
            return;
        }
    }
    DuLoadString(IDS_UNKNOWN, Text, cch);
}

static DU_CATEGORY
DuCategorize(
    _In_ STORAGE_BUS_TYPE Bus,
    _In_ BOOL Removable)
{
    if (Bus == BusTypeVirtual || Bus == BusTypeFileBackedVirtual)
        return DuCategoryImage;
    if (Removable || Bus == BusTypeUsb || Bus == BusTypeSd || Bus == BusTypeMmc || Bus == BusType1394)
        return DuCategoryExternal;
    return DuCategoryInternal;
}

static VOID
DuCopyDescriptorString(
    _In_ const STORAGE_DEVICE_DESCRIPTOR *Desc,
    _In_ ULONG Size,
    _In_ ULONG Offset,
    _Out_writes_(cch) PWSTR Text,
    _In_ SIZE_T cch)
{
    CHAR Buffer[128];
    PCSTR Source;
    SIZE_T Length;

    Text[0] = UNICODE_NULL;
    if (Offset == 0 || Offset >= Size)
        return;
    Source = (PCSTR)Desc + Offset;
    StringCchCopyNA(Buffer, ARRAYSIZE(Buffer), Source, Size - Offset);
    StrTrimA(Buffer, " ");
    Length = strlen(Buffer);
    if (Length)
        MultiByteToWideChar(CP_ACP, 0, Buffer, -1, Text, (INT)cch);
}

static VOID
DuQueryDevice(
    _In_ HANDLE Handle,
    _Inout_ PDU_DISK Disk)
{
    STORAGE_PROPERTY_QUERY Query;
    BYTE Buffer[1024];
    PSTORAGE_DEVICE_DESCRIPTOR Desc = (PSTORAGE_DEVICE_DESCRIPTOR)Buffer;
    WCHAR Vendor[64], Product[64];
    DWORD Returned;

    ZeroMemory(&Query, sizeof(Query));
    Query.PropertyId = StorageDeviceProperty;
    Query.QueryType = PropertyStandardQuery;
    ZeroMemory(Buffer, sizeof(Buffer));
    Disk->Bus = BusTypeUnknown;
    if (!DeviceIoControl(Handle, IOCTL_STORAGE_QUERY_PROPERTY, &Query, sizeof(Query),
                         Buffer, sizeof(Buffer), &Returned, NULL) ||
        Returned < FIELD_OFFSET(STORAGE_DEVICE_DESCRIPTOR, RawPropertiesLength))
    {
        return;
    }

    Disk->Bus = Desc->BusType;
    Disk->Removable = Desc->RemovableMedia;
    DuCopyDescriptorString(Desc, Returned, Desc->VendorIdOffset, Vendor, ARRAYSIZE(Vendor));
    DuCopyDescriptorString(Desc, Returned, Desc->ProductIdOffset, Product, ARRAYSIZE(Product));
    DuCopyDescriptorString(Desc, Returned, Desc->SerialNumberOffset, Disk->Serial, ARRAYSIZE(Disk->Serial));
    if (Vendor[0] && Product[0] && StrCmpNIW(Product, Vendor, lstrlenW(Vendor)) != 0)
        StringCchPrintfW(Disk->Model, ARRAYSIZE(Disk->Model), L"%s %s", Vendor, Product);
    else if (Product[0])
        StringCchCopyW(Disk->Model, ARRAYSIZE(Disk->Model), Product);
    else
        StringCchCopyW(Disk->Model, ARRAYSIZE(Disk->Model), Vendor);
}

static DU_PART_KIND
DuGptKind(
    _In_ const GUID *Type)
{
    if (IsEqualGUID(Type, &PARTITION_BASIC_DATA_GUID))
        return DuPartData;
    if (IsEqualGUID(Type, &PARTITION_SYSTEM_GUID))
        return DuPartEsp;
    if (IsEqualGUID(Type, &PARTITION_MSFT_RESERVED_GUID))
        return DuPartMsr;
    if (IsEqualGUID(Type, &DuRecoveryGuid))
        return DuPartRecovery;
    return DuPartOther;
}

static DU_PART_KIND
DuMbrKind(
    _In_ UCHAR Type)
{
    switch (Type)
    {
        case PARTITION_FAT_12:
        case PARTITION_FAT_16:
        case PARTITION_HUGE:
        case PARTITION_IFS:
        case PARTITION_FAT32:
        case PARTITION_FAT32_XINT13:
        case PARTITION_XINT13:
            return DuPartData;
        case 0xEF:
            return DuPartEsp;
        case 0x27:
            return DuPartRecovery;
    }
    return DuPartOther;
}

static VOID
DuAddPart(
    _Inout_ PDU_DISK Disk,
    _In_ const DU_PART *Part)
{
    ULONG i;

    if (Disk->PartCount >= DU_MAX_PARTS)
        return;
    for (i = Disk->PartCount; i > 0 && Disk->Parts[i - 1].Offset > Part->Offset; i--)
        Disk->Parts[i] = Disk->Parts[i - 1];
    Disk->Parts[i] = *Part;
    Disk->PartCount++;
}

static VOID
DuAddFreeRegions(
    _Inout_ PDU_DISK Disk)
{
    DU_PART Free;
    ULONGLONG Cursor = Disk->UsableStart;
    ULONG Count = Disk->PartCount;
    ULONG i;
    DU_PART Parts[DU_MAX_PARTS];

    CopyMemory(Parts, Disk->Parts, Count * sizeof(DU_PART));
    for (i = 0; i <= Count; i++)
    {
        ULONGLONG End = (i < Count) ? Parts[i].Offset : Disk->UsableEnd;
        ULONGLONG Start = (Cursor + DU_MEGABYTE - 1) & ~(DU_MEGABYTE - 1);

        if (End > Start && End - Start >= DU_MEGABYTE)
        {
            ZeroMemory(&Free, sizeof(Free));
            Free.Offset = Start;
            Free.Length = (End - Start) & ~(DU_MEGABYTE - 1);
            Free.Kind = DuPartFree;
            Free.Volume = -1;
            if (Free.Length)
                DuAddPart(Disk, &Free);
        }
        if (i < Count && Parts[i].Offset + Parts[i].Length > Cursor)
            Cursor = Parts[i].Offset + Parts[i].Length;
    }
}

static VOID
DuQueryLayout(
    _In_ HANDLE Handle,
    _Inout_ PDU_DISK Disk)
{
    PDRIVE_LAYOUT_INFORMATION_EX Layout;
    DWORD Size = sizeof(DRIVE_LAYOUT_INFORMATION_EX) + 127 * sizeof(PARTITION_INFORMATION_EX);
    DWORD Returned;
    ULONG i;

    Disk->Style = PARTITION_STYLE_RAW;
    Disk->UsableStart = DU_MEGABYTE;
    Disk->UsableEnd = Disk->Size;

    Layout = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size);
    if (!Layout)
        return;
    if (!DeviceIoControl(Handle, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, Layout, Size, &Returned, NULL))
    {
        HeapFree(GetProcessHeap(), 0, Layout);
        return;
    }

    if (Layout->PartitionStyle == PARTITION_STYLE_MBR && Layout->PartitionCount == 0)
    {
        HeapFree(GetProcessHeap(), 0, Layout);
        return;
    }
    Disk->Style = Layout->PartitionStyle;
    if (Layout->PartitionStyle == PARTITION_STYLE_GPT)
    {
        Disk->DiskId = Layout->Gpt.DiskId;
        Disk->UsableStart = Layout->Gpt.StartingUsableOffset.QuadPart;
        Disk->UsableEnd = Layout->Gpt.StartingUsableOffset.QuadPart + Layout->Gpt.UsableLength.QuadPart;
    }
    else if (Layout->PartitionStyle == PARTITION_STYLE_MBR)
    {
        Disk->Signature = Layout->Mbr.Signature;
    }

    for (i = 0; i < Layout->PartitionCount; i++)
    {
        PPARTITION_INFORMATION_EX Entry = &Layout->PartitionEntry[i];
        DU_PART Part;

        if (Entry->PartitionLength.QuadPart == 0)
            continue;
        ZeroMemory(&Part, sizeof(Part));
        Part.Offset = Entry->StartingOffset.QuadPart;
        Part.Length = Entry->PartitionLength.QuadPart;
        Part.Number = Entry->PartitionNumber;
        Part.Volume = -1;
        if (Entry->PartitionStyle == PARTITION_STYLE_GPT)
        {
            if (IsEqualGUID(&Entry->Gpt.PartitionType, &GUID_NULL))
                continue;
            Part.Type = Entry->Gpt.PartitionType;
            Part.Id = Entry->Gpt.PartitionId;
            Part.Kind = DuGptKind(&Entry->Gpt.PartitionType);
            StringCchCopyNW(Part.Name, ARRAYSIZE(Part.Name), Entry->Gpt.Name, ARRAYSIZE(Entry->Gpt.Name));
        }
        else
        {
            if (Entry->Mbr.PartitionType == PARTITION_ENTRY_UNUSED ||
                IsContainerPartition(Entry->Mbr.PartitionType))
            {
                continue;
            }
            Part.MbrType = Entry->Mbr.PartitionType;
            Part.Active = Entry->Mbr.BootIndicator;
            Part.Kind = DuMbrKind(Entry->Mbr.PartitionType);
        }
        DuAddPart(Disk, &Part);
    }

    HeapFree(GetProcessHeap(), 0, Layout);
    if (Disk->Style != PARTITION_STYLE_RAW)
        DuAddFreeRegions(Disk);
}

static VOID
DuEnumDisks(
    _Inout_ PDU_MODEL Model)
{
    ULONG Number;

    for (Number = 0; Number < 64 && Model->DiskCount < DU_MAX_DISKS; Number++)
    {
        WCHAR Path[MAX_PATH];
        DISK_GEOMETRY_EX Geometry;
        PDU_DISK Disk;
        HANDLE Handle;
        DWORD Returned;

        StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\\\.\\PhysicalDrive%lu", Number);
        Handle = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL, OPEN_EXISTING, 0, NULL);
        if (Handle == INVALID_HANDLE_VALUE)
            continue;

        Disk = &Model->Disks[Model->DiskCount];
        ZeroMemory(Disk, sizeof(*Disk));
        Disk->Number = Number;
        if (DeviceIoControl(Handle, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0,
                            &Geometry, sizeof(Geometry), &Returned, NULL))
        {
            Disk->Size = Geometry.DiskSize.QuadPart;
            Disk->BytesPerSector = Geometry.Geometry.BytesPerSector;
        }
        if (Disk->Size == 0)
        {
            CloseHandle(Handle);
            continue;
        }
        DuQueryDevice(Handle, Disk);
        Disk->Category = DuCategorize(Disk->Bus, Disk->Removable);
        DuQueryLayout(Handle, Disk);
        CloseHandle(Handle);
        Model->DiskCount++;
    }
}

static VOID
DuEnumVolumes(
    _Inout_ PDU_MODEL Model)
{
    WCHAR Name[MAX_PATH], WindowsVolume[64], Windows[MAX_PATH];
    HANDLE Find;

    WindowsVolume[0] = UNICODE_NULL;
    if (GetWindowsDirectoryW(Windows, ARRAYSIZE(Windows)) && PathStripToRootW(Windows))
        GetVolumeNameForVolumeMountPointW(Windows, WindowsVolume, ARRAYSIZE(WindowsVolume));

    Find = FindFirstVolumeW(Name, ARRAYSIZE(Name));
    if (Find == INVALID_HANDLE_VALUE)
        return;
    do
    {
        BYTE ExtentBuffer[sizeof(VOLUME_DISK_EXTENTS) + 7 * sizeof(DISK_EXTENT)];
        PVOLUME_DISK_EXTENTS Extents = (PVOLUME_DISK_EXTENTS)ExtentBuffer;
        WCHAR Device[MAX_PATH], Paths[MAX_PATH * 4];
        PDU_VOLUME Volume;
        ULARGE_INTEGER Avail, Total, Free;
        SIZE_T Length;
        HANDLE Handle;
        DWORD Returned;

        if (Model->VolumeCount >= DU_MAX_VOLUMES)
            break;
        Length = wcslen(Name);
        if (Length < 5 || Name[Length - 1] != L'\\')
            continue;

        StringCchCopyW(Device, ARRAYSIZE(Device), Name);
        Device[Length - 1] = UNICODE_NULL;
        Handle = CreateFileW(Device, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (Handle == INVALID_HANDLE_VALUE)
            continue;
        if (!DeviceIoControl(Handle, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0,
                             Extents, sizeof(ExtentBuffer), &Returned, NULL) ||
            Extents->NumberOfDiskExtents != 1)
        {
            CloseHandle(Handle);
            continue;
        }
        CloseHandle(Handle);

        Volume = &Model->Volumes[Model->VolumeCount];
        ZeroMemory(Volume, sizeof(*Volume));
        StringCchCopyW(Volume->GuidPath, ARRAYSIZE(Volume->GuidPath), Name);
        Volume->DiskNumber = Extents->Extents[0].DiskNumber;
        Volume->Offset = Extents->Extents[0].StartingOffset.QuadPart;
        Volume->Capacity = Extents->Extents[0].ExtentLength.QuadPart;
        if (!QueryDosDeviceW(&Device[4], Volume->DeviceName, ARRAYSIZE(Volume->DeviceName)))
            Volume->DeviceName[0] = UNICODE_NULL;
        if (GetVolumePathNamesForVolumeNameW(Name, Paths, ARRAYSIZE(Paths), &Returned) && Paths[0])
            StringCchCopyW(Volume->MountPath, ARRAYSIZE(Volume->MountPath), Paths);
        Volume->HasFileSystem = GetVolumeInformationW(Name, Volume->Label, ARRAYSIZE(Volume->Label),
                                                      &Volume->Serial, NULL, &Volume->FsFlags,
                                                      Volume->FileSystem, ARRAYSIZE(Volume->FileSystem));
        if (Volume->HasFileSystem && GetDiskFreeSpaceExW(Name, &Avail, &Total, &Free))
        {
            Volume->Capacity = Total.QuadPart;
            Volume->Free = Free.QuadPart;
        }
        Volume->Boot = WindowsVolume[0] && !lstrcmpiW(WindowsVolume, Name);
        Model->VolumeCount++;
    } while (FindNextVolumeW(Find, Name, ARRAYSIZE(Name)));
    FindVolumeClose(Find);
}

static VOID
DuLinkVolumes(
    _Inout_ PDU_MODEL Model)
{
    ULONG d, p, v;

    for (d = 0; d < Model->DiskCount; d++)
    {
        PDU_DISK Disk = &Model->Disks[d];

        for (p = 0; p < Disk->PartCount; p++)
        {
            for (v = 0; v < Model->VolumeCount; v++)
            {
                if (Disk->Parts[p].Kind != DuPartFree &&
                    Model->Volumes[v].DiskNumber == Disk->Number &&
                    Model->Volumes[v].Offset == Disk->Parts[p].Offset)
                {
                    Disk->Parts[p].Volume = (INT)v;
                    break;
                }
            }
        }
    }
}

VOID
DuModelRefresh(
    _Out_ PDU_MODEL Model)
{
    ZeroMemory(Model, sizeof(*Model));
    DuEnumDisks(Model);
    DuEnumVolumes(Model);
    DuLinkVolumes(Model);
}

PDU_VOLUME
DuPartVolume(
    _In_ PDU_MODEL Model,
    _In_ const DU_PART *Part)
{
    if (Part->Volume < 0 || (ULONG)Part->Volume >= Model->VolumeCount)
        return NULL;
    return &Model->Volumes[Part->Volume];
}

VOID
DuPartKindName(
    _In_ const DU_DISK *Disk,
    _In_ const DU_PART *Part,
    _Out_writes_(cch) PWSTR Text,
    _In_ SIZE_T cch)
{
    static const UINT Ids[] =
    {
        IDS_KIND_DATA, IDS_KIND_ESP, IDS_KIND_MSR, IDS_KIND_RECOVERY, IDS_KIND_OTHER, IDS_KIND_FREE
    };

    if (Part->Kind == DuPartData && Disk->Style == PARTITION_STYLE_MBR)
        DuLoadString(IDS_KIND_PRIMARY, Text, cch);
    else
        DuLoadString(Ids[Part->Kind], Text, cch);
}

VOID
DuPartTitle(
    _In_ PDU_MODEL Model,
    _In_ const DU_PART *Part,
    _Out_writes_(cch) PWSTR Text,
    _In_ SIZE_T cch)
{
    PDU_VOLUME Volume = DuPartVolume(Model, Part);
    WCHAR Kind[64];
    ULONG d;

    if (Volume && Volume->HasFileSystem)
    {
        WCHAR Label[MAX_PATH];

        if (Volume->Label[0])
            StringCchCopyW(Label, ARRAYSIZE(Label), Volume->Label);
        else
            DuLoadString(IDS_UNTITLED, Label, ARRAYSIZE(Label));
        if (Volume->MountPath[0] && Volume->MountPath[1] == L':' && Volume->MountPath[3] == UNICODE_NULL)
            StringCchPrintfW(Text, cch, L"%s (%c:)", Label, Volume->MountPath[0]);
        else
            StringCchCopyW(Text, cch, Label);
        return;
    }

    for (d = 0; d < Model->DiskCount; d++)
    {
        if (Part >= Model->Disks[d].Parts && Part < Model->Disks[d].Parts + DU_MAX_PARTS)
        {
            DuPartKindName(&Model->Disks[d], Part, Kind, ARRAYSIZE(Kind));
            StringCchCopyW(Text, cch, Kind);
            return;
        }
    }
    Text[0] = UNICODE_NULL;
}

VOID
DuDiskTitle(
    _In_ const DU_DISK *Disk,
    _Out_writes_(cch) PWSTR Text,
    _In_ SIZE_T cch)
{
    if (Disk->Model[0])
        StringCchCopyW(Text, cch, Disk->Model);
    else
        DuPrintf(Text, cch, IDS_DISK_N, Disk->Number);
}

BOOL
DuPartIsProtected(
    _In_ PDU_MODEL Model,
    _In_ const DU_DISK *Disk,
    _In_ const DU_PART *Part)
{
    PDU_VOLUME Volume = DuPartVolume(Model, Part);

    UNREFERENCED_PARAMETER(Disk);
    if (Volume && Volume->Boot)
        return TRUE;
    return Part->Kind == DuPartEsp || Part->Kind == DuPartMsr || Part->Kind == DuPartRecovery;
}

BOOL
DuDiskHasProtectedVolume(
    _In_ PDU_MODEL Model,
    _In_ const DU_DISK *Disk)
{
    ULONG p;

    for (p = 0; p < Disk->PartCount; p++)
    {
        PDU_VOLUME Volume = DuPartVolume(Model, &Disk->Parts[p]);

        if ((Volume && Volume->Boot) || Disk->Parts[p].Kind == DuPartEsp)
            return TRUE;
    }
    return FALSE;
}

COLORREF
DuPartColor(
    _In_ const DU_DISK *Disk,
    _In_ ULONG Index)
{
    static const COLORREF Palette[] =
    {
        RGB(0x00, 0x78, 0xD4), RGB(0x88, 0x5E, 0xD8), RGB(0x2E, 0xA0, 0x6B),
        RGB(0xD8, 0x8A, 0x1E), RGB(0x1E, 0xA5, 0xC8), RGB(0xC8, 0x45, 0x6E),
    };
    ULONG i, Data = 0;

    switch (Disk->Parts[Index].Kind)
    {
        case DuPartFree:
            return GetSysColor(COLOR_BTNFACE);
        case DuPartData:
            break;
        default:
            return RGB(0x80, 0x80, 0x80);
    }
    for (i = 0; i < Index; i++)
    {
        if (Disk->Parts[i].Kind == DuPartData)
            Data++;
    }
    return Palette[Data % ARRAYSIZE(Palette)];
}
