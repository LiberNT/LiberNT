/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the KS filter of a USB camera, checked against Windows 11
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lntuvc_test.h"

typedef struct _EXPECT_RANGE
{
    const GUID *Subtype;
    LONG Width;
    LONG Height;
    DWORD Compression;
    WORD Bits;
    DWORD Image;
    LONG MinInterval;
    LONG MaxInterval;
    DWORD MinBits;
    DWORD MaxBits;
    DWORD Rate;
    BOOL Fixed;
} EXPECT_RANGE;

typedef struct _EXPECT_CONTROL
{
    ULONG Id;
    LONG Min;
    LONG Max;
    ULONG Step;
    LONG Default;
    ULONG Flags;
    ULONG Capabilities;
} EXPECT_CONTROL;

#define AUTO_FLAG   KSPROPERTY_VIDEOPROCAMP_FLAGS_AUTO
#define MANUAL_FLAG KSPROPERTY_VIDEOPROCAMP_FLAGS_MANUAL

static const EXPECT_CONTROL ExpectProcAmp[] =
{
    { 0, -64, 64, 1, 0, MANUAL_FLAG, MANUAL_FLAG },
    { 1, 0, 95, 1, 32, MANUAL_FLAG, MANUAL_FLAG },
    { 2, -2000, 2000, 100, 0, MANUAL_FLAG, MANUAL_FLAG },
    { 3, 0, 100, 1, 64, MANUAL_FLAG, MANUAL_FLAG },
    { 4, 1, 7, 1, 3, MANUAL_FLAG, MANUAL_FLAG },
    { 5, 100, 300, 1, 100, MANUAL_FLAG, MANUAL_FLAG },
    { 7, 2800, 6500, 10, 4600, AUTO_FLAG, AUTO_FLAG | MANUAL_FLAG },
    { 8, 0, 2, 1, 1, MANUAL_FLAG, MANUAL_FLAG },
    { 9, 0, 100, 1, 0, MANUAL_FLAG, MANUAL_FLAG },
    { 13, 0, 2, 1, 1, 0, 0 },
};

static const ULONG MissingProcAmp[] = { 6, 10, 11, 12 };

static const EXPECT_CONTROL ExpectCamera[] =
{
    { 0, -10, 10, 1, 0, MANUAL_FLAG, MANUAL_FLAG },
    { 1, -5, 5, 1, 0, MANUAL_FLAG, MANUAL_FLAG },
    { 3, 100, 500, 1, 100, MANUAL_FLAG, MANUAL_FLAG },
    { 4, -13, 0, 1, -6, AUTO_FLAG, AUTO_FLAG | MANUAL_FLAG },
    { 6, 0, 250, 5, 0, AUTO_FLAG, AUTO_FLAG | MANUAL_FLAG },
};

static const ULONG MissingCamera[] = { 2, 5, 7, 8, 10, 11, 12, 13, 14, 15, 16, 17, 19, 20 };

static void
CheckRange(const KSDATARANGE *Range, const EXPECT_RANGE *Expect, BOOL Second)
{
    const KS_VIDEO_STREAM_CONFIG_CAPS *Caps;
    const KS_BITMAPINFOHEADER *Bmi;
    BOOL Fixed;
    DWORD Rate;
    LONGLONG Average;

    ok(Range->FormatSize == (Second ? sizeof(KS_DATARANGE_VIDEO2) : sizeof(KS_DATARANGE_VIDEO)), "FormatSize %lu\n", Range->FormatSize);
    ok(Range->Flags == 0, "Flags %lx\n", Range->Flags);
    ok(Range->SampleSize == Expect->Image, "SampleSize %lu, expected %lu\n", Range->SampleSize, Expect->Image);
    ok(IsEqualGUID(&Range->MajorFormat, &UvcFormatVideo), "Major %s\n", wine_dbgstr_guid(&Range->MajorFormat));
    ok(IsEqualGUID(&Range->SubFormat, Expect->Subtype), "Sub %s\n", wine_dbgstr_guid(&Range->SubFormat));
    ok(IsEqualGUID(&Range->Specifier, Second ? &UvcSpecifierVideoInfo2 : &UvcSpecifierVideoInfo), "Specifier %s\n", wine_dbgstr_guid(&Range->Specifier));

    if (Second)
    {
        const KS_DATARANGE_VIDEO2 *Video = (const KS_DATARANGE_VIDEO2 *)Range;
        LONG AspectX = Expect->Width == 1280 ? 16 : 4;
        LONG AspectY = Expect->Width == 1280 ? 9 : 3;

        Fixed = Video->bFixedSizeSamples;
        Caps = &Video->ConfigCaps;
        Bmi = &Video->VideoInfoHeader.bmiHeader;
        Rate = Video->VideoInfoHeader.dwBitRate;
        Average = Video->VideoInfoHeader.AvgTimePerFrame;
        ok(Video->VideoInfoHeader.dwInterlaceFlags == 0, "Interlace %lx\n", Video->VideoInfoHeader.dwInterlaceFlags);
        ok(Video->VideoInfoHeader.dwCopyProtectFlags == 0, "CopyProtect %lx\n", Video->VideoInfoHeader.dwCopyProtectFlags);
        ok(Video->VideoInfoHeader.dwPictAspectRatioX == (DWORD)AspectX, "AspectX %lu\n", Video->VideoInfoHeader.dwPictAspectRatioX);
        ok(Video->VideoInfoHeader.dwPictAspectRatioY == (DWORD)AspectY, "AspectY %lu\n", Video->VideoInfoHeader.dwPictAspectRatioY);
        ok(Video->VideoInfoHeader.dwControlFlags == (Expect->Fixed ? 0x8D2681 : 0x8D1181), "%ldx%ld fixed %d ControlFlags %lx\n",
           Expect->Width, Expect->Height, Expect->Fixed, Video->VideoInfoHeader.dwControlFlags);
        ok(IsEqualGUID(&Caps->guid, &UvcSpecifierVideoInfo2), "Caps guid %s\n", wine_dbgstr_guid(&Caps->guid));
    }
    else
    {
        const KS_DATARANGE_VIDEO *Video = (const KS_DATARANGE_VIDEO *)Range;

        Fixed = Video->bFixedSizeSamples;
        Caps = &Video->ConfigCaps;
        Bmi = &Video->VideoInfoHeader.bmiHeader;
        Rate = Video->VideoInfoHeader.dwBitRate;
        Average = Video->VideoInfoHeader.AvgTimePerFrame;
        ok(Video->bTemporalCompression == FALSE, "Temporal %d\n", Video->bTemporalCompression);
        ok(Video->StreamDescriptionFlags == 0, "StreamDescriptionFlags %lx\n", Video->StreamDescriptionFlags);
        ok(Video->MemoryAllocationFlags == 0, "MemoryAllocationFlags %lx\n", Video->MemoryAllocationFlags);
        ok(Video->VideoInfoHeader.rcSource.right == 0 && Video->VideoInfoHeader.rcTarget.right == 0, "Rects are set\n");
        ok(IsEqualGUID(&Caps->guid, &UvcSpecifierVideoInfo), "Caps guid %s\n", wine_dbgstr_guid(&Caps->guid));
    }

    ok(Fixed == Expect->Fixed, "Fixed %d\n", Fixed);
    ok(Caps->VideoStandard == 0, "VideoStandard %lx\n", Caps->VideoStandard);
    ok(Caps->InputSize.cx == Expect->Width && Caps->InputSize.cy == Expect->Height, "InputSize %ldx%ld\n", Caps->InputSize.cx, Caps->InputSize.cy);
    ok(Caps->MinCroppingSize.cx == Expect->Width && Caps->MaxCroppingSize.cy == Expect->Height, "Cropping size\n");
    ok(Caps->CropGranularityX == 1 && Caps->CropGranularityY == 1, "Crop granularity\n");
    ok(Caps->CropAlignX == 1 && Caps->CropAlignY == 1, "Crop alignment\n");
    ok(Caps->MinOutputSize.cx == Expect->Width && Caps->MaxOutputSize.cy == Expect->Height, "Output size\n");
    ok(Caps->OutputGranularityX == 1 && Caps->OutputGranularityY == 1, "Output granularity\n");
    ok(Caps->StretchTapsX == 0 && Caps->ShrinkTapsY == 0, "Taps\n");
    ok(Caps->MinFrameInterval == Expect->MinInterval, "MinFrameInterval %ld\n", (LONG)Caps->MinFrameInterval);
    ok(Caps->MaxFrameInterval == Expect->MaxInterval, "MaxFrameInterval %ld\n", (LONG)Caps->MaxFrameInterval);
    ok(Caps->MinBitsPerSecond == (LONG)Expect->MinBits, "MinBitsPerSecond %ld\n", Caps->MinBitsPerSecond);
    ok(Caps->MaxBitsPerSecond == (LONG)Expect->MaxBits, "MaxBitsPerSecond %ld\n", Caps->MaxBitsPerSecond);
    ok(Rate == Expect->Rate, "BitRate %lu, expected %lu\n", Rate, Expect->Rate);
    ok(Average == Expect->MinInterval, "AvgTimePerFrame %ld\n", (LONG)Average);
    ok(Bmi->biSize == sizeof(KS_BITMAPINFOHEADER), "biSize %lu\n", Bmi->biSize);
    ok(Bmi->biWidth == Expect->Width && Bmi->biHeight == Expect->Height, "Bitmap %ldx%ld\n", Bmi->biWidth, Bmi->biHeight);
    ok(Bmi->biPlanes == 1, "biPlanes %u\n", Bmi->biPlanes);
    ok(Bmi->biBitCount == Expect->Bits, "biBitCount %u\n", Bmi->biBitCount);
    ok(Bmi->biCompression == Expect->Compression, "biCompression %08lx\n", Bmi->biCompression);
    ok(Bmi->biSizeImage == Expect->Image, "biSizeImage %lu\n", Bmi->biSizeImage);
}

static void
CheckRanges(HANDLE Filter)
{
    const EXPECT_RANGE Expect[] =
    {
        { &UvcSubtypeYuy2, 640, 480, UVC_FOURCC('Y', 'U', 'Y', '2'), 16, 614400, 333333, 666666, 24576000, 147456000, 147456000, TRUE },
        { &UvcSubtypeYuy2, 320, 240, UVC_FOURCC('Y', 'U', 'Y', '2'), 16, 153600, 333333, 666666, 6144000, 36864000, 36864000, TRUE },
        { &UvcSubtypeYuy2, 160, 120, UVC_FOURCC('Y', 'U', 'Y', '2'), 16, 38400, 333333, 666666, 1536000, 9216000, 9216000, TRUE },
        { &UvcSubtypeYuy2, 1280, 720, UVC_FOURCC('Y', 'U', 'Y', '2'), 16, 1843200, 1000000, 2000000, 73728000, 442368000, 147456000, TRUE },
        { &UvcSubtypeMjpg, 640, 480, UVC_FOURCC('M', 'J', 'P', 'G'), 24, 921600, 333333, 666666, 24576000, 147456000, 221184000, FALSE },
        { &UvcSubtypeMjpg, 320, 240, UVC_FOURCC('M', 'J', 'P', 'G'), 24, 230400, 333333, 666666, 6144000, 36864000, 55296000, FALSE },
    };
    static BYTE Buffer[64 * 1024];
    KSMULTIPLE_ITEM *Multiple = (PVOID)Buffer;
    PBYTE Item;
    DWORD Returned = 0, Error;
    ULONG Index;

    Error = UvcGetPinProperty(Filter, 0, KSPROPERTY_PIN_DATARANGES, Buffer, sizeof(Buffer), &Returned);
    ok(Error == ERROR_SUCCESS, "PIN_DATARANGES failed %lu\n", Error);
    if (Error)
        return;

    ok(Multiple->Count == 12, "Range count %lu\n", Multiple->Count);
    ok(Multiple->Size == 3704, "Range size %lu\n", Multiple->Size);
    ok(Returned == 3704, "Returned %lu\n", Returned);

    Item = (PBYTE)(Multiple + 1);
    for (Index = 0; Index < Multiple->Count && Index < 2 * ARRAYSIZE(Expect); Index++)
    {
        const KSDATARANGE *Range = (const KSDATARANGE *)Item;

        CheckRange(Range, &Expect[Index / 2], Index & 1);
        Item += (Range->FormatSize + 7) & ~7u;
    }

    Error = UvcGetPinProperty(Filter, 1, KSPROPERTY_PIN_DATARANGES, Buffer, sizeof(Buffer), &Returned);
    ok(Error == ERROR_SUCCESS, "Bridge PIN_DATARANGES failed %lu\n", Error);
    ok(Multiple->Count == 0 && Multiple->Size == sizeof(KSMULTIPLE_ITEM), "Bridge ranges %lu size %lu\n", Multiple->Count, Multiple->Size);
}

static void
CheckPin(HANDLE Filter, ULONG PinId, ULONG Possible, KSPIN_DATAFLOW ExpectFlow, KSPIN_COMMUNICATION ExpectComm, const GUID *Category, const WCHAR *ExpectName)
{
    KSPIN_CINSTANCES Instances = { 0 };
    KSPIN_DATAFLOW Flow = 0;
    KSPIN_COMMUNICATION Comm = 0;
    GUID Guid = { 0 };
    ULONG Necessary = 7;
    WCHAR Name[128] = L"";
    BYTE Buffer[512];
    KSMULTIPLE_ITEM *Multiple = (PVOID)Buffer;
    KSIDENTIFIER *Id = (PVOID)(Multiple + 1);
    DWORD Returned, Error;

    Error = UvcGetPinProperty(Filter, PinId, KSPROPERTY_PIN_CINSTANCES, &Instances, sizeof(Instances), &Returned);
    ok(Error == ERROR_SUCCESS && Instances.PossibleCount == Possible && Instances.CurrentCount == 0,
       "Pin %lu CINSTANCES err %lu possible %lu current %lu\n", PinId, Error, Instances.PossibleCount, Instances.CurrentCount);
    Error = UvcGetPinProperty(Filter, PinId, KSPROPERTY_PIN_NECESSARYINSTANCES, &Necessary, sizeof(Necessary), &Returned);
    ok(Error == ERROR_SUCCESS && Necessary == 0, "Pin %lu NECESSARY err %lu value %lu\n", PinId, Error, Necessary);
    Error = UvcGetPinProperty(Filter, PinId, KSPROPERTY_PIN_DATAFLOW, &Flow, sizeof(Flow), &Returned);
    ok(Error == ERROR_SUCCESS && Flow == ExpectFlow, "Pin %lu DATAFLOW err %lu value %d\n", PinId, Error, Flow);
    Error = UvcGetPinProperty(Filter, PinId, KSPROPERTY_PIN_COMMUNICATION, &Comm, sizeof(Comm), &Returned);
    ok(Error == ERROR_SUCCESS && Comm == ExpectComm, "Pin %lu COMMUNICATION err %lu value %d\n", PinId, Error, Comm);
    Error = UvcGetPinProperty(Filter, PinId, KSPROPERTY_PIN_CATEGORY, &Guid, sizeof(Guid), &Returned);
    ok(Error == ERROR_SUCCESS && IsEqualGUID(&Guid, Category), "Pin %lu CATEGORY err %lu value %s\n", PinId, Error, wine_dbgstr_guid(&Guid));

    Error = UvcGetPinProperty(Filter, PinId, KSPROPERTY_PIN_NAME, Name, sizeof(Name), &Returned);
    if (ExpectName)
        ok(Error == ERROR_SUCCESS && !wcscmp(Name, ExpectName), "Pin %lu NAME err %lu value '%ls'\n", PinId, Error, Name);
    else
        ok(Error == ERROR_FILE_NOT_FOUND, "Pin %lu NAME err %lu value '%ls'\n", PinId, Error, Name);

    Error = UvcGetPinProperty(Filter, PinId, KSPROPERTY_PIN_INTERFACES, Buffer, sizeof(Buffer), &Returned);
    ok(Error == ERROR_SUCCESS && Multiple->Count == 1 && IsEqualGUID(&Id->Set, &UvcInterfaceStandard) && Id->Id == 0,
       "Pin %lu INTERFACES err %lu\n", PinId, Error);
    Error = UvcGetPinProperty(Filter, PinId, KSPROPERTY_PIN_MEDIUMS, Buffer, sizeof(Buffer), &Returned);
    ok(Error == ERROR_SUCCESS && Multiple->Count == 1 && IsEqualGUID(&Id->Set, &UvcMediumStandard) && Id->Id == 0,
       "Pin %lu MEDIUMS err %lu\n", PinId, Error);
}

static void
CheckTopology(HANDLE Filter)
{
    const GUID *ExpectCategories[] = { &UvcCategoryVideo, &UvcCategoryCapture, &UvcCategoryCamera };
    const GUID *ExpectNodes[] = { &UvcNodeCamera, &UvcNodeProcessing, &UvcNodeExtension, &UvcNodeStreaming };
    static const KSTOPOLOGY_CONNECTION ExpectConnections[] =
    {
        { KSFILTER_NODE, 1, 0, 1 },
        { 0, 0, 1, 1 },
        { 1, 0, 2, 1 },
        { 2, 0, 3, 1 },
        { 3, 0, KSFILTER_NODE, 0 },
    };
    static BYTE Buffer[8192];
    KSMULTIPLE_ITEM *Multiple = (PVOID)Buffer;
    GUID *Guids = (PVOID)(Multiple + 1);
    DWORD Returned, Error;
    ULONG Index;

    Error = UvcGetProperty(Filter, &UvcPropsetTopology, KSPROPERTY_TOPOLOGY_CATEGORIES, Buffer, sizeof(Buffer), &Returned);
    ok(Error == ERROR_SUCCESS && Multiple->Count == ARRAYSIZE(ExpectCategories), "CATEGORIES err %lu count %lu\n", Error, Multiple->Count);
    for (Index = 0; !Error && Index < Multiple->Count && Index < ARRAYSIZE(ExpectCategories); Index++)
        ok(IsEqualGUID(&Guids[Index], ExpectCategories[Index]), "Category %lu is %s\n", Index, wine_dbgstr_guid(&Guids[Index]));

    Error = UvcGetProperty(Filter, &UvcPropsetTopology, KSPROPERTY_TOPOLOGY_NODES, Buffer, sizeof(Buffer), &Returned);
    ok(Error == ERROR_SUCCESS && Multiple->Count == ARRAYSIZE(ExpectNodes), "NODES err %lu count %lu\n", Error, Multiple->Count);
    for (Index = 0; !Error && Index < Multiple->Count && Index < ARRAYSIZE(ExpectNodes); Index++)
        ok(IsEqualGUID(&Guids[Index], ExpectNodes[Index]), "Node %lu is %s\n", Index, wine_dbgstr_guid(&Guids[Index]));

    Error = UvcGetProperty(Filter, &UvcPropsetTopology, KSPROPERTY_TOPOLOGY_CONNECTIONS, Buffer, sizeof(Buffer), &Returned);
    ok(Error == ERROR_SUCCESS && Multiple->Count == ARRAYSIZE(ExpectConnections), "CONNECTIONS err %lu count %lu\n", Error, Multiple->Count);
    for (Index = 0; !Error && Index < Multiple->Count && Index < ARRAYSIZE(ExpectConnections); Index++)
    {
        const KSTOPOLOGY_CONNECTION *Actual = (const KSTOPOLOGY_CONNECTION *)(Multiple + 1) + Index;

        ok(!memcmp(Actual, &ExpectConnections[Index], sizeof(*Actual)), "Connection %lu: %ld.%lu to %ld.%lu\n",
           Index, (LONG)Actual->FromNode, Actual->FromNodePin, (LONG)Actual->ToNode, Actual->ToNodePin);
    }
}

static void
CheckControl(HANDLE Filter, const GUID *Set, const EXPECT_CONTROL *Expect)
{
    BYTE Support[128], Reply[64];
    KSPROPERTY Query;
    KSPROPERTY_DESCRIPTION *Description = (PVOID)Support;
    KSPROPERTY_MEMBERSHEADER *Header = (PVOID)(Description + 1);
    KSPROPERTY_VIDEOPROCAMP_S *Value = (PVOID)Reply;
    BYTE Request[40] = { 0 };
    KSPROPERTY *Property = (PVOID)Request;
    DWORD Returned = 0;
    BOOL Result;

    Query.Set = *Set;
    Query.Id = Expect->Id;
    Query.Flags = KSPROPERTY_TYPE_BASICSUPPORT;
    ZeroMemory(Support, sizeof(Support));
    Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Query, sizeof(Query), Support, sizeof(Support), &Returned);
    ok(Result && Returned == 72, "Control %lu BASICSUPPORT err %lu ret %lu\n", Expect->Id, Result ? 0 : GetLastError(), Returned);
    if (Result)
    {
        KSPROPERTY_STEPPING_LONG *Step = (PVOID)(Header + 1);

        ok(Description->AccessFlags == (KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET), "Control %lu access %lx\n", Expect->Id, Description->AccessFlags);
        ok(Description->DescriptionSize == 72, "Control %lu size %lu\n", Expect->Id, Description->DescriptionSize);
        ok(Description->PropTypeSet.Id == 3, "Control %lu proptype %lu\n", Expect->Id, Description->PropTypeSet.Id);
        ok(Description->MembersListCount == 1, "Control %lu members %lu\n", Expect->Id, Description->MembersListCount);
        ok(Header->MembersFlags == KSPROPERTY_MEMBER_RANGES, "Control %lu member flags %lx\n", Expect->Id, Header->MembersFlags);
        ok(Header->MembersSize == sizeof(KSPROPERTY_STEPPING_LONG) && Header->MembersCount == 1 && Header->Flags == 0,
           "Control %lu header %lu %lu %lx\n", Expect->Id, Header->MembersSize, Header->MembersCount, Header->Flags);
        ok(Step->SteppingDelta == Expect->Step, "Control %lu step %lu, expected %lu\n", Expect->Id, Step->SteppingDelta, Expect->Step);
        ok(Step->Bounds.SignedMinimum == Expect->Min, "Control %lu min %ld, expected %ld\n", Expect->Id, Step->Bounds.SignedMinimum, Expect->Min);
        ok(Step->Bounds.SignedMaximum == Expect->Max, "Control %lu max %ld, expected %ld\n", Expect->Id, Step->Bounds.SignedMaximum, Expect->Max);
    }

    Query.Flags = KSPROPERTY_TYPE_DEFAULTVALUES;
    ZeroMemory(Support, sizeof(Support));
    Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Query, sizeof(Query), Support, sizeof(Support), &Returned);
    ok(Result && Returned == 60, "Control %lu DEFAULTVALUES err %lu ret %lu\n", Expect->Id, Result ? 0 : GetLastError(), Returned);
    if (Result)
    {
        ok(Description->DescriptionSize == 60, "Control %lu default size %lu\n", Expect->Id, Description->DescriptionSize);
        ok(Header->MembersFlags == KSPROPERTY_MEMBER_VALUES, "Control %lu default flags %lx\n", Expect->Id, Header->MembersFlags);
        ok(Header->MembersSize == sizeof(LONG) && Header->MembersCount == 1 && Header->Flags == KSPROPERTY_MEMBER_FLAG_DEFAULT,
           "Control %lu default header %lu %lu %lx\n", Expect->Id, Header->MembersSize, Header->MembersCount, Header->Flags);
        ok(*(LONG *)(Header + 1) == Expect->Default, "Control %lu default %ld, expected %ld\n", Expect->Id, *(LONG *)(Header + 1), Expect->Default);
    }

    Property->Set = *Set;
    Property->Id = Expect->Id;
    Property->Flags = KSPROPERTY_TYPE_GET;

    Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, Request, sizeof(Request), Reply, 36, &Returned);
    ok(!Result && GetLastError() == ERROR_INSUFFICIENT_BUFFER, "Control %lu GET with 36 bytes: result %d err %lu\n", Expect->Id, Result, GetLastError());

    FillMemory(Reply, sizeof(Reply), 0xCC);
    Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, Request, sizeof(Request), Reply, 40, &Returned);
    ok(Result && Returned == 40, "Control %lu GET err %lu ret %lu\n", Expect->Id, Result ? 0 : GetLastError(), Returned);
    if (Result)
    {
        ok(Value->Value == Expect->Default, "Control %lu value %ld, expected %ld\n", Expect->Id, Value->Value, Expect->Default);
        ok(Value->Flags == Expect->Flags, "Control %lu flags %lx, expected %lx\n", Expect->Id, Value->Flags, Expect->Flags);
        ok(Value->Capabilities == Expect->Capabilities, "Control %lu capabilities %lx, expected %lx\n", Expect->Id, Value->Capabilities, Expect->Capabilities);
    }
}

static void
CheckMissing(HANDLE Filter, const GUID *Set, const ULONG *Ids, ULONG Count)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        BYTE Data[64];
        KSPROPERTY Query;
        DWORD Returned;
        BOOL Result;

        Query.Set = *Set;
        Query.Id = Ids[Index];
        Query.Flags = KSPROPERTY_TYPE_BASICSUPPORT;
        Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Query, sizeof(Query), Data, sizeof(Data), &Returned);
        ok(!Result && GetLastError() == ERROR_NOT_FOUND, "Control %lu BASICSUPPORT result %d err %lu\n", Ids[Index], Result, GetLastError());
    }
}

static void
CheckSet(HANDLE Filter, const GUID *Set, ULONG Id, LONG NewValue, LONG Restore, ULONG RestoreFlags)
{
    BYTE Request[40] = { 0 }, Reply[40];
    KSPROPERTY *Property = (PVOID)Request;
    KSPROPERTY_VIDEOPROCAMP_S *Input = (PVOID)Reply;
    DWORD Returned = 0;
    BOOL Result;

    Property->Set = *Set;
    Property->Id = Id;
    Property->Flags = KSPROPERTY_TYPE_SET;
    ZeroMemory(Reply, sizeof(Reply));
    Input->Value = NewValue;
    Input->Flags = MANUAL_FLAG;
    Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, Request, sizeof(Request), Reply, sizeof(Reply), &Returned);
    ok(Result, "Control %lu SET failed %lu\n", Id, GetLastError());

    Property->Flags = KSPROPERTY_TYPE_GET;
    ZeroMemory(Reply, sizeof(Reply));
    Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, Request, sizeof(Request), Reply, sizeof(Reply), &Returned);
    ok(Result && Input->Value == NewValue && Input->Flags == MANUAL_FLAG, "Control %lu after SET: err %lu value %ld flags %lx\n",
       Id, Result ? 0 : GetLastError(), Input->Value, Input->Flags);

    Property->Flags = KSPROPERTY_TYPE_SET;
    ZeroMemory(Reply, sizeof(Reply));
    Input->Value = Restore;
    Input->Flags = MANUAL_FLAG;
    Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, Request, sizeof(Request), Reply, sizeof(Reply), &Returned);
    ok(Result, "Control %lu restore failed %lu\n", Id, GetLastError());

    if (RestoreFlags != MANUAL_FLAG)
    {
        ZeroMemory(Reply, sizeof(Reply));
        Input->Value = Restore;
        Input->Flags = RestoreFlags;
        Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, Request, sizeof(Request), Reply, sizeof(Reply), &Returned);
        ok(Result, "Control %lu automatic restore failed %lu\n", Id, GetLastError());

        Property->Flags = KSPROPERTY_TYPE_GET;
        ZeroMemory(Reply, sizeof(Reply));
        Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, Request, sizeof(Request), Reply, sizeof(Reply), &Returned);
        ok(Result && Input->Value == Restore && Input->Flags == RestoreFlags, "Control %lu restored: err %lu value %ld flags %lx\n",
           Id, Result ? 0 : GetLastError(), Input->Value, Input->Flags);
    }
}

static void
CheckPanTiltAndFocal(HANDLE Filter)
{
    KSPROPERTY Query;
    BYTE Support[64] = { 0 };
    KSPROPERTY_DESCRIPTION *Description = (PVOID)Support;
    DWORD Returned = 0;
    BOOL Result;

    Query.Set = UvcPropsetCamera;
    Query.Id = KSPROPERTY_CAMERACONTROL_PANTILT;
    Query.Flags = KSPROPERTY_TYPE_BASICSUPPORT;
    Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Query, sizeof(Query), Support, sizeof(Support), &Returned);
    ok(Result && Returned == sizeof(KSPROPERTY_DESCRIPTION), "PANTILT BASICSUPPORT err %lu ret %lu\n", Result ? 0 : GetLastError(), Returned);
    ok(Description->AccessFlags == (KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET), "PANTILT access %lx\n", Description->AccessFlags);
    ok(Description->MembersListCount == 0, "PANTILT members %lu\n", Description->MembersListCount);

    Query.Id = KSPROPERTY_CAMERACONTROL_FOCAL_LENGTH;
    ZeroMemory(Support, sizeof(Support));
    Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Query, sizeof(Query), Support, sizeof(Support), &Returned);
    ok(Result && Returned == sizeof(KSPROPERTY_DESCRIPTION), "FOCAL_LENGTH BASICSUPPORT err %lu ret %lu\n", Result ? 0 : GetLastError(), Returned);
    ok(Description->AccessFlags == KSPROPERTY_TYPE_GET, "FOCAL_LENGTH access %lx\n", Description->AccessFlags);
}

static void
CheckExtensionUnit(HANDLE Filter)
{
    ULONG Node;

    for (Node = 0; Node < 4; Node++)
    {
        KSP_NODE Request;
        BYTE Data[16] = { 0 };
        DWORD Returned = 0;
        BOOL Result;

        ZeroMemory(&Request, sizeof(Request));
        Request.Property.Set = UvcExtensionUnit;
        Request.Property.Id = 1;
        Request.Property.Flags = KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_TOPOLOGY;
        Request.NodeId = Node;
        Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Request, sizeof(Request), Data, sizeof(Data), &Returned);
        if (Node == 2)
        {
            ok(Result && Returned == 4 && *(ULONG *)Data == 0x12345678, "XU GET err %lu ret %lu value %08lx\n", Result ? 0 : GetLastError(), Returned, *(ULONG *)Data);

            Returned = 0;
            Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Request, sizeof(Request), NULL, 0, &Returned);
            ok(!Result && GetLastError() == ERROR_MORE_DATA && Returned == 4, "XU size query result %d err %lu ret %lu\n", Result, GetLastError(), Returned);
        }
        else
        {
            ok(!Result && GetLastError() == ERROR_SET_NOT_FOUND, "XU on node %lu result %d err %lu\n", Node, Result, GetLastError());
        }
    }
}

START_TEST(KsFilter)
{
    WCHAR Capture[512], Video[512], Camera[512];
    BOOL HasCapture, HasVideo, HasCamera;
    HANDLE Filter;
    ULONG Pins = 0, Index;
    DWORD Returned, Error;
    KSCOMPONENTID Component;

    HasCapture = UvcFindCamera(&UvcCategoryCapture, Capture, ARRAYSIZE(Capture));
    HasVideo = UvcFindCamera(&UvcCategoryVideo, Video, ARRAYSIZE(Video));
    HasCamera = UvcFindCamera(&UvcCategoryCamera, Camera, ARRAYSIZE(Camera));
    if (!HasCapture)
    {
        skip("No usb-uvc test camera\n");
        return;
    }
    ok(HasVideo, "Camera is not in KSCATEGORY_VIDEO\n");
    ok(HasCamera, "Camera is not in KSCATEGORY_VIDEO_CAMERA\n");
    ok(wcsstr(Capture, L"\\global") != NULL, "Capture interface %ls\n", Capture);

    Filter = UvcOpenFilter(Capture);
    ok(Filter != INVALID_HANDLE_VALUE, "Opening the filter failed %lu\n", GetLastError());
    if (Filter == INVALID_HANDLE_VALUE)
        return;

    Error = UvcGetPinProperty(Filter, 0, KSPROPERTY_PIN_CTYPES, &Pins, sizeof(Pins), &Returned);
    ok(Error == ERROR_SUCCESS && Pins == 2, "PIN_CTYPES err %lu value %lu\n", Error, Pins);

    {
        ULONG Short = 0;

        Error = UvcGetPinProperty(Filter, 0, KSPROPERTY_PIN_CINSTANCES, &Short, sizeof(Short), &Returned);
        ok(Error == ERROR_INSUFFICIENT_BUFFER, "PIN_CINSTANCES with 4 bytes: err %lu\n", Error);
        Returned = 0;
        Error = UvcGetPinProperty(Filter, 0, KSPROPERTY_PIN_CINSTANCES, NULL, 0, &Returned);
        ok(Error == ERROR_INSUFFICIENT_BUFFER && Returned == 0, "PIN_CINSTANCES with no buffer: err %lu ret %lu\n", Error, Returned);
    }

    CheckPin(Filter, 0, 1, KSPIN_DATAFLOW_OUT, KSPIN_COMMUNICATION_BOTH, &UvcPinCapture, L"Capture");
    CheckPin(Filter, 1, 0, KSPIN_DATAFLOW_IN, KSPIN_COMMUNICATION_BRIDGE, &UvcPinBridge, NULL);
    CheckRanges(Filter);
    CheckTopology(Filter);

    ZeroMemory(&Component, sizeof(Component));
    Error = UvcGetProperty(Filter, &UvcPropsetGeneral, KSPROPERTY_GENERAL_COMPONENTID, &Component, sizeof(Component), &Returned);
    ok(Error == ERROR_SUCCESS && Returned == sizeof(Component), "COMPONENTID err %lu ret %lu\n", Error, Returned);
    ok(IsEqualGUID(&Component.Component, &UvcComponentCamera), "Component %s\n", wine_dbgstr_guid(&Component.Component));
    ok(Component.Version == 1 && Component.Revision == 0, "Version %lu.%lu\n", Component.Version, Component.Revision);
    trace("Manufacturer %s product %s\n", wine_dbgstr_guid(&Component.Manufacturer), wine_dbgstr_guid(&Component.Product));

    for (Index = 0; Index < ARRAYSIZE(ExpectProcAmp); Index++)
        CheckControl(Filter, &UvcPropsetProcAmp, &ExpectProcAmp[Index]);
    CheckMissing(Filter, &UvcPropsetProcAmp, MissingProcAmp, ARRAYSIZE(MissingProcAmp));

    for (Index = 0; Index < ARRAYSIZE(ExpectCamera); Index++)
        CheckControl(Filter, &UvcPropsetCamera, &ExpectCamera[Index]);
    CheckMissing(Filter, &UvcPropsetCamera, MissingCamera, ARRAYSIZE(MissingCamera));

    CheckSet(Filter, &UvcPropsetProcAmp, 0, 12, 0, MANUAL_FLAG);
    CheckSet(Filter, &UvcPropsetProcAmp, 2, 300, 0, MANUAL_FLAG);
    CheckSet(Filter, &UvcPropsetProcAmp, 7, 5000, 4600, AUTO_FLAG);
    CheckSet(Filter, &UvcPropsetCamera, 0, 3, 0, MANUAL_FLAG);
    CheckSet(Filter, &UvcPropsetCamera, 4, -3, -6, AUTO_FLAG);

    CheckPanTiltAndFocal(Filter);
    CheckExtensionUnit(Filter);

    CloseHandle(Filter);
}
