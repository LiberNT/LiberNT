/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for USB video class controls, still capture and camera button events
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lntuvc_test.h"
#include <stdlib.h>

static const GUID UvcPropsetVideoControl = { 0x6a2e0670, 0x28e4, 0x11d0, { 0xa1, 0x8c, 0x00, 0xa0, 0xc9, 0x11, 0x89, 0x56 } };
static const GUID UvcEventsetVidcapToSti = { 0xdb47de20, 0xf628, 0x11d1, { 0xba, 0x41, 0x00, 0xa0, 0xc9, 0x0d, 0x2b, 0x05 } };
static const GUID UvcPinStill = { 0xfb6c428a, 0x0353, 0x11d1, { 0x90, 0x5f, 0x00, 0x00, 0xc0, 0xcc, 0x16, 0xba } };

typedef struct _EXPECT_RANGE
{
    ULONG Id;
    LONG Min;
    LONG Max;
    ULONG Step;
    LONG Default;
    ULONG Caps;
} EXPECT_RANGE;

static const EXPECT_RANGE ExpectProcAmpRanges[] =
{
    { KSPROPERTY_VIDEOPROCAMP_BRIGHTNESS, -64, 64, 1, 0, 2 },
    { KSPROPERTY_VIDEOPROCAMP_CONTRAST, 0, 95, 1, 32, 2 },
    { KSPROPERTY_VIDEOPROCAMP_HUE, -2000, 2000, 100, 0, 3 },
    { KSPROPERTY_VIDEOPROCAMP_SATURATION, 0, 100, 1, 64, 2 },
    { KSPROPERTY_VIDEOPROCAMP_SHARPNESS, 1, 7, 1, 3, 2 },
    { KSPROPERTY_VIDEOPROCAMP_GAMMA, 100, 300, 1, 100, 2 },
    { KSPROPERTY_VIDEOPROCAMP_WHITEBALANCE, 2800, 6500, 10, 4600, 3 },
    { KSPROPERTY_VIDEOPROCAMP_BACKLIGHT_COMPENSATION, 0, 2, 1, 1, 2 },
    { KSPROPERTY_VIDEOPROCAMP_GAIN, 0, 100, 1, 0, 2 },
    { KSPROPERTY_VIDEOPROCAMP_DIGITAL_MULTIPLIER, 100, 400, 1, 100, 0 },
    { KSPROPERTY_VIDEOPROCAMP_DIGITAL_MULTIPLIER_LIMIT, 100, 400, 1, 400, 0 },
    { KSPROPERTY_VIDEOPROCAMP_POWERLINE_FREQUENCY, 0, 2, 1, 1, 0 },
};

static const EXPECT_RANGE ExpectCameraRanges[] =
{
    { KSPROPERTY_CAMERACONTROL_PAN, -10, 10, 1, 0, 2 },
    { KSPROPERTY_CAMERACONTROL_TILT, -5, 5, 1, 0, 2 },
    { KSPROPERTY_CAMERACONTROL_ROLL, -180, 180, 1, 0, 2 },
    { KSPROPERTY_CAMERACONTROL_ZOOM, 100, 500, 1, 100, 2 },
    { KSPROPERTY_CAMERACONTROL_EXPOSURE, -13, 0, 1, -6, 3 },
    { KSPROPERTY_CAMERACONTROL_IRIS, 0, 16000, 100, 2000, 3 },
    { KSPROPERTY_CAMERACONTROL_FOCUS, 0, 250, 5, 0, 3 },
    { KSPROPERTY_CAMERACONTROL_SCANMODE, 0, 1, 1, 0, 0 },
    { KSPROPERTY_CAMERACONTROL_PRIVACY, 0, 1, 1, 0, 0 },
    { KSPROPERTY_CAMERACONTROL_PAN_RELATIVE, 0, 4, 1, 0, 0 },
    { KSPROPERTY_CAMERACONTROL_TILT_RELATIVE, 0, 4, 1, 0, 0 },
    { KSPROPERTY_CAMERACONTROL_ROLL_RELATIVE, 0, 1, 1, 0, 0 },
    { KSPROPERTY_CAMERACONTROL_ZOOM_RELATIVE, 0, 4, 1, 0, 2 },
    { KSPROPERTY_CAMERACONTROL_EXPOSURE_RELATIVE, 0, 255, 1, 0, 3 },
    { KSPROPERTY_CAMERACONTROL_IRIS_RELATIVE, 0, 255, 1, 0, 3 },
    { KSPROPERTY_CAMERACONTROL_FOCUS_RELATIVE, 0, 1, 1, 0, 3 },
    { KSPROPERTY_CAMERACONTROL_AUTO_EXPOSURE_PRIORITY, 0, 1, 1, 0, 0 },
};

typedef struct _EXPECT_SET
{
    ULONG Id;
    LONG Value;
    ULONG Flags;
    LONG ReadValue;
    ULONG ReadFlags;
} EXPECT_SET;

static const EXPECT_SET ExpectCameraSets[] =
{
    { KSPROPERTY_CAMERACONTROL_EXPOSURE_RELATIVE, 1, 0x12, 1, 2 },
    { KSPROPERTY_CAMERACONTROL_ZOOM_RELATIVE, 1, 0x12, 1, 2 },
    { KSPROPERTY_CAMERACONTROL_PAN_RELATIVE, 1, 0x12, 1, 0 },
    { KSPROPERTY_CAMERACONTROL_TILT_RELATIVE, 1, 0x12, 1, 0 },
    { KSPROPERTY_CAMERACONTROL_ROLL_RELATIVE, 1, 0x12, 1, 0 },
    { KSPROPERTY_CAMERACONTROL_FOCUS_RELATIVE, 1, 0x12, 1, 2 },
    { KSPROPERTY_CAMERACONTROL_IRIS_RELATIVE, 1, 0x12, 1, 2 },
    { KSPROPERTY_CAMERACONTROL_EXPOSURE_RELATIVE, -1, 0x12, 255, 2 },
    { KSPROPERTY_CAMERACONTROL_ZOOM_RELATIVE, -1, 0x12, -1, 2 },
    { KSPROPERTY_CAMERACONTROL_ZOOM_RELATIVE, 3, 0x12, 3, 2 },
    { KSPROPERTY_CAMERACONTROL_PAN_RELATIVE, -1, 0x12, 1, 0 },
    { KSPROPERTY_CAMERACONTROL_PAN_RELATIVE, 3, 0x12, 3, 0 },
    { KSPROPERTY_CAMERACONTROL_TILT_RELATIVE, 3, 0x12, 3, 0 },
    { KSPROPERTY_CAMERACONTROL_FOCUS_RELATIVE, -1, 0x12, -1, 2 },
    { KSPROPERTY_CAMERACONTROL_IRIS, 300, 2, 300, 2 },
    { KSPROPERTY_CAMERACONTROL_IRIS, 300, 1, 300, 1 },
    { KSPROPERTY_CAMERACONTROL_EXPOSURE, -5, 2, -5, 2 },
    { KSPROPERTY_CAMERACONTROL_EXPOSURE_RELATIVE, 1, 0x11, 255, 1 },
    { KSPROPERTY_CAMERACONTROL_FOCUS_RELATIVE, 1, 0x11, -1, 1 },
    { KSPROPERTY_CAMERACONTROL_ROLL, 5, 2, 5, 2 },
    { KSPROPERTY_CAMERACONTROL_SCANMODE, 1, 2, 1, 0 },
    { KSPROPERTY_CAMERACONTROL_PRIVACY, 1, 2, 1, 0 },
    { KSPROPERTY_CAMERACONTROL_AUTO_EXPOSURE_PRIORITY, 1, 2, 1, 0 },
};

typedef struct _UVC_CONTROL_REQUEST
{
    KSPROPERTY Property;
    LONG Value;
    ULONG Flags;
    ULONG Capabilities;
} UVC_CONTROL_REQUEST;

typedef struct _UVC_NODE_REQUEST
{
    KSP_NODE Node;
    LONG Value;
    ULONG Flags;
    ULONG Capabilities;
    ULONG Dummy;
} UVC_NODE_REQUEST;

typedef struct _UVC_CAPS_REQUEST
{
    KSPROPERTY Property;
    ULONG StreamIndex;
    LONG Value;
} UVC_CAPS_REQUEST;

typedef struct _UVC_READ_REQUEST
{
    KSSTREAM_HEADER Header;
    KS_FRAME_INFO FrameInfo;
} UVC_READ_REQUEST;

static int
CompareGuid(const void *A, const void *B)
{
    return memcmp(A, B, sizeof(GUID));
}

static void
DumpSets(HANDLE Handle, const char *Where, DWORD Code, const char *What, BOOL Probe)
{
    KSIDENTIFIER Query;
    GUID Sets[64];
    DWORD Returned = 0;
    BOOL Result;
    ULONG Index, Count;

    ZeroMemory(&Query, sizeof(Query));
    Query.Flags = KSPROPERTY_TYPE_SETSUPPORT;
    Result = UvcIoctl(Handle, Code, &Query, sizeof(Query), Sets, sizeof(Sets), &Returned);
    Count = Result ? Returned / sizeof(GUID) : 0;
    trace("MEASURE sets %s %s status %lu count %lu\n", Where, What, Result ? 0 : GetLastError(), Count);
    qsort(Sets, Count, sizeof(GUID), CompareGuid);
    for (Index = 0; Index < Count; Index++)
    {
        trace("MEASURE sets %s %s %s\n", Where, What, wine_dbgstr_guid(&Sets[Index]));
        if (Probe)
        {
            ULONG Id;

            for (Id = 0; Id < 32; Id++)
            {
                KSPROPERTY Property;
                ULONG Access = 0;

                Property.Set = Sets[Index];
                Property.Id = Id;
                Property.Flags = KSPROPERTY_TYPE_BASICSUPPORT;
                Returned = 0;
                Result = UvcIoctl(Handle, Code, &Property, sizeof(Property), &Access, sizeof(Access), &Returned);
                if (Result || GetLastError() != ERROR_NOT_FOUND)
                    trace("MEASURE sets %s %s id %lu status %lu access %lx\n", Where, What, Id, Result ? 0 : GetLastError(), Access);
            }
        }
    }
}

#define UVC_NO_CHECK MAXDWORD

static void
DumpDroppedFrames(HANDLE Handle, const char *Where, DWORD Expect)
{
    static const GUID Set = { 0xc6e13344, 0x30ac, 0x11d0, { 0xa1, 0x8c, 0x00, 0xa0, 0xc9, 0x11, 0x89, 0x56 } };
    struct
    {
        KSPROPERTY Property;
        LONGLONG PictureNumber;
        LONGLONG DropCount;
        ULONG AverageFrameSize;
    } Request, Reply;
    DWORD Returned = 0, Status;
    BOOL Result;

    ZeroMemory(&Request, sizeof(Request));
    ZeroMemory(&Reply, sizeof(Reply));
    Request.Property.Set = Set;
    Request.Property.Id = 0;
    Request.Property.Flags = KSPROPERTY_TYPE_GET;
    Result = UvcIoctl(Handle, IOCTL_KS_PROPERTY, &Request, sizeof(Request), &Reply, sizeof(Reply), &Returned);
    Status = Result ? 0 : GetLastError();
    trace("MEASURE dropped %s status %lu returned %lu picture %I64d drop %I64d size %lu\n", Where, Status, Returned,
          Reply.PictureNumber, Reply.DropCount, Reply.AverageFrameSize);
    if (Expect == UVC_NO_CHECK)
        return;
    if (Expect)
        ok(Status == Expect && !Returned, "Dropped frames on the %s: status %lu returned %lu\n", Where, Status, Returned);
    else
        ok(!Status && Returned == sizeof(Reply) && !Reply.PictureNumber && !Reply.DropCount && Reply.AverageFrameSize == 614400,
           "Dropped frames on the %s: status %lu returned %lu picture %I64d drop %I64d size %lu\n", Where, Status, Returned,
           Reply.PictureNumber, Reply.DropCount, Reply.AverageFrameSize);
}

static const GUID UvcPropsetUnknownB = { 0xb03a874b, 0xd32b, 0x4213, { 0xac, 0x38, 0x25, 0xa7, 0x18, 0xe4, 0x45, 0x4f } };
static const GUID UvcPropsetUnknownA = { 0xa60d8368, 0x5324, 0x4893, { 0xb0, 0x20, 0xc4, 0x31, 0xa5, 0x0b, 0xcb, 0xe3 } };
static const GUID UvcPropsetExtendedCamera = { 0x1cb79112, 0xc0d2, 0x4213, { 0x9c, 0xa6, 0xcd, 0x4f, 0xdb, 0x92, 0x79, 0x72 } };
static const GUID UvcPropsetMemoryTransport = { 0x0a3d1c5d, 0x5243, 0x4819, { 0x9e, 0xd0, 0xae, 0xe8, 0x04, 0x4c, 0xee, 0x2b } };
static const GUID UvcPropsetMdlCacheClear = { 0xbd718a7b, 0x97fc, 0x40c7, { 0x88, 0xce, 0xd3, 0xff, 0x06, 0xf5, 0x5b, 0x16 } };
static const GUID UvcPropsetStreamInterface = { 0x1fdd8ee1, 0x9cd3, 0x11d0, { 0x82, 0xaa, 0x00, 0x00, 0xf8, 0x22, 0xfe, 0x8a } };
static const GUID UvcPropsetDroppedFrames = { 0xc6e13344, 0x30ac, 0x11d0, { 0xa1, 0x8c, 0x00, 0xa0, 0xc9, 0x11, 0x89, 0x56 } };
static const GUID UvcEventsetDevice = { 0x288296ec, 0x9f94, 0x41b4, { 0xa1, 0x53, 0xaa, 0x31, 0xae, 0xec, 0xb3, 0x3f } };
static const GUID UvcEventsetConnection = { 0x7f4bcbe0, 0x9ea5, 0x11cf, { 0xa5, 0xd6, 0x28, 0xdb, 0x04, 0xc1, 0x00, 0x00 } };

#define UVC_SECURE_MODE 36

typedef struct _UVC_GET_EXPECT
{
    const GUID *Set;
    ULONG Id;
    ULONG Flags;
    ULONG OutSize;
    DWORD Status;
    DWORD Returned;
    DWORD Value;
} UVC_GET_EXPECT;

static DWORD
DumpGet(HANDLE Handle, const char *Where, const GUID *Set, ULONG Id, ULONG Flags, ULONG OutSize, BOOL Quiet, DWORD *Returned, DWORD *Value)
{
    KSPROPERTY Property;
    BYTE Data[256];
    DWORD Status;
    BOOL Result;

    ZeroMemory(Data, sizeof(Data));
    *Returned = 0;
    Property.Set = *Set;
    Property.Id = Id;
    Property.Flags = Flags;
    Result = UvcIoctl(Handle, IOCTL_KS_PROPERTY, &Property, sizeof(Property), OutSize ? Data : NULL, OutSize, Returned);
    Status = Result ? 0 : GetLastError();
    memcpy(Value, Data, sizeof(*Value));
    if (Quiet && Status == ERROR_NOT_FOUND)
        return Status;
    trace("MEASURE get %s %s id %lu flags %lx out %lu status %lu returned %lu data %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n",
          Where, wine_dbgstr_guid(Set), Id, Flags, OutSize, Status, *Returned,
          Data[0], Data[1], Data[2], Data[3], Data[4], Data[5], Data[6], Data[7],
          Data[8], Data[9], Data[10], Data[11], Data[12], Data[13], Data[14], Data[15]);
    return Status;
}

static void
CheckGets(HANDLE Handle, const char *Where, const UVC_GET_EXPECT *Expect, ULONG Count)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        DWORD Returned, Value = 0, Status;

        Status = DumpGet(Handle, Where, Expect[Index].Set, Expect[Index].Id, Expect[Index].Flags, Expect[Index].OutSize, FALSE, &Returned, &Value);
        ok(Status == Expect[Index].Status && Returned == Expect[Index].Returned &&
           (Expect[Index].Status || Returned < sizeof(DWORD) || Expect[Index].Value == UVC_NO_CHECK || Value == Expect[Index].Value),
           "%s %s id %lu flags %lx out %lu: status %lu returned %lu value %lx\n", Where, wine_dbgstr_guid(Expect[Index].Set),
           Expect[Index].Id, Expect[Index].Flags, Expect[Index].OutSize, Status, Returned, Value);
    }
}

static DWORD
DumpSet(HANDLE Handle, const char *Where, const GUID *Set, ULONG Id, const void *Value, ULONG Size)
{
    KSPROPERTY Property;
    BYTE Data[64];
    DWORD Returned = 0;
    BOOL Result;

    memcpy(Data, Value, Size);
    Property.Set = *Set;
    Property.Id = Id;
    Property.Flags = KSPROPERTY_TYPE_SET;
    Result = UvcIoctl(Handle, IOCTL_KS_PROPERTY, &Property, sizeof(Property), Data, Size, &Returned);
    trace("MEASURE set %s %s id %lu size %lu status %lu returned %lu\n", Where, wine_dbgstr_guid(Set), Id, Size,
          Result ? 0 : GetLastError(), Returned);
    return Result ? 0 : GetLastError();
}

static void
CheckEventSupport(HANDLE Handle, const char *Where, const GUID *Set, const DWORD *ExpectSupport, const DWORD *ExpectEnable)
{
    ULONG Id;

    for (Id = 0; Id < 8; Id++)
    {
        KSEVENT Event;
        KSEVENTDATA Data;
        HANDLE Signal;
        ULONG Support = 0;
        DWORD Returned = 0, Ignored = 0, Status;
        BOOL Result;

        Event.Set = *Set;
        Event.Id = Id;
        Event.Flags = KSEVENT_TYPE_BASICSUPPORT;
        Result = UvcIoctl(Handle, IOCTL_KS_ENABLE_EVENT, &Event, sizeof(Event), &Support, sizeof(Support), &Returned);
        Status = Result ? 0 : GetLastError();
        ok(Status == ExpectSupport[Id] && !Returned, "%s event %s id %lu support: status %lu returned %lu\n", Where, wine_dbgstr_guid(Set),
           Id, Status, Returned);

        Signal = CreateEventW(NULL, TRUE, FALSE, NULL);
        ZeroMemory(&Data, sizeof(Data));
        Data.NotificationType = KSEVENTF_EVENT_HANDLE;
        Data.EventHandle.Event = Signal;
        Event.Flags = KSEVENT_TYPE_ENABLE;
        Returned = 0;
        Result = UvcIoctl(Handle, IOCTL_KS_ENABLE_EVENT, &Event, sizeof(Event), &Data, sizeof(Data), &Returned);
        Status = Result ? 0 : GetLastError();
        ok(Status == ExpectEnable[Id] && !Returned, "%s event %s id %lu enable: status %lu returned %lu\n", Where, wine_dbgstr_guid(Set),
           Id, Status, Returned);
        if (Result)
            UvcIoctl(Handle, IOCTL_KS_DISABLE_EVENT, &Data, sizeof(Data), NULL, 0, &Ignored);
        CloseHandle(Signal);
    }
}

static void
ProbeFilterSets(HANDLE Filter, BOOL AllControls)
{
    static const DWORD StiEvents[8] = { 0, 0, 0, ERROR_NOT_FOUND, ERROR_NOT_FOUND, ERROR_NOT_FOUND, ERROR_NOT_FOUND, ERROR_NOT_FOUND };
    static const DWORD DeviceEvents[8] = { 0, 0, 0, 0, ERROR_NOT_FOUND, ERROR_NOT_FOUND, ERROR_NOT_FOUND, ERROR_NOT_FOUND };
    static const DWORD NoSet[8] = { ERROR_SET_NOT_FOUND, ERROR_SET_NOT_FOUND, ERROR_SET_NOT_FOUND, ERROR_SET_NOT_FOUND,
                                    ERROR_SET_NOT_FOUND, ERROR_SET_NOT_FOUND, ERROR_SET_NOT_FOUND, ERROR_SET_NOT_FOUND };
    static const UVC_GET_EXPECT Expect[] =
    {
        { &UvcPropsetDroppedFrames, 0, KSPROPERTY_TYPE_BASICSUPPORT, 4, 0, 4, KSPROPERTY_TYPE_GET },
        { &UvcPropsetExtendedCamera, UVC_SECURE_MODE, KSPROPERTY_TYPE_GET, 256, ERROR_INVALID_PARAMETER, 0, 0 },
        { &UvcPropsetExtendedCamera, UVC_SECURE_MODE, KSPROPERTY_TYPE_BASICSUPPORT, 256, 0, sizeof(KSPROPERTY_DESCRIPTION),
          KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET },
    };
    static const UVC_GET_EXPECT Secure[] =
    {
        { &UvcPropsetExtendedCamera, UVC_SECURE_MODE, KSPROPERTY_TYPE_GET, 0, ERROR_MORE_DATA, 40, 0 },
        { &UvcPropsetExtendedCamera, UVC_SECURE_MODE, KSPROPERTY_TYPE_GET, 32, ERROR_INSUFFICIENT_BUFFER, 0, 0 },
        { &UvcPropsetExtendedCamera, UVC_SECURE_MODE, KSPROPERTY_TYPE_GET, 40, ERROR_INVALID_PARAMETER, 0, 0 },
        { &UvcPropsetExtendedCamera, UVC_SECURE_MODE, KSPROPERTY_TYPE_GET, 48, ERROR_INVALID_PARAMETER, 0, 0 },
    };
    BYTE Zero[64] = { 0 };
    DWORD Returned, Value;
    ULONG Id;

    DumpGet(Filter, "filter", &UvcPropsetUnknownB, 0, KSPROPERTY_TYPE_GET, 256, FALSE, &Returned, &Value);
    DumpGet(Filter, "filter", &UvcPropsetUnknownA, 0, KSPROPERTY_TYPE_GET, 4, FALSE, &Returned, &Value);
    DumpGet(Filter, "filter", &UvcEventsetVidcapToSti, 0, KSPROPERTY_TYPE_GET, 256, FALSE, &Returned, &Value);
    CheckGets(Filter, "filter", Expect, ARRAYSIZE(Expect));
    ok(DumpSet(Filter, "filter", &UvcPropsetExtendedCamera, UVC_SECURE_MODE, Zero, sizeof(Zero)) == ERROR_INVALID_PARAMETER,
       "Secure mode set with 64 bytes\n");
    ok(DumpSet(Filter, "filter", &UvcPropsetExtendedCamera, UVC_SECURE_MODE, Zero, 4) == ERROR_INSUFFICIENT_BUFFER,
       "Secure mode set with 4 bytes\n");
    CheckGets(Filter, "filter secure", Secure, ARRAYSIZE(Secure));
    {
        struct
        {
            ULONG Version;
            ULONG PinId;
            ULONG Size;
            ULONG Result;
            ULONGLONG Flags;
            ULONGLONG Capability;
            ULONGLONG Value;
        } SecureValue = { 1, 0xFFFFFFFF, 40, 0, 0, 0, 0 };

        ok(DumpSet(Filter, "filter secure valid", &UvcPropsetExtendedCamera, UVC_SECURE_MODE, &SecureValue, sizeof(SecureValue)) == ERROR_INVALID_PARAMETER,
           "Secure mode set with a filter-scope header\n");
        ok(DumpSet(Filter, "filter secure header", &UvcPropsetExtendedCamera, UVC_SECURE_MODE, &SecureValue, 32) == ERROR_INSUFFICIENT_BUFFER,
           "Secure mode set with only a header\n");
    }
    for (Id = 0; Id < 64; Id++)
    {
        DWORD Get, Support;

        if (Id == UVC_SECURE_MODE)
            continue;
        Get = DumpGet(Filter, "filter", &UvcPropsetExtendedCamera, Id, KSPROPERTY_TYPE_GET, 256, TRUE, &Returned, &Value);
        Support = DumpGet(Filter, "filter", &UvcPropsetExtendedCamera, Id, KSPROPERTY_TYPE_BASICSUPPORT, 256, TRUE, &Returned, &Value);
        ok(Get == ERROR_NOT_FOUND && Support == ERROR_NOT_FOUND, "Extended camera control %lu: get %lu support %lu\n", Id, Get, Support);
    }
    if (AllControls)
    {
        for (Id = KSPROPERTY_CAMERACONTROL_PAN_RELATIVE; Id <= KSPROPERTY_CAMERACONTROL_AUTO_EXPOSURE_PRIORITY; Id++)
        {
            UVC_GET_EXPECT Camera[3] =
            {
                { &UvcPropsetCamera, Id, KSPROPERTY_TYPE_BASICSUPPORT, 4, 0, 4, KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET },
                { &UvcPropsetCamera, Id, KSPROPERTY_TYPE_BASICSUPPORT, 64, ERROR_INSUFFICIENT_BUFFER, 0, 0 },
                { &UvcPropsetCamera, Id, KSPROPERTY_TYPE_GET, 256, 0, sizeof(KSPROPERTY_CAMERACONTROL_S), UVC_NO_CHECK },
            };

            if (Id == KSPROPERTY_CAMERACONTROL_PANTILT_RELATIVE)
            {
                Camera[0].Status = Camera[1].Status = Camera[2].Status = ERROR_NOT_FOUND;
                Camera[0].Returned = Camera[2].Returned = 0;
            }
            else if (Id == KSPROPERTY_CAMERACONTROL_FOCAL_LENGTH)
            {
                Camera[0].Value = KSPROPERTY_TYPE_GET;
                Camera[1].Status = 0;
                Camera[1].Returned = sizeof(KSPROPERTY_DESCRIPTION);
                Camera[1].Value = KSPROPERTY_TYPE_GET;
            }
            CheckGets(Filter, "filter camera", Camera, ARRAYSIZE(Camera));
        }
    }
    for (Id = 0; Id < 16; Id++)
    {
        static const GUID PinSet = { 0x8c134960, 0x51ad, 0x11cf, { 0x87, 0x8a, 0x94, 0xf8, 0x01, 0xc1, 0x00, 0x00 } };
        BOOL Listed = Id <= KSPROPERTY_PIN_COMMUNICATION || Id == KSPROPERTY_PIN_NECESSARYINSTANCES ||
                      Id == KSPROPERTY_PIN_CATEGORY || Id == KSPROPERTY_PIN_NAME;
        DWORD Status = DumpGet(Filter, "filter pin", &PinSet, Id, KSPROPERTY_TYPE_BASICSUPPORT, 4, TRUE, &Returned, &Value);

        ok(Listed ? (!Status && Value == KSPROPERTY_TYPE_GET) : Status == ERROR_NOT_FOUND, "Pin property %lu: status %lu access %lx\n", Id, Status, Value);
    }
    CheckEventSupport(Filter, "filter", &UvcEventsetVidcapToSti, StiEvents, StiEvents);
    CheckEventSupport(Filter, "filter", &UvcEventsetDevice, DeviceEvents, DeviceEvents);
    CheckEventSupport(Filter, "filter", &UvcEventsetConnection, NoSet, NoSet);
}

static void
ProbePinSets(HANDLE Pin, const char *Where)
{
    static const DWORD ConnectionSupport[8] = { 0, 0, 0, 0, ERROR_NOT_FOUND, ERROR_NOT_FOUND, ERROR_NOT_FOUND, ERROR_NOT_FOUND };
    static const DWORD ConnectionEnable[8] = { 0, 0, 0, 0, 0, ERROR_NOT_FOUND, ERROR_NOT_FOUND, ERROR_NOT_FOUND };
    static const DWORD NoSet[8] = { ERROR_SET_NOT_FOUND, ERROR_SET_NOT_FOUND, ERROR_SET_NOT_FOUND, ERROR_SET_NOT_FOUND,
                                    ERROR_SET_NOT_FOUND, ERROR_SET_NOT_FOUND, ERROR_SET_NOT_FOUND, ERROR_SET_NOT_FOUND };
    UVC_GET_EXPECT Expect[] =
    {
        { &UvcPropsetStreamInterface, 0, KSPROPERTY_TYPE_GET, 4, 0, 4, sizeof(void *) == 8 ? 72 : UVC_NO_CHECK },
        { &UvcPropsetStreamInterface, 0, KSPROPERTY_TYPE_BASICSUPPORT, 4, 0, 4, KSPROPERTY_TYPE_GET },
        { &UvcPropsetDroppedFrames, 0, KSPROPERTY_TYPE_BASICSUPPORT, 4, 0, 4, KSPROPERTY_TYPE_GET },
        { &UvcPropsetMemoryTransport, 0, KSPROPERTY_TYPE_GET, 4, ERROR_NOT_FOUND, 0, 0 },
        { &UvcPropsetMemoryTransport, 0, KSPROPERTY_TYPE_BASICSUPPORT, 4, ERROR_NOT_FOUND, 0, 0 },
        { &UvcPropsetMemoryTransport, 1, KSPROPERTY_TYPE_GET, 4, ERROR_NOT_FOUND, 0, 0 },
        { &UvcPropsetMemoryTransport, 1, KSPROPERTY_TYPE_BASICSUPPORT, 4, 0, 4, KSPROPERTY_TYPE_SET },
        { &UvcPropsetMemoryTransport, 2, KSPROPERTY_TYPE_GET, 4, ERROR_NOT_FOUND, 0, 0 },
        { &UvcPropsetMemoryTransport, 2, KSPROPERTY_TYPE_BASICSUPPORT, 4, ERROR_NOT_FOUND, 0, 0 },
        { &UvcPropsetMdlCacheClear, 0, KSPROPERTY_TYPE_GET, 4, ERROR_NOT_FOUND, 0, 0 },
        { &UvcPropsetMdlCacheClear, 0, KSPROPERTY_TYPE_BASICSUPPORT, 4, ERROR_NOT_FOUND, 0, 0 },
        { &UvcPropsetMdlCacheClear, 1, KSPROPERTY_TYPE_GET, 4, ERROR_INSUFFICIENT_BUFFER, 0, 0 },
        { &UvcPropsetMdlCacheClear, 1, KSPROPERTY_TYPE_BASICSUPPORT, 4, 0, 4, KSPROPERTY_TYPE_SET },
        { &UvcPropsetMdlCacheClear, 2, KSPROPERTY_TYPE_GET, 4, ERROR_NOT_FOUND, 0, 0 },
        { &UvcPropsetMdlCacheClear, 2, KSPROPERTY_TYPE_BASICSUPPORT, 4, ERROR_NOT_FOUND, 0, 0 },
        { &UvcPropsetConnection, KSPROPERTY_CONNECTION_ACQUIREORDERING, KSPROPERTY_TYPE_GET, 4, 0, 4, FALSE },
        { &UvcPropsetConnection, KSPROPERTY_CONNECTION_ACQUIREORDERING, KSPROPERTY_TYPE_GET, 8, 0, 4, FALSE },
        { &UvcPropsetConnection, KSPROPERTY_CONNECTION_ACQUIREORDERING, KSPROPERTY_TYPE_GET, 0, ERROR_INSUFFICIENT_BUFFER, 0, 0 },
        { &UvcPropsetConnection, KSPROPERTY_CONNECTION_ACQUIREORDERING, KSPROPERTY_TYPE_BASICSUPPORT, 64, 0, sizeof(KSPROPERTY_DESCRIPTION),
          KSPROPERTY_TYPE_GET },
    };
    struct
    {
        ULONG Event;
        PVOID Buffer;
    } Mdl;
    BOOL Transport;

    CheckGets(Pin, Where, Expect, ARRAYSIZE(Expect));
    Transport = FALSE;
    DumpSet(Pin, Where, &UvcPropsetMemoryTransport, 1, &Transport, sizeof(Transport));
    Transport = TRUE;
    DumpSet(Pin, Where, &UvcPropsetMemoryTransport, 1, &Transport, sizeof(Transport));
    Transport = FALSE;
    DumpSet(Pin, Where, &UvcPropsetMemoryTransport, 1, &Transport, sizeof(Transport));
    Mdl.Event = 2;
    Mdl.Buffer = NULL;
    DumpSet(Pin, Where, &UvcPropsetMdlCacheClear, 1, &Mdl, sizeof(Mdl));
    Mdl.Event = 0;
    DumpSet(Pin, Where, &UvcPropsetMdlCacheClear, 1, &Mdl, sizeof(Mdl));
    Mdl.Event = 7;
    DumpSet(Pin, Where, &UvcPropsetMdlCacheClear, 1, &Mdl, sizeof(Mdl));
    DumpSet(Pin, Where, &UvcPropsetMdlCacheClear, 1, &Mdl, 8);
    CheckEventSupport(Pin, Where, &UvcEventsetConnection, ConnectionSupport, ConnectionEnable);
    CheckEventSupport(Pin, Where, &UvcEventsetDevice, NoSet, NoSet);
}

static BOOL
ReadFormatByCompression(HANDLE Filter, ULONG PinId, DWORD Compression, KS_DATAFORMAT_VIDEOINFOHEADER *Format)
{
    static BYTE Ranges[128 * 1024];
    KSMULTIPLE_ITEM *Items = (PVOID)Ranges;
    PBYTE Cursor = (PBYTE)(Items + 1);
    DWORD Returned = 0;
    ULONG Index;

    ZeroMemory(Ranges, sizeof(Ranges));
    if (UvcGetPinProperty(Filter, PinId, KSPROPERTY_PIN_DATARANGES, Ranges, sizeof(Ranges), &Returned))
        return FALSE;
    for (Index = 0; Index < Items->Count; Index++)
    {
        KS_DATARANGE_VIDEO *Range = (PVOID)Cursor;

        if (Range->DataRange.FormatSize == sizeof(KS_DATARANGE_VIDEO) && Range->VideoInfoHeader.bmiHeader.biCompression == Compression)
        {
            UvcBuildFormat(Format, &Range->DataRange.SubFormat, Compression, Range->VideoInfoHeader.bmiHeader.biWidth,
                           Range->VideoInfoHeader.bmiHeader.biHeight, (LONG)Range->VideoInfoHeader.AvgTimePerFrame);
            return TRUE;
        }
        Cursor += (Range->DataRange.FormatSize + 7) & ~7;
    }
    return FALSE;
}

static void
DumpComponent(HANDLE Filter, const WCHAR *Path)
{
    KSCOMPONENTID Component;
    DWORD Returned = 0, Status;

    ZeroMemory(&Component, sizeof(Component));
    Status = UvcGetProperty(Filter, &UvcPropsetGeneral, KSPROPERTY_GENERAL_COMPONENTID, &Component, sizeof(Component), &Returned);
    trace("MEASURE component status %lu returned %lu\n", Status, Returned);
    trace("MEASURE component manufacturer %s\n", wine_dbgstr_guid(&Component.Manufacturer));
    trace("MEASURE component product %s\n", wine_dbgstr_guid(&Component.Product));
    trace("MEASURE component component %s\n", wine_dbgstr_guid(&Component.Component));
    trace("MEASURE component name %s version %lu revision %lu\n", wine_dbgstr_guid(&Component.Name), Component.Version, Component.Revision);
    {
        ULONG Vendor = 0, Product = 0;

        ok(swscanf(Path, L"\\\\?\\usb#vid_%04lx&pid_%04lx", &Vendor, &Product) == 2, "Path '%ls'\n", Path);
        ok(Component.Manufacturer.Data1 == 0xed85452b + Vendor, "Manufacturer %s for vendor %lx\n", wine_dbgstr_guid(&Component.Manufacturer), Vendor);
        ok(Component.Product.Data1 == 0x7c0f42c4 + Product, "Product %s for product %lx\n", wine_dbgstr_guid(&Component.Product), Product);
        ok(Component.Manufacturer.Data2 == 0x36f9 && Component.Manufacturer.Data3 == 0x43a3, "Manufacturer %s\n", wine_dbgstr_guid(&Component.Manufacturer));
        ok(Component.Product.Data2 == 0xc6c9 && Component.Product.Data3 == 0x4ca0, "Product %s\n", wine_dbgstr_guid(&Component.Product));
    }
}

static void
DumpTopology(HANDLE Filter)
{
    BYTE Buffer[1024];
    KSMULTIPLE_ITEM *Items = (PVOID)Buffer;
    KSTOPOLOGY_CONNECTION *Connection = (PVOID)(Items + 1);
    DWORD Returned = 0, Status;
    ULONG Index;

    ZeroMemory(Buffer, sizeof(Buffer));
    Status = UvcGetProperty(Filter, &UvcPropsetTopology, KSPROPERTY_TOPOLOGY_CONNECTIONS, Buffer, sizeof(Buffer), &Returned);
    trace("MEASURE connections status %lu count %lu\n", Status, Items->Count);
    if (Status)
        return;
    for (Index = 0; Index < Items->Count; Index++)
        trace("MEASURE connection %lu from %lu pin %lu to %lu pin %lu\n", Index, Connection[Index].FromNode, Connection[Index].FromNodePin,
              Connection[Index].ToNode, Connection[Index].ToNodePin);
    if (Items->Count == 6)
        ok(Connection[5].FromNodePin == 1 && Connection[5].ToNode == KSFILTER_NODE && Connection[5].ToNodePin == 2,
           "Still connection from %lu pin %lu to %lu pin %lu\n", Connection[5].FromNode, Connection[5].FromNodePin, Connection[5].ToNode, Connection[5].ToNodePin);
}

static ULONG
DumpPins(HANDLE Filter, ULONG *StillPin)
{
    ULONG Count = 0, Pin;
    DWORD Returned = 0, Status;

    *StillPin = MAXDWORD;
    Status = UvcGetProperty(Filter, &UvcPropsetPin, KSPROPERTY_PIN_CTYPES, &Count, sizeof(Count), &Returned);
    trace("MEASURE pin count %lu status %lu\n", Count, Status);

    for (Pin = 0; Pin < Count; Pin++)
    {
        KSPIN_CINSTANCES Instances = { 0 };
        KSPIN_DATAFLOW Flow = 0;
        KSPIN_COMMUNICATION Communication = 0;
        GUID Category = { 0 };
        static BYTE Ranges[128 * 1024];
        KSMULTIPLE_ITEM *Items = (PVOID)Ranges;
        WCHAR Name[64] = L"";
        DWORD NameStatus;

        UvcGetPinProperty(Filter, Pin, KSPROPERTY_PIN_CINSTANCES, &Instances, sizeof(Instances), &Returned);
        UvcGetPinProperty(Filter, Pin, KSPROPERTY_PIN_DATAFLOW, &Flow, sizeof(Flow), &Returned);
        UvcGetPinProperty(Filter, Pin, KSPROPERTY_PIN_COMMUNICATION, &Communication, sizeof(Communication), &Returned);
        UvcGetPinProperty(Filter, Pin, KSPROPERTY_PIN_CATEGORY, &Category, sizeof(Category), &Returned);
        NameStatus = UvcGetPinProperty(Filter, Pin, KSPROPERTY_PIN_NAME, Name, sizeof(Name), &Returned);
        ZeroMemory(Ranges, sizeof(Ranges));
        Status = UvcGetPinProperty(Filter, Pin, KSPROPERTY_PIN_DATARANGES, Ranges, sizeof(Ranges), &Returned);
        trace("MEASURE pin %lu instances %lu/%lu flow %d comm %d category %s name status %lu '%ls' ranges status %lu count %lu\n",
              Pin, Instances.CurrentCount, Instances.PossibleCount, Flow, Communication, wine_dbgstr_guid(&Category),
              NameStatus, Name, Status, Status ? 0 : Items->Count);
        if (IsEqualGUID(&Category, &UvcPinStill))
            *StillPin = Pin;
        if (!Status)
        {
            KSDATARANGE *Range = (PVOID)(Items + 1);
            ULONG Index;

            for (Index = 0; Index < Items->Count; Index++)
            {
                KS_DATARANGE_VIDEO *Video = (PVOID)Range;

                trace("MEASURE pin %lu range %lu size %lu sample %lu sub %s %ldx%ld image %lu\n", Pin, Index,
                      Range->FormatSize, Range->SampleSize, wine_dbgstr_guid(&Range->SubFormat),
                      Video->VideoInfoHeader.bmiHeader.biWidth, Video->VideoInfoHeader.bmiHeader.biHeight,
                      Video->VideoInfoHeader.bmiHeader.biSizeImage);
                if (IsEqualGUID(&Range->Specifier, &UvcSpecifierVideoInfo))
                    trace("MEASURE pin %lu range %lu avg %I64d interval %I64d..%I64d bitrate %lu fixed %d temporal %d mask %lx\n", Pin, Index,
                          Video->VideoInfoHeader.AvgTimePerFrame, Video->ConfigCaps.MinFrameInterval, Video->ConfigCaps.MaxFrameInterval,
                          Video->VideoInfoHeader.dwBitRate, Video->bFixedSizeSamples, Video->bTemporalCompression, Video->StreamDescriptionFlags);
                if (IsEqualGUID(&Range->Specifier, &UvcSpecifierVideoInfo2))
                    trace("MEASURE pin %lu range %lu control flags %08lx\n", Pin, Index, ((KS_DATARANGE_VIDEO2 *)Range)->VideoInfoHeader.dwControlFlags);
                Range = (PVOID)((ULONG_PTR)Range + ((Range->FormatSize + 7) & ~7ul));
            }
        }
    }
    return Count;
}

static void
DumpControl(HANDLE Filter, const GUID *Set, ULONG Id, const char *Name)
{
    UVC_CONTROL_REQUEST Request, Reply;
    BYTE Support[128];
    KSPROPERTY_DESCRIPTION *Description = (PVOID)Support;
    KSPROPERTY_MEMBERSHEADER *Header = (PVOID)(Description + 1);
    KSPROPERTY_STEPPING_LONG *Step = (PVOID)(Header + 1);
    KSPROPERTY Query;
    DWORD Returned = 0;
    BOOL Result;

    ZeroMemory(&Request, sizeof(Request));
    ZeroMemory(&Reply, sizeof(Reply));
    Request.Property.Set = *Set;
    Request.Property.Id = Id;
    Request.Property.Flags = KSPROPERTY_TYPE_GET;
    Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Request, sizeof(Request), &Reply, sizeof(Reply), &Returned);
    trace("MEASURE %s %lu get %lu returned %lu value %ld flags %lx caps %lx\n", Name, Id, Result ? 0 : GetLastError(),
          Returned, Reply.Value, Reply.Flags, Reply.Capabilities);

    Query.Set = *Set;
    Query.Id = Id;
    Query.Flags = KSPROPERTY_TYPE_BASICSUPPORT;
    ZeroMemory(Support, sizeof(Support));
    Returned = 0;
    Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Query, sizeof(Query), Support, sizeof(Support), &Returned);
    trace("MEASURE %s %lu basic %lu returned %lu access %lx size %lu type %lu members %lu step %lu min %ld max %ld\n", Name, Id,
          Result ? 0 : GetLastError(), Returned, Description->AccessFlags, Description->DescriptionSize,
          Description->PropTypeSet.Id, Description->MembersListCount, Step->SteppingDelta,
          Step->Bounds.SignedMinimum, Step->Bounds.SignedMaximum);

    Query.Flags = KSPROPERTY_TYPE_DEFAULTVALUES;
    ZeroMemory(Support, sizeof(Support));
    Returned = 0;
    Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Query, sizeof(Query), Support, sizeof(Support), &Returned);
    trace("MEASURE %s %lu default %lu returned %lu value %ld\n", Name, Id, Result ? 0 : GetLastError(), Returned, *(LONG *)(Header + 1));
}

static void
CheckRanges(HANDLE Filter, const GUID *Set, const EXPECT_RANGE *Expect, ULONG Count)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        UVC_CONTROL_REQUEST Request, Reply;
        BYTE Support[128];
        KSPROPERTY_DESCRIPTION *Description = (PVOID)Support;
        KSPROPERTY_MEMBERSHEADER *Header = (PVOID)(Description + 1);
        KSPROPERTY_STEPPING_LONG *Step = (PVOID)(Header + 1);
        KSPROPERTY Query;
        DWORD Returned = 0;
        BOOL Result;

        Query.Set = *Set;
        Query.Id = Expect[Index].Id;
        Query.Flags = KSPROPERTY_TYPE_BASICSUPPORT;
        ZeroMemory(Support, sizeof(Support));
        Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Query, sizeof(Query), Support, sizeof(Support), &Returned);
        ok(Result && Returned == 72, "Control %lu support %lu returned %lu\n", Expect[Index].Id, Result ? 0 : GetLastError(), Returned);
        ok(Step->Bounds.SignedMinimum == Expect[Index].Min && Step->Bounds.SignedMaximum == Expect[Index].Max && Step->SteppingDelta == Expect[Index].Step,
           "Control %lu range %ld..%ld step %lu\n", Expect[Index].Id, Step->Bounds.SignedMinimum, Step->Bounds.SignedMaximum, Step->SteppingDelta);

        Query.Flags = KSPROPERTY_TYPE_DEFAULTVALUES;
        ZeroMemory(Support, sizeof(Support));
        Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Query, sizeof(Query), Support, sizeof(Support), &Returned);
        ok(Result && Returned == 60 && *(LONG *)(Header + 1) == Expect[Index].Default, "Control %lu default %ld returned %lu\n", Expect[Index].Id, *(LONG *)(Header + 1), Returned);

        ZeroMemory(&Request, sizeof(Request));
        ZeroMemory(&Reply, sizeof(Reply));
        Request.Property.Set = *Set;
        Request.Property.Id = Expect[Index].Id;
        Request.Property.Flags = KSPROPERTY_TYPE_GET;
        Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Request, sizeof(Request), &Reply, sizeof(Reply), &Returned);
        ok(Result && Returned == 40 && Reply.Capabilities == Expect[Index].Caps, "Control %lu get %lu caps %lx\n", Expect[Index].Id, Result ? 0 : GetLastError(), Reply.Capabilities);
    }
}

static void
CheckCameraSets(HANDLE Filter)
{
    ULONG Index;

    for (Index = 0; Index < ARRAYSIZE(ExpectCameraSets); Index++)
    {
        const EXPECT_SET *Expect = &ExpectCameraSets[Index];
        UVC_CONTROL_REQUEST Request, Reply;
        DWORD Returned = 0;
        BOOL Result;

        ZeroMemory(&Request, sizeof(Request));
        Request.Property.Set = UvcPropsetCamera;
        Request.Property.Id = Expect->Id;
        Request.Property.Flags = KSPROPERTY_TYPE_SET;
        Request.Value = Expect->Value;
        Request.Flags = Expect->Flags;
        Request.Capabilities = Expect->Flags;
        Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Request, sizeof(Request), &Request, sizeof(Request), &Returned);
        ok(Result, "Set %lu of control %lu failed %lu\n", Index, Expect->Id, GetLastError());

        ZeroMemory(&Reply, sizeof(Reply));
        Request.Property.Flags = KSPROPERTY_TYPE_GET;
        Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Request, sizeof(Request), &Reply, sizeof(Reply), &Returned);
        ok(Result && Reply.Value == Expect->ReadValue && Reply.Flags == Expect->ReadFlags,
           "Set %lu of control %lu read back %ld flags %lx\n", Index, Expect->Id, Reply.Value, Reply.Flags);
    }
}

static void
CheckWhiteBalanceComponent(HANDLE Filter)
{
    KSPROPERTY_VIDEOPROCAMP_NODE_S2 Node, Reply;
    KSPROPERTY_VIDEOPROCAMP_S2 Plain;
    BYTE Support[128];
    KSPROPERTY_DESCRIPTION *Description = (PVOID)Support;
    KSPROPERTY_MEMBERSHEADER *Header = (PVOID)(Description + 1);
    KSPROPERTY_STEPPING_LONG *Step = (PVOID)(Header + 1);
    KSPROPERTY Query;
    DWORD Returned = 0;
    BOOL Result;

    Query.Set = UvcPropsetProcAmp;
    Query.Id = KSPROPERTY_VIDEOPROCAMP_WHITEBALANCE_COMPONENT;
    Query.Flags = KSPROPERTY_TYPE_BASICSUPPORT;
    ZeroMemory(Support, sizeof(Support));
    Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Query, sizeof(Query), Support, sizeof(Support), &Returned);
    ok(Result && Step->Bounds.SignedMinimum == 0 && Step->Bounds.SignedMaximum == 0x0fff0fff && Step->SteppingDelta == 0x00010001,
       "White balance range %ld..%ld step %lu\n", Step->Bounds.SignedMinimum, Step->Bounds.SignedMaximum, Step->SteppingDelta);

    ZeroMemory(&Plain, sizeof(Plain));
    Plain.Property = Query;
    Plain.Property.Flags = KSPROPERTY_TYPE_GET;
    Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Plain, sizeof(Plain), &Plain, sizeof(Plain), &Returned);
    ok(!Result && GetLastError() == ERROR_INSUFFICIENT_BUFFER, "Plain white balance %d error %lu\n", Result, GetLastError());

    ZeroMemory(&Node, sizeof(Node));
    ZeroMemory(&Reply, sizeof(Reply));
    Node.NodeProperty.Property = Query;
    Node.NodeProperty.Property.Flags = KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_TOPOLOGY;
    Node.NodeProperty.NodeId = 1;
    Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Node, sizeof(Node), &Reply, sizeof(Reply), &Returned);
    ok(Result && Returned == sizeof(Reply) && Reply.Value1 == 1024 && Reply.Value2 == 1024 && Reply.Flags == 1 && Reply.Capabilities == 3,
       "Node white balance %lu returned %lu %ld %ld flags %lx caps %lx\n", Result ? 0 : GetLastError(), Returned, Reply.Value1, Reply.Value2, Reply.Flags, Reply.Capabilities);

    Node.NodeProperty.Property.Flags = KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_TOPOLOGY;
    Node.Value1 = 0x100;
    Node.Value2 = 0x200;
    Node.Flags = KSPROPERTY_VIDEOPROCAMP_FLAGS_MANUAL;
    Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Node, sizeof(Node), &Node, sizeof(Node), &Returned);
    ok(Result, "Setting white balance failed %lu\n", GetLastError());

    Node.NodeProperty.Property.Flags = KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_TOPOLOGY;
    ZeroMemory(&Reply, sizeof(Reply));
    Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Node, sizeof(Node), &Reply, sizeof(Reply), &Returned);
    ok(Result && Reply.Value1 == 0x100 && Reply.Value2 == 0x200 && Reply.Flags == 2, "White balance read back %ld %ld flags %lx\n", Reply.Value1, Reply.Value2, Reply.Flags);
}

static BOOL
HasAllControls(HANDLE Filter)
{
    KSPROPERTY Query;
    ULONG Access = 0;
    DWORD Returned = 0;

    Query.Set = UvcPropsetCamera;
    Query.Id = KSPROPERTY_CAMERACONTROL_ZOOM_RELATIVE;
    Query.Flags = KSPROPERTY_TYPE_BASICSUPPORT;
    return UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Query, sizeof(Query), &Access, sizeof(Access), &Returned);
}

static void
CheckNodes(HANDLE Filter)
{
    static const struct { const GUID *Set; ULONG Id; ULONG GoodNode; } Probes[] =
    {
        { &UvcPropsetProcAmp, KSPROPERTY_VIDEOPROCAMP_BRIGHTNESS, 1 },
        { &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_ZOOM, 0 },
        { &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_PANTILT, 0 },
        { &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_FOCAL_LENGTH, 0 },
    };
    ULONG Probe, Node;

    for (Probe = 0; Probe < ARRAYSIZE(Probes); Probe++)
    {
        for (Node = 0; Node < 5; Node++)
        {
            UVC_NODE_REQUEST Request, Reply;
            DWORD Returned = 0;
            BOOL Result;

            ZeroMemory(&Request, sizeof(Request));
            ZeroMemory(&Reply, sizeof(Reply));
            Request.Node.Property.Set = *Probes[Probe].Set;
            Request.Node.Property.Id = Probes[Probe].Id;
            Request.Node.Property.Flags = KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_TOPOLOGY;
            Request.Node.NodeId = Node;
            Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Request, sizeof(Request), &Reply, sizeof(Reply), &Returned);
            if (Node == Probes[Probe].GoodNode)
                ok(Result && Returned == 48, "Node %lu probe %lu failed %lu returned %lu\n", Node, Probe, GetLastError(), Returned);
            else if (Node < 4)
                ok(!Result && GetLastError() == ERROR_SET_NOT_FOUND, "Node %lu probe %lu result %d error %lu\n", Node, Probe, Result, GetLastError());
            else
                ok(!Result && GetLastError() == ERROR_INVALID_FUNCTION, "Node %lu probe %lu result %d error %lu\n", Node, Probe, Result, GetLastError());
        }
    }
}

static void
DumpControlSet(HANDLE Filter, const GUID *Set, ULONG Id, const char *Name, LONG Value, ULONG Flags)
{
    UVC_CONTROL_REQUEST Request, Reply;
    DWORD Returned = 0;
    BOOL Result;

    ZeroMemory(&Request, sizeof(Request));
    Request.Property.Set = *Set;
    Request.Property.Id = Id;
    Request.Property.Flags = KSPROPERTY_TYPE_SET;
    Request.Value = Value;
    Request.Flags = Flags;
    Request.Capabilities = Flags;
    Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Request, sizeof(Request), &Request, sizeof(Request), &Returned);
    trace("MEASURE %s %lu set %ld flags %lx status %lu\n", Name, Id, Value, Flags, Result ? 0 : GetLastError());

    ZeroMemory(&Reply, sizeof(Reply));
    Request.Property.Flags = KSPROPERTY_TYPE_GET;
    Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Request, sizeof(Request), &Reply, sizeof(Reply), &Returned);
    trace("MEASURE %s %lu after set get %lu value %ld flags %lx\n", Name, Id, Result ? 0 : GetLastError(), Reply.Value, Reply.Flags);
}

static void
DumpWhiteBalanceComponent(HANDLE Filter)
{
    KSPROPERTY_VIDEOPROCAMP_S2 Request, Reply;
    BYTE Large[64];
    DWORD Returned = 0;
    BOOL Result;

    ZeroMemory(&Request, sizeof(Request));
    ZeroMemory(&Reply, sizeof(Reply));
    Request.Property.Set = UvcPropsetProcAmp;
    Request.Property.Id = KSPROPERTY_VIDEOPROCAMP_WHITEBALANCE_COMPONENT;
    Request.Property.Flags = KSPROPERTY_TYPE_GET;
    Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Request, sizeof(Request), &Reply, sizeof(Reply), &Returned);
    trace("MEASURE wbcomponent get s2 size %lu status %lu returned %lu value1 %ld value2 %ld flags %lx caps %lx\n", (ULONG)sizeof(Request),
          Result ? 0 : GetLastError(), Returned, Reply.Value1, Reply.Value2, Reply.Flags, Reply.Capabilities);

    {
        static const DWORD InSizes[] = { 24, 32, 36, 40, 44, 48 };
        static const DWORD OutSizes[] = { 16, 24, 32, 36, 40, 44, 48, 52, 56, 64, 72, 96 };
        ULONG In, Out;

        for (In = 0; In < ARRAYSIZE(InSizes); In++)
        {
            for (Out = 0; Out < ARRAYSIZE(OutSizes); Out++)
            {
                BYTE Query[64] = { 0 }, Answer[96] = { 0 };

                *(KSPROPERTY *)Query = Request.Property;
                Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, Query, InSizes[In], Answer, OutSizes[Out], &Returned);
                if (Result || GetLastError() != ERROR_INSUFFICIENT_BUFFER)
                    trace("MEASURE wbcomponent matrix in %lu out %lu status %lu returned %lu data %08lx %08lx %08lx %08lx\n", InSizes[In], OutSizes[Out],
                          Result ? 0 : GetLastError(), Returned, ((ULONG *)Answer)[6], ((ULONG *)Answer)[7], ((ULONG *)Answer)[8], ((ULONG *)Answer)[9]);
            }
        }
    }

    ZeroMemory(Large, sizeof(Large));
    Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Request, sizeof(KSPROPERTY), Large, sizeof(Large), &Returned);
    trace("MEASURE wbcomponent get large status %lu returned %lu data %08lx %08lx %08lx %08lx %08lx %08lx\n", Result ? 0 : GetLastError(), Returned,
          ((ULONG *)Large)[6], ((ULONG *)Large)[7], ((ULONG *)Large)[8], ((ULONG *)Large)[9], ((ULONG *)Large)[10], ((ULONG *)Large)[11]);

    {
        KSPROPERTY_VIDEOPROCAMP_NODE_S2 Node, NodeReply;
        ULONG NodeId;

        for (NodeId = 0; NodeId < 2; NodeId++)
        {
            ZeroMemory(&Node, sizeof(Node));
            ZeroMemory(&NodeReply, sizeof(NodeReply));
            Node.NodeProperty.Property = Request.Property;
            Node.NodeProperty.Property.Flags = KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_TOPOLOGY;
            Node.NodeProperty.NodeId = NodeId;
            Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Node, sizeof(Node), &NodeReply, sizeof(NodeReply), &Returned);
            trace("MEASURE wbcomponent node %lu get size %lu status %lu returned %lu value1 %ld value2 %ld flags %lx caps %lx\n", NodeId, (ULONG)sizeof(Node),
                  Result ? 0 : GetLastError(), Returned, NodeReply.Value1, NodeReply.Value2, NodeReply.Flags, NodeReply.Capabilities);
        }
        Node.NodeProperty.NodeId = 1;
        Node.NodeProperty.Property.Flags = KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_TOPOLOGY;
        Node.Value1 = 0x100;
        Node.Value2 = 0x200;
        Node.Flags = KSPROPERTY_VIDEOPROCAMP_FLAGS_MANUAL;
        Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Node, sizeof(Node), &Node, sizeof(Node), &Returned);
        trace("MEASURE wbcomponent node set status %lu\n", Result ? 0 : GetLastError());
        Node.NodeProperty.Property.Flags = KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_TOPOLOGY;
        ZeroMemory(&NodeReply, sizeof(NodeReply));
        Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Node, sizeof(Node), &NodeReply, sizeof(NodeReply), &Returned);
        trace("MEASURE wbcomponent node after set status %lu value1 %ld value2 %ld flags %lx\n", Result ? 0 : GetLastError(), NodeReply.Value1, NodeReply.Value2, NodeReply.Flags);
    }

    {
        BYTE Input[64] = { 0 };

        *(KSPROPERTY *)Input = Request.Property;
        ZeroMemory(Large, sizeof(Large));
        Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, Input, sizeof(Input), Large, sizeof(Large), &Returned);
        trace("MEASURE wbcomponent get 64 in out status %lu returned %lu data %08lx %08lx %08lx %08lx %08lx %08lx\n", Result ? 0 : GetLastError(), Returned,
              ((ULONG *)Large)[6], ((ULONG *)Large)[7], ((ULONG *)Large)[8], ((ULONG *)Large)[9], ((ULONG *)Large)[10], ((ULONG *)Large)[11]);
    }

    Request.Property.Flags = KSPROPERTY_TYPE_SET;
    Request.Value1 = 0x100;
    Request.Value2 = 0x200;
    Request.Flags = KSPROPERTY_VIDEOPROCAMP_FLAGS_MANUAL;
    Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Request, sizeof(Request), &Request, sizeof(Request), &Returned);
    trace("MEASURE wbcomponent set s2 status %lu\n", Result ? 0 : GetLastError());

    Request.Property.Flags = KSPROPERTY_TYPE_GET;
    ZeroMemory(Large, sizeof(Large));
    Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Request, sizeof(KSPROPERTY), Large, sizeof(Large), &Returned);
    trace("MEASURE wbcomponent after set status %lu returned %lu data %08lx %08lx %08lx %08lx %08lx %08lx\n", Result ? 0 : GetLastError(), Returned,
          ((ULONG *)Large)[6], ((ULONG *)Large)[7], ((ULONG *)Large)[8], ((ULONG *)Large)[9], ((ULONG *)Large)[10], ((ULONG *)Large)[11]);
}

static void
DumpControls(HANDLE Filter)
{
    ULONG Id;

    for (Id = 0; Id <= 14; Id++)
        DumpControl(Filter, &UvcPropsetProcAmp, Id, "procamp");
    for (Id = 0; Id <= 22; Id++)
        DumpControl(Filter, &UvcPropsetCamera, Id, "camera");

    DumpWhiteBalanceComponent(Filter);
    DumpControlSet(Filter, &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_EXPOSURE_RELATIVE, "camera", 1, KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL | KSPROPERTY_CAMERACONTROL_FLAGS_RELATIVE);
    DumpControlSet(Filter, &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_ZOOM_RELATIVE, "camera", 1, KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL | KSPROPERTY_CAMERACONTROL_FLAGS_RELATIVE);
    DumpControlSet(Filter, &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_PAN_RELATIVE, "camera", 1, KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL | KSPROPERTY_CAMERACONTROL_FLAGS_RELATIVE);
    DumpControlSet(Filter, &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_TILT_RELATIVE, "camera", 1, KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL | KSPROPERTY_CAMERACONTROL_FLAGS_RELATIVE);
    DumpControlSet(Filter, &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_ROLL_RELATIVE, "camera", 1, KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL | KSPROPERTY_CAMERACONTROL_FLAGS_RELATIVE);
    DumpControlSet(Filter, &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_FOCUS_RELATIVE, "camera", 1, KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL | KSPROPERTY_CAMERACONTROL_FLAGS_RELATIVE);
    DumpControlSet(Filter, &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_IRIS_RELATIVE, "camera", 1, KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL | KSPROPERTY_CAMERACONTROL_FLAGS_RELATIVE);
    DumpControlSet(Filter, &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_EXPOSURE_RELATIVE, "camera", -1, KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL | KSPROPERTY_CAMERACONTROL_FLAGS_RELATIVE);
    DumpControlSet(Filter, &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_ZOOM_RELATIVE, "camera", -1, KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL | KSPROPERTY_CAMERACONTROL_FLAGS_RELATIVE);
    DumpControlSet(Filter, &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_ZOOM_RELATIVE, "camera", 3, KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL | KSPROPERTY_CAMERACONTROL_FLAGS_RELATIVE);
    DumpControlSet(Filter, &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_PAN_RELATIVE, "camera", -1, KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL | KSPROPERTY_CAMERACONTROL_FLAGS_RELATIVE);
    DumpControlSet(Filter, &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_PAN_RELATIVE, "camera", 3, KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL | KSPROPERTY_CAMERACONTROL_FLAGS_RELATIVE);
    DumpControlSet(Filter, &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_TILT_RELATIVE, "camera", 3, KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL | KSPROPERTY_CAMERACONTROL_FLAGS_RELATIVE);
    DumpControlSet(Filter, &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_FOCUS_RELATIVE, "camera", -1, KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL | KSPROPERTY_CAMERACONTROL_FLAGS_RELATIVE);
    DumpControlSet(Filter, &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_IRIS, "camera", 300, KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL);
    DumpControlSet(Filter, &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_IRIS, "camera", 300, KSPROPERTY_CAMERACONTROL_FLAGS_AUTO);
    DumpControl(Filter, &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_EXPOSURE, "camera");
    DumpControl(Filter, &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_IRIS, "camera");
    DumpControlSet(Filter, &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_EXPOSURE, "camera", -5, KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL);
    DumpControl(Filter, &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_IRIS, "camera");
    DumpControlSet(Filter, &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_EXPOSURE_RELATIVE, "camera", 1, KSPROPERTY_CAMERACONTROL_FLAGS_AUTO | KSPROPERTY_CAMERACONTROL_FLAGS_RELATIVE);
    DumpControlSet(Filter, &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_FOCUS_RELATIVE, "camera", 1, KSPROPERTY_CAMERACONTROL_FLAGS_AUTO | KSPROPERTY_CAMERACONTROL_FLAGS_RELATIVE);
    DumpControlSet(Filter, &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_ROLL, "camera", 5, KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL);
    DumpControlSet(Filter, &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_SCANMODE, "camera", 1, KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL);
    DumpControlSet(Filter, &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_PRIVACY, "camera", 1, KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL);
    DumpControlSet(Filter, &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_AUTO_EXPOSURE_PRIORITY, "camera", 1, KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL);
    DumpControlSet(Filter, &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_EXPOSURE, "camera", -6, KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL);
    DumpControlSet(Filter, &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_EXPOSURE, "camera", -6, KSPROPERTY_CAMERACONTROL_FLAGS_AUTO);
}

static void
DumpNodes(HANDLE Filter)
{
    static const struct { const GUID *Set; ULONG Id; const char *Name; } Probes[] =
    {
        { &UvcPropsetProcAmp, KSPROPERTY_VIDEOPROCAMP_BRIGHTNESS, "procamp" },
        { &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_ZOOM, "camera" },
        { &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_PANTILT, "camera" },
        { &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_FOCAL_LENGTH, "camera" },
        { &UvcPropsetCamera, KSPROPERTY_CAMERACONTROL_IRIS, "camera" },
    };
    ULONG Node, Probe;

    for (Probe = 0; Probe < ARRAYSIZE(Probes); Probe++)
    {
        for (Node = 0; Node < 5; Node++)
        {
            UVC_NODE_REQUEST Request, Reply;
            DWORD Returned = 0;
            BOOL Result;

            ZeroMemory(&Request, sizeof(Request));
            ZeroMemory(&Reply, sizeof(Reply));
            Request.Node.Property.Set = *Probes[Probe].Set;
            Request.Node.Property.Id = Probes[Probe].Id;
            Request.Node.Property.Flags = KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_TOPOLOGY;
            Request.Node.NodeId = Node;
            Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Request, sizeof(Request), &Reply, sizeof(Reply), &Returned);
            trace("MEASURE node %lu %s %lu status %lu returned %lu value %ld flags %lx caps %lx\n", Node, Probes[Probe].Name,
                  Probes[Probe].Id, Result ? 0 : GetLastError(), Returned, Reply.Value, Reply.Flags, Reply.Capabilities);
        }
    }
}

static void
DumpVideoControl(HANDLE Handle, const char *Where, ULONG StreamIndex, ULONG Pins)
{
    UVC_CAPS_REQUEST Request, Reply;
    DWORD Returned = 0;
    BOOL Result;
    ULONG Id;

    for (Id = KSPROPERTY_VIDEOCONTROL_CAPS; Id <= KSPROPERTY_VIDEOCONTROL_MODE; Id += KSPROPERTY_VIDEOCONTROL_MODE)
    {
        ZeroMemory(&Request, sizeof(Request));
        ZeroMemory(&Reply, sizeof(Reply));
        Request.Property.Set = UvcPropsetVideoControl;
        Request.Property.Id = Id;
        Request.Property.Flags = KSPROPERTY_TYPE_GET;
        Request.StreamIndex = StreamIndex;
        Result = UvcIoctl(Handle, IOCTL_KS_PROPERTY, &Request, sizeof(Request), &Reply, sizeof(Reply), &Returned);
        trace("MEASURE videocontrol %s stream %lu id %lu status %lu returned %lu value %lx\n", Where, StreamIndex, Id,
              Result ? 0 : GetLastError(), Returned, Reply.Value);
        if (StreamIndex < Pins)
            ok(Result && Returned == 32 && Reply.Value == (StreamIndex == 2 ? 0xc : 0), "Video control %s stream %lu id %lu: %lu returned %lu value %lx\n", Where, StreamIndex, Id, Result ? 0 : GetLastError(), Returned, Reply.Value);
        else
            ok(!Result && GetLastError() == ERROR_INVALID_PARAMETER, "Video control stream %lu id %lu: %d error %lu\n", StreamIndex, Id, Result, GetLastError());
    }
}

typedef struct _UVC_RATE_REQUEST
{
    KSPROPERTY Property;
    ULONG StreamIndex;
    ULONG RangeIndex;
    SIZE Dimensions;
    LONGLONG Actual;
    LONGLONG Maximum;
} UVC_RATE_REQUEST;

static void
DumpFrameRates(HANDLE Handle, const char *Where, ULONG Stream, ULONG RangeIndex, LONG Width, LONG Height)
{
    UVC_RATE_REQUEST Request, Reply;
    BYTE List[256];
    KSMULTIPLE_ITEM *Items = (PVOID)List;
    LONGLONG *Rates = (PVOID)(Items + 1);
    DWORD Returned = 0;
    BOOL Result;
    ULONG Index;

    ZeroMemory(&Request, sizeof(Request));
    ZeroMemory(&Reply, sizeof(Reply));
    Request.Property.Set = UvcPropsetVideoControl;
    Request.Property.Id = KSPROPERTY_VIDEOCONTROL_ACTUAL_FRAME_RATE;
    Request.Property.Flags = KSPROPERTY_TYPE_GET;
    Request.StreamIndex = Stream;
    Request.RangeIndex = RangeIndex;
    Request.Dimensions.cx = Width;
    Request.Dimensions.cy = Height;
    Result = UvcIoctl(Handle, IOCTL_KS_PROPERTY, &Request, sizeof(Request), &Reply, sizeof(Reply), &Returned);
    trace("MEASURE actualrate %s stream %lu range %lu %ldx%ld status %lu returned %lu actual %I64d max %I64d\n", Where, Stream, RangeIndex, Width, Height,
          Result ? 0 : GetLastError(), Returned, Reply.Actual, Reply.Maximum);

    Request.Property.Id = KSPROPERTY_VIDEOCONTROL_FRAME_RATES;
    Returned = 0;
    Result = UvcIoctl(Handle, IOCTL_KS_PROPERTY, &Request, sizeof(KSPROPERTY) + 16, NULL, 0, &Returned);
    trace("MEASURE framerates %s size query status %lu returned %lu\n", Where, Result ? 0 : GetLastError(), Returned);
    ZeroMemory(List, sizeof(List));
    Returned = 0;
    Result = UvcIoctl(Handle, IOCTL_KS_PROPERTY, &Request, sizeof(KSPROPERTY) + 16, List, sizeof(List), &Returned);
    trace("MEASURE framerates %s stream %lu range %lu %ldx%ld status %lu returned %lu size %lu count %lu\n", Where, Stream, RangeIndex, Width, Height,
          Result ? 0 : GetLastError(), Returned, Items->Size, Items->Count);
    if (Result)
    {
        for (Index = 0; Index < Items->Count && Index < 16; Index++)
            trace("MEASURE framerates %s rate %lu = %I64d\n", Where, Index, Rates[Index]);
    }
}

static void
DumpAllFrameRates(HANDLE Filter)
{
    static BYTE Ranges[128 * 1024];
    KSMULTIPLE_ITEM *Items = (PVOID)Ranges;
    KSDATARANGE *Range = (PVOID)(Items + 1);
    DWORD Returned = 0;
    ULONG Index;

    ZeroMemory(Ranges, sizeof(Ranges));
    if (UvcGetPinProperty(Filter, 0, KSPROPERTY_PIN_DATARANGES, Ranges, sizeof(Ranges), &Returned))
        return;
    for (Index = 0; Index < Items->Count; Index++)
    {
        KS_DATARANGE_VIDEO *Video = (PVOID)Range;

        if (IsEqualGUID(&Range->Specifier, &UvcSpecifierVideoInfo))
            DumpFrameRates(Filter, "filter", 0, Index, Video->VideoInfoHeader.bmiHeader.biWidth, Video->VideoInfoHeader.bmiHeader.biHeight);
        Range = (PVOID)((ULONG_PTR)Range + ((Range->FormatSize + 7) & ~7ul));
    }
}

static void
SetVideoControlMode(HANDLE Handle, const char *Where, ULONG Stream, LONG Mode)
{
    UVC_CAPS_REQUEST Request;
    DWORD Returned = 0;
    BOOL Result;

    ZeroMemory(&Request, sizeof(Request));
    Request.Property.Set = UvcPropsetVideoControl;
    Request.Property.Id = KSPROPERTY_VIDEOCONTROL_MODE;
    Request.Property.Flags = KSPROPERTY_TYPE_SET;
    Request.StreamIndex = Stream;
    Request.Value = Mode;
    Result = UvcIoctl(Handle, IOCTL_KS_PROPERTY, &Request, sizeof(Request), &Request, sizeof(Request), &Returned);
    trace("MEASURE setmode %s stream %lu mode %lx status %lu\n", Where, Stream, Mode, Result ? 0 : GetLastError());
}

static DWORD
StillVideoControlStatus(HANDLE Still, ULONG StreamIndex)
{
    UVC_CAPS_REQUEST Request, Reply;
    DWORD Returned = 0;

    ZeroMemory(&Request, sizeof(Request));
    Request.Property.Set = UvcPropsetVideoControl;
    Request.Property.Id = KSPROPERTY_VIDEOCONTROL_CAPS;
    Request.Property.Flags = KSPROPERTY_TYPE_GET;
    Request.StreamIndex = StreamIndex;
    ok(!UvcIoctl(Still, IOCTL_KS_PROPERTY, &Request, sizeof(Request), &Reply, sizeof(Reply), &Returned) && GetLastError() == ERROR_SET_NOT_FOUND,
       "Video control on the still pin error %lu\n", GetLastError());
    return GetLastError();
}

static HANDLE
EnableEvent(HANDLE Handle, const char *Where, HANDLE Signal, KSEVENTDATA *Data, BOOL IsFilter)
{
    KSEVENT Event;
    DWORD Returned = 0;
    BOOL Result;

    Event.Set = UvcEventsetVidcapToSti;
    Event.Id = 0;
    Event.Flags = KSEVENT_TYPE_SETSUPPORT;
    Result = UvcIoctl(Handle, IOCTL_KS_ENABLE_EVENT, &Event, sizeof(Event), NULL, 0, &Returned);
    trace("MEASURE event %s setsupport status %lu\n", Where, Result ? 0 : GetLastError());
    Event.Flags = KSEVENT_TYPE_BASICSUPPORT;
    Result = UvcIoctl(Handle, IOCTL_KS_ENABLE_EVENT, &Event, sizeof(Event), NULL, 0, &Returned);
    trace("MEASURE event %s basicsupport status %lu\n", Where, Result ? 0 : GetLastError());
    {
        ULONG Support = 0;

        Returned = 0;
        Result = UvcIoctl(Handle, IOCTL_KS_ENABLE_EVENT, &Event, sizeof(Event), &Support, sizeof(Support), &Returned);
        trace("MEASURE event %s basicsupport 4 status %lu returned %lu value %lx\n", Where, Result ? 0 : GetLastError(), Returned, Support);
    }

    ZeroMemory(Data, sizeof(*Data));
    Data->NotificationType = KSEVENTF_EVENT_HANDLE;
    Data->EventHandle.Event = Signal;
    Event.Flags = KSEVENT_TYPE_ENABLE;
    Result = UvcIoctl(Handle, IOCTL_KS_ENABLE_EVENT, &Event, sizeof(Event), Data, sizeof(*Data), &Returned);
    trace("MEASURE event %s enable status %lu\n", Where, Result ? 0 : GetLastError());
    if (Handle)
        ok(IsFilter ? Result : (!Result && GetLastError() == ERROR_SET_NOT_FOUND), "Enabling the event on %s: %d error %lu\n", Where, Result, GetLastError());
    return Result ? Signal : NULL;
}

static void
ReadPending(HANDLE Pin, UVC_READ_REQUEST *Request, PBYTE Buffer, DWORD Size, OVERLAPPED *Overlapped)
{
    DWORD Returned = 0;
    BOOL Result;

    ZeroMemory(Request, sizeof(*Request));
    Request->Header.Size = sizeof(*Request);
    Request->Header.FrameExtent = Size;
    Request->Header.Data = Buffer;
    Request->FrameInfo.ExtendedHeaderSize = sizeof(KS_FRAME_INFO);
    ZeroMemory(Overlapped, sizeof(*Overlapped));
    Overlapped->hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Result = DeviceIoControl(Pin, IOCTL_KS_READ_STREAM, NULL, 0, Request, sizeof(*Request), &Returned, Overlapped);
    trace("MEASURE read result %d error %lu\n", Result, Result ? 0 : GetLastError());
}

static void
WaitRead(HANDLE Pin, UVC_READ_REQUEST *Request, OVERLAPPED *Overlapped, DWORD Timeout, const char *What)
{
    DWORD Wait = WaitForSingleObject(Overlapped->hEvent, Timeout), Returned = 0;
    BOOL Result = FALSE;

    if (Wait == WAIT_OBJECT_0)
        Result = GetOverlappedResult(Pin, Overlapped, &Returned, FALSE);
    else
    {
        CancelIo(Pin);
        GetOverlappedResult(Pin, Overlapped, &Returned, TRUE);
    }
    trace("MEASURE %s wait %lu result %d used %lu flags %lx\n", What, Wait, Result, Request->Header.DataUsed, Request->Header.OptionsFlags);
    CloseHandle(Overlapped->hEvent);
}

static BOOL
ReadFirstFormat(HANDLE Filter, ULONG PinId, KS_DATAFORMAT_VIDEOINFOHEADER *Format)
{
    static BYTE Ranges[128 * 1024];
    KSMULTIPLE_ITEM *Items = (PVOID)Ranges;
    KS_DATARANGE_VIDEO *Range = (PVOID)(Items + 1);
    DWORD Returned = 0;

    ZeroMemory(Ranges, sizeof(Ranges));
    if (UvcGetPinProperty(Filter, PinId, KSPROPERTY_PIN_DATARANGES, Ranges, sizeof(Ranges), &Returned) || !Items->Count)
        return FALSE;
    UvcBuildFormat(Format, &Range->DataRange.SubFormat, Range->VideoInfoHeader.bmiHeader.biCompression,
                   Range->VideoInfoHeader.bmiHeader.biWidth, Range->VideoInfoHeader.bmiHeader.biHeight,
                   (LONG)Range->VideoInfoHeader.AvgTimePerFrame);
    return TRUE;
}

static void
SetTrigger(HANDLE Handle, const char *Where, ULONG StreamIndex)
{
    UVC_CAPS_REQUEST Request;
    DWORD Returned = 0;
    BOOL Result;

    ZeroMemory(&Request, sizeof(Request));
    Request.Property.Set = UvcPropsetVideoControl;
    Request.Property.Id = KSPROPERTY_VIDEOCONTROL_MODE;
    Request.Property.Flags = KSPROPERTY_TYPE_SET;
    Request.StreamIndex = StreamIndex;
    Request.Value = 8;
    Result = UvcIoctl(Handle, IOCTL_KS_PROPERTY, &Request, sizeof(Request), &Request, sizeof(Request), &Returned);
    trace("MEASURE trigger %s stream %lu status %lu\n", Where, StreamIndex, Result ? 0 : GetLastError());
}

typedef struct _UVC_READER
{
    HANDLE Pin;
    PBYTE Buffer;
    DWORD Size;
    volatile LONG Stop;
    LONG Frames;
    LONGLONG FirstPicture;
    LONGLONG LastPicture;
    LONGLONG LastDrop;
} UVC_READER;

static DWORD WINAPI
ReaderThread(PVOID Context)
{
    UVC_READER *Reader = Context;

    while (!Reader->Stop)
    {
        UVC_READ_REQUEST Request;
        DWORD Returned = 0;

        ZeroMemory(&Request, sizeof(Request));
        Request.Header.Size = sizeof(Request);
        Request.Header.FrameExtent = Reader->Size;
        Request.Header.Data = Reader->Buffer;
        Request.FrameInfo.ExtendedHeaderSize = sizeof(KS_FRAME_INFO);
        if (UvcIoctl(Reader->Pin, IOCTL_KS_READ_STREAM, NULL, 0, &Request, sizeof(Request), &Returned))
        {
            if (!Reader->Frames)
                Reader->FirstPicture = Request.FrameInfo.PictureNumber;
            Reader->LastPicture = Request.FrameInfo.PictureNumber;
            Reader->LastDrop = Request.FrameInfo.DropCount;
            InterlockedIncrement(&Reader->Frames);
        }
    }
    return 0;
}

static void
StreamAndWatch(HANDLE Filter, ULONG StillPin, ULONG Pins)
{
    UVC_READER Reader = { 0 };
    HANDLE ReaderHandle = NULL;
    KS_DATAFORMAT_VIDEOINFOHEADER Format;
    HANDLE Capture = NULL, Still = NULL, FilterEvent, PinEvent;
    KSEVENTDATA FilterData, PinData;
    UVC_READ_REQUEST Request, StillRequest;
    OVERLAPPED Overlapped, StillOverlapped;
    PBYTE Buffer, StillBuffer;
    DWORD Size = 1920 * 1080 * 2, Error;

    Buffer = VirtualAlloc(NULL, Size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    StillBuffer = VirtualAlloc(NULL, Size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    ReadFirstFormat(Filter, 0, &Format);
    Error = UvcCreatePin(Filter, 0, &Format, &Capture);
    trace("MEASURE capture pin create %lu\n", Error);
    if (Error)
        return;

    FilterEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    PinEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    EnableEvent(Filter, "filter", FilterEvent, &FilterData, TRUE);
    EnableEvent(Capture, "pin", PinEvent, &PinData, FALSE);
    DumpVideoControl(Filter, "filter", 0, Pins);
    DumpVideoControl(Filter, "filter", 1, Pins);
    DumpVideoControl(Filter, "filter", 7, Pins);
    DumpVideoControl(Capture, "capture", 0, Pins);
    DumpSets(Capture, "capture", IOCTL_KS_PROPERTY, "property", TRUE);
    DumpSets(Capture, "capture", IOCTL_KS_METHOD, "method", FALSE);
    DumpSets(Capture, "capture", IOCTL_KS_ENABLE_EVENT, "event", FALSE);
    DumpDroppedFrames(Capture, "capture", 0);
    ProbePinSets(Capture, "capture");
    SetVideoControlMode(Filter, "filter", 0, 0);
    SetVideoControlMode(Filter, "filter", 0, 1);
    SetVideoControlMode(Filter, "filter", 1, 0);
    SetVideoControlMode(Filter, "filter", 7, 0);
    DumpFrameRates(Filter, "mismatch", 0, 0, 1234, 567);
    DumpFrameRates(Filter, "mismatch", 0, 6, 640, 480);
    DumpFrameRates(Filter, "mismatch", 0, 0, 1280, 720);
    DumpFrameRates(Filter, "mismatch", 0, 500, 640, 480);
    DumpFrameRates(Filter, "mismatch", 7, 0, 640, 480);
    DumpFrameRates(Filter, "mismatch", 1, 0, 640, 480);
    DumpFrameRates(Capture, "capture", 0, 0, Format.VideoInfoHeader.bmiHeader.biWidth, Format.VideoInfoHeader.bmiHeader.biHeight);
    if (StillPin != MAXDWORD)
    {
        DumpVideoControl(Filter, "filter", StillPin, Pins);
        if (ReadFirstFormat(Filter, StillPin, &Format))
        {
            Error = UvcCreatePin(Filter, StillPin, &Format, &Still);
            trace("MEASURE still pin create %lu format %ldx%ld\n", Error, Format.VideoInfoHeader.bmiHeader.biWidth,
                  Format.VideoInfoHeader.bmiHeader.biHeight);
            if (!Error)
                trace("MEASURE videocontrol on the still pin %lu\n", StillVideoControlStatus(Still, StillPin));
        }
    }

    UvcSetPinState(Capture, KSSTATE_ACQUIRE);
    UvcSetPinState(Capture, KSSTATE_PAUSE);
    UvcSetPinState(Capture, KSSTATE_RUN);
    ReadPending(Capture, &Request, Buffer, Size, &Overlapped);
    WaitRead(Capture, &Request, &Overlapped, 2000, "capture read");
    trace("MEASURE first frame info picture %I64d drop %I64d flags %lx\n", Request.FrameInfo.PictureNumber, Request.FrameInfo.DropCount,
          Request.FrameInfo.dwFrameFlags);
    DumpDroppedFrames(Capture, "first read capture", 0);
    DumpDroppedFrames(Filter, "first read filter", 0);

    Reader.Pin = Capture;
    Reader.Buffer = Buffer;
    Reader.Size = Size;
    ReaderHandle = CreateThread(NULL, 0, ReaderThread, &Reader, 0, NULL);

    if (Still)
    {
        UvcSetPinState(Still, KSSTATE_ACQUIRE);
        UvcSetPinState(Still, KSSTATE_PAUSE);
        UvcSetPinState(Still, KSSTATE_RUN);
        ReadPending(Still, &StillRequest, StillBuffer, Size, &StillOverlapped);
        Sleep(200);
        SetTrigger(Filter, "filter", StillPin);
        WaitRead(Still, &StillRequest, &StillOverlapped, 2000, "still read");
        trace("MEASURE still data %02x %02x %02x %02x\n", StillBuffer[0], StillBuffer[1], StillBuffer[2], StillBuffer[3]);
        ok(StillRequest.Header.OptionsFlags == (KSSTREAM_HEADER_OPTIONSF_SPLICEPOINT | KSSTREAM_HEADER_OPTIONSF_TIMEVALID), "Still flags %lx\n", StillRequest.Header.OptionsFlags);
        ok(StillRequest.Header.DataUsed == 614400, "Still used %lu\n", StillRequest.Header.DataUsed);
        ok(!memcmp(StillBuffer, "UVCQ", 4), "Still data %02x %02x %02x %02x\n", StillBuffer[0], StillBuffer[1], StillBuffer[2], StillBuffer[3]);
        ReadPending(Still, &StillRequest, StillBuffer, Size, &StillOverlapped);
        WaitRead(Still, &StillRequest, &StillOverlapped, 3000, "still read from the button");
        ZeroMemory(StillBuffer, 16);
        UvcSetPinState(Still, KSSTATE_PAUSE);
        UvcSetPinState(Still, KSSTATE_STOP);
        CloseHandle(Still);
    }

    DumpFrameRates(Filter, "running filter", 0, 0, Format.VideoInfoHeader.bmiHeader.biWidth, Format.VideoInfoHeader.bmiHeader.biHeight);
    DumpFrameRates(Capture, "running capture", 0, 0, Format.VideoInfoHeader.bmiHeader.biWidth, Format.VideoInfoHeader.bmiHeader.biHeight);
    trace("MEASURE button filter event wait %lu\n", WaitForSingleObject(FilterEvent, 3000));
    trace("MEASURE button pin event wait %lu\n", WaitForSingleObject(PinEvent, 500));
    DumpDroppedFrames(Filter, "running filter", 0);
    DumpDroppedFrames(Capture, "running capture", 0);
    InterlockedExchange(&Reader.Stop, 1);
    WaitForSingleObject(ReaderHandle, INFINITE);
    CloseHandle(ReaderHandle);
    trace("MEASURE capture frames while watching %ld\n", Reader.Frames);
    trace("MEASURE reader frame info first %I64d last %I64d drop %I64d\n", Reader.FirstPicture, Reader.LastPicture, Reader.LastDrop);
    DumpDroppedFrames(Capture, "after reading", 0);
    Sleep(1000);
    DumpDroppedFrames(Capture, "after starving", 0);
    ReadPending(Capture, &Request, Buffer, Size, &Overlapped);
    WaitRead(Capture, &Request, &Overlapped, 2000, "capture read after starving");
    trace("MEASURE starved frame info picture %I64d drop %I64d flags %lx\n", Request.FrameInfo.PictureNumber, Request.FrameInfo.DropCount,
          Request.FrameInfo.dwFrameFlags);
    DumpDroppedFrames(Capture, "after one more read", 0);

    UvcIoctl(Filter, IOCTL_KS_DISABLE_EVENT, &FilterData, sizeof(FilterData), NULL, 0, &Error);
    UvcIoctl(Capture, IOCTL_KS_DISABLE_EVENT, &PinData, sizeof(PinData), NULL, 0, &Error);
    UvcSetPinState(Capture, KSSTATE_PAUSE);
    DumpDroppedFrames(Capture, "paused", 0);
    UvcSetPinState(Capture, KSSTATE_STOP);
    DumpDroppedFrames(Capture, "stopped", 0);
    DumpDroppedFrames(Filter, "stopped filter", 0);
    CloseHandle(Capture);
    DumpDroppedFrames(Filter, "closed filter", ERROR_INVALID_PARAMETER);
    if (ReadFormatByCompression(Filter, 0, UVC_FOURCC('M', 'J', 'P', 'G'), &Format))
    {
        Error = UvcCreatePin(Filter, 0, &Format, &Capture);
        trace("MEASURE mjpeg pin create %lu\n", Error);
        if (!Error)
        {
            DumpDroppedFrames(Capture, "mjpeg capture", UVC_NO_CHECK);
            CloseHandle(Capture);
        }
    }
    else
    {
        trace("MEASURE mjpeg format not found\n");
    }
    CloseHandle(FilterEvent);
    CloseHandle(PinEvent);
    VirtualFree(Buffer, 0, MEM_RELEASE);
    VirtualFree(StillBuffer, 0, MEM_RELEASE);
}

static void
MeasureRate(HANDLE Filter, ULONG Readers)
{
    KS_DATAFORMAT_VIDEOINFOHEADER Format;
    UVC_READER Reader[3];
    HANDLE Threads[3], Pin = NULL;
    DWORD Size = 640 * 480 * 2, Start, Error;
    LONG Frames = 0;
    ULONG Index;

    UvcBuildFormat(&Format, &UvcSubtypeYuy2, UVC_FOURCC('Y', 'U', 'Y', '2'), 640, 480, 333333);
    Error = UvcCreatePin(Filter, 0, &Format, &Pin);
    if (Error)
    {
        trace("MEASURE rate create %lu\n", Error);
        return;
    }
    UvcSetPinState(Pin, KSSTATE_ACQUIRE);
    UvcSetPinState(Pin, KSSTATE_PAUSE);
    UvcSetPinState(Pin, KSSTATE_RUN);
    ZeroMemory(Reader, sizeof(Reader));
    Start = GetTickCount();
    for (Index = 0; Index < Readers; Index++)
    {
        Reader[Index].Pin = Pin;
        Reader[Index].Size = Size;
        Reader[Index].Buffer = VirtualAlloc(NULL, Size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        Threads[Index] = CreateThread(NULL, 0, ReaderThread, &Reader[Index], 0, NULL);
    }
    Sleep(3000);
    for (Index = 0; Index < Readers; Index++)
    {
        InterlockedExchange(&Reader[Index].Stop, 1);
        Frames += Reader[Index].Frames;
    }
    trace("MEASURE rate readers %lu frames %ld in %lu ms\n", Readers, Frames, GetTickCount() - Start);
    if (Readers == 3)
        ok(Frames >= 75, "Only %ld frames in 3 seconds at 30 per second\n", Frames);
    for (Index = 0; Index < Readers; Index++)
    {
        WaitForSingleObject(Threads[Index], INFINITE);
        CloseHandle(Threads[Index]);
        VirtualFree(Reader[Index].Buffer, 0, MEM_RELEASE);
    }
    UvcSetPinState(Pin, KSSTATE_PAUSE);
    UvcSetPinState(Pin, KSSTATE_STOP);
    CloseHandle(Pin);
}

static void
StillAlone(HANDLE Filter, ULONG StillPin)
{
    KS_DATAFORMAT_VIDEOINFOHEADER Format;
    UVC_READ_REQUEST Request;
    OVERLAPPED Overlapped;
    HANDLE Still = NULL;
    DWORD Size = 1920 * 1080 * 2, Error;
    PBYTE Buffer;

    if (StillPin == MAXDWORD || !ReadFirstFormat(Filter, StillPin, &Format))
        return;

    Buffer = VirtualAlloc(NULL, Size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    Error = UvcCreatePin(Filter, StillPin, &Format, &Still);
    trace("MEASURE alone still create %lu\n", Error);
    ok(Error == ERROR_INVALID_PARAMETER, "Still pin without a capture pin: error %lu\n", Error);
    if (!Error)
    {
        trace("MEASURE alone state %lu\n", UvcSetPinState(Still, KSSTATE_ACQUIRE));
        trace("MEASURE alone state %lu\n", UvcSetPinState(Still, KSSTATE_PAUSE));
        trace("MEASURE alone state %lu\n", UvcSetPinState(Still, KSSTATE_RUN));
        ReadPending(Still, &Request, Buffer, Size, &Overlapped);
        Sleep(200);
        SetTrigger(Filter, "alone", StillPin);
        WaitRead(Still, &Request, &Overlapped, 1500, "alone read");
        UvcSetPinState(Still, KSSTATE_PAUSE);
        UvcSetPinState(Still, KSSTATE_STOP);
        CloseHandle(Still);
    }
    VirtualFree(Buffer, 0, MEM_RELEASE);
}

START_TEST(KsCaps)
{
    WCHAR Path[512];
    HANDLE Filter;
    ULONG StillPin, Pins;

    if (!UvcFindCameraId(&UvcCategoryCapture, L"vid_", Path, ARRAYSIZE(Path)))
    {
        skip("No usb-uvc test camera\n");
        return;
    }

    Filter = UvcOpenFilter(Path);
    ok(Filter != INVALID_HANDLE_VALUE, "Opening the filter failed %lu\n", GetLastError());
    if (Filter == INVALID_HANDLE_VALUE)
        return;

    winetest_mute_threshold = 100000;
    DumpComponent(Filter, Path);
    DumpTopology(Filter);
    DumpSets(Filter, "filter", IOCTL_KS_PROPERTY, "property", TRUE);
    DumpSets(Filter, "filter", IOCTL_KS_METHOD, "method", FALSE);
    DumpSets(Filter, "filter", IOCTL_KS_ENABLE_EVENT, "event", FALSE);
    DumpDroppedFrames(Filter, "filter", ERROR_INVALID_PARAMETER);
    ProbeFilterSets(Filter, HasAllControls(Filter));
    Pins = DumpPins(Filter, &StillPin);
    if (!HasAllControls(Filter))
        DumpControls(Filter);
    DumpNodes(Filter);
    if (HasAllControls(Filter))
    {
        CheckNodes(Filter);
        CheckRanges(Filter, &UvcPropsetProcAmp, ExpectProcAmpRanges, ARRAYSIZE(ExpectProcAmpRanges));
        CheckRanges(Filter, &UvcPropsetCamera, ExpectCameraRanges, ARRAYSIZE(ExpectCameraRanges));
        CheckCameraSets(Filter);
        CheckWhiteBalanceComponent(Filter);
    }
    StillAlone(Filter, StillPin);
    DumpAllFrameRates(Filter);
    StreamAndWatch(Filter, StillPin, Pins);
    MeasureRate(Filter, 1);
    MeasureRate(Filter, 3);
    CloseHandle(Filter);
}
