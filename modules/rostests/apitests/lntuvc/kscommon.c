/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared helpers for the USB video class tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lntuvc_test.h"

const GUID UvcCategoryCapture = { STATIC_KSCATEGORY_CAPTURE };
const GUID UvcCategoryVideo = { 0x6994AD05L, 0x93EF, 0x11D0, { 0xA3, 0xCC, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96 } };
const GUID UvcCategoryCamera = { 0xE5323777, 0xF976, 0x4F5B, { 0x9B, 0x55, 0xB9, 0x46, 0x99, 0xC4, 0x6E, 0x44 } };
const GUID UvcPropsetPin = { STATIC_KSPROPSETID_Pin };
const GUID UvcPropsetTopology = { STATIC_KSPROPSETID_Topology };
const GUID UvcPropsetConnection = { STATIC_KSPROPSETID_Connection };
const GUID UvcPropsetGeneral = { STATIC_KSPROPSETID_General };
const GUID UvcPropsetProcAmp = { 0xC6E13360L, 0x30AC, 0x11D0, { 0xA1, 0x8C, 0x00, 0xA0, 0xC9, 0x11, 0x89, 0x56 } };
const GUID UvcPropsetCamera = { 0xC6E13370L, 0x30AC, 0x11D0, { 0xA1, 0x8C, 0x00, 0xA0, 0xC9, 0x11, 0x89, 0x56 } };
const GUID UvcPropsetStream = { STATIC_KSPROPSETID_Stream };
const GUID UvcExtensionUnit = { 0xF00DCAFE, 0x3210, 0x7654, { 0x98, 0xBA, 0xDC, 0xFE, 0x01, 0x23, 0x45, 0x67 } };
const GUID UvcFormatVideo = { STATIC_KSDATAFORMAT_TYPE_VIDEO };
const GUID UvcSpecifierVideoInfo = { 0x05589F80L, 0xC356, 0x11CE, { 0xBF, 0x01, 0x00, 0xAA, 0x00, 0x55, 0x59, 0x5A } };
const GUID UvcSpecifierVideoInfo2 = { 0xF72A76A0L, 0xEB0A, 0x11D0, { 0xAC, 0xE4, 0x00, 0x00, 0xC0, 0xCC, 0x16, 0xBA } };
const GUID UvcPinCapture = { 0xFB6C4281L, 0x0353, 0x11D1, { 0x90, 0x5F, 0x00, 0x00, 0xC0, 0xCC, 0x16, 0xBA } };
const GUID UvcPinBridge = { 0x3EBC7959L, 0x3310, 0x493B, { 0xAA, 0x81, 0xC7, 0xE1, 0x32, 0xD5, 0x6F, 0x71 } };
const GUID UvcNodeCamera = { 0xDFF229E6L, 0xF70F, 0x11D0, { 0xB9, 0x17, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96 } };
const GUID UvcNodeProcessing = { 0xDFF229E5L, 0xF70F, 0x11D0, { 0xB9, 0x17, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96 } };
const GUID UvcNodeExtension = { 0x941C7AC0L, 0xC559, 0x11D0, { 0x8A, 0x2B, 0x00, 0xA0, 0xC9, 0x25, 0x5A, 0xC1 } };
const GUID UvcNodeStreaming = { 0xDFF229E1L, 0xF70F, 0x11D0, { 0xB9, 0x17, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96 } };
const GUID UvcComponentCamera = { 0x808880FBL, 0xCE0D, 0x4302, { 0xB1, 0x12, 0xC6, 0x77, 0xBB, 0xBB, 0xCC, 0x0E } };
const GUID UvcMemoryNonPaged = { 0x4A6D5FC4L, 0x7895, 0x11D1, { 0xB0, 0x69, 0x00, 0xA0, 0xC9, 0x06, 0x28, 0x02 } };
const GUID UvcSubtypeYuy2 = { 0x32595559, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
const GUID UvcSubtypeMjpg = { 0x47504A4D, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
const GUID UvcInterfaceStandard = { STATIC_KSINTERFACESETID_Standard };
const GUID UvcMediumStandard = { STATIC_KSMEDIUMSETID_Standard };

DWORD UvcIoctlTimeout = 5000;

BOOL
UvcFindCamera(const GUID *Category, WCHAR *Path, DWORD PathChars)
{
    return UvcFindCameraId(Category, L"vid_46f4&pid_0099", Path, PathChars);
}

BOOL
UvcFindCameraId(const GUID *Category, const WCHAR *Id, WCHAR *Path, DWORD PathChars)
{
    HDEVINFO Set;
    SP_DEVICE_INTERFACE_DATA Interface;
    BYTE Buffer[1024];
    SP_DEVICE_INTERFACE_DETAIL_DATA_W *Detail = (PVOID)Buffer;
    DWORD Index;
    BOOL Found = FALSE;

    Set = SetupDiGetClassDevsW(Category, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (Set == INVALID_HANDLE_VALUE)
        return FALSE;

    Interface.cbSize = sizeof(Interface);
    for (Index = 0; SetupDiEnumDeviceInterfaces(Set, NULL, Category, Index, &Interface); Index++)
    {
        Detail->cbSize = sizeof(*Detail);
        if (!SetupDiGetDeviceInterfaceDetailW(Set, &Interface, Detail, sizeof(Buffer), NULL, NULL))
            continue;
        if (wcsstr(_wcslwr(Detail->DevicePath), Id))
        {
            lstrcpynW(Path, Detail->DevicePath, PathChars);
            Found = TRUE;
        }
    }

    SetupDiDestroyDeviceInfoList(Set);
    return Found;
}

HANDLE
UvcOpenFilter(const WCHAR *Path)
{
    return CreateFileW(Path,
                       GENERIC_READ | GENERIC_WRITE,
                       0,
                       NULL,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                       NULL);
}

BOOL
UvcIoctl(HANDLE Handle, DWORD Code, PVOID In, DWORD InLength, PVOID Out, DWORD OutLength, DWORD *Returned)
{
    OVERLAPPED Overlapped = { 0 };
    DWORD Local = 0;
    BOOL Result;

    Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Result = DeviceIoControl(Handle, Code, In, InLength, Out, OutLength, &Local, &Overlapped);
    if (!Result && GetLastError() == ERROR_IO_PENDING)
    {
        if (WaitForSingleObject(Overlapped.hEvent, UvcIoctlTimeout) != WAIT_OBJECT_0)
        {
            CancelIo(Handle);
            GetOverlappedResult(Handle, &Overlapped, &Local, TRUE);
            CloseHandle(Overlapped.hEvent);
            SetLastError(WAIT_TIMEOUT);
            if (Returned)
                *Returned = 0;
            return FALSE;
        }
        Result = GetOverlappedResult(Handle, &Overlapped, &Local, FALSE);
    }
    CloseHandle(Overlapped.hEvent);
    if (Returned)
        *Returned = Local;
    return Result;
}

DWORD
UvcGetProperty(HANDLE Handle, const GUID *Set, ULONG Id, PVOID Data, DWORD Length, DWORD *Returned)
{
    KSPROPERTY Property;

    Property.Set = *Set;
    Property.Id = Id;
    Property.Flags = KSPROPERTY_TYPE_GET;
    return UvcIoctl(Handle, IOCTL_KS_PROPERTY, &Property, sizeof(Property), Data, Length, Returned) ? ERROR_SUCCESS : GetLastError();
}

DWORD
UvcSetProperty(HANDLE Handle, const GUID *Set, ULONG Id, PVOID Data, DWORD Length)
{
    KSPROPERTY Property;
    DWORD Returned;

    Property.Set = *Set;
    Property.Id = Id;
    Property.Flags = KSPROPERTY_TYPE_SET;
    return UvcIoctl(Handle, IOCTL_KS_PROPERTY, &Property, sizeof(Property), Data, Length, &Returned) ? ERROR_SUCCESS : GetLastError();
}

DWORD
UvcGetPinProperty(HANDLE Filter, ULONG PinId, ULONG Id, PVOID Data, DWORD Length, DWORD *Returned)
{
    KSP_PIN Property;

    Property.Property.Set = UvcPropsetPin;
    Property.Property.Id = Id;
    Property.Property.Flags = KSPROPERTY_TYPE_GET;
    Property.PinId = PinId;
    Property.Reserved = 0;
    return UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Property, sizeof(Property), Data, Length, Returned) ? ERROR_SUCCESS : GetLastError();
}

DWORD
UvcSetPinState(HANDLE Pin, KSSTATE State)
{
    return UvcSetProperty(Pin, &UvcPropsetConnection, KSPROPERTY_CONNECTION_STATE, &State, sizeof(State));
}

void
UvcBuildFormat(KS_DATAFORMAT_VIDEOINFOHEADER *Format, const GUID *Subtype, DWORD FourCc, LONG Width, LONG Height, LONGLONG Interval)
{
    DWORD ImageSize = Width * Height * 2;

    ZeroMemory(Format, sizeof(*Format));
    Format->DataFormat.FormatSize = sizeof(*Format);
    Format->DataFormat.SampleSize = ImageSize;
    Format->DataFormat.MajorFormat = UvcFormatVideo;
    Format->DataFormat.SubFormat = *Subtype;
    Format->DataFormat.Specifier = UvcSpecifierVideoInfo;
    Format->VideoInfoHeader.dwBitRate = (DWORD)(ImageSize * 8 * 10000000LL / Interval);
    Format->VideoInfoHeader.AvgTimePerFrame = Interval;
    Format->VideoInfoHeader.bmiHeader.biSize = sizeof(KS_BITMAPINFOHEADER);
    Format->VideoInfoHeader.bmiHeader.biWidth = Width;
    Format->VideoInfoHeader.bmiHeader.biHeight = Height;
    Format->VideoInfoHeader.bmiHeader.biPlanes = 1;
    Format->VideoInfoHeader.bmiHeader.biBitCount = 16;
    Format->VideoInfoHeader.bmiHeader.biCompression = FourCc;
    Format->VideoInfoHeader.bmiHeader.biSizeImage = ImageSize;
}

DWORD
UvcCreatePin(HANDLE Filter, ULONG PinId, const KS_DATAFORMAT_VIDEOINFOHEADER *Format, HANDLE *Pin)
{
    struct
    {
        KSPIN_CONNECT Connect;
        KS_DATAFORMAT_VIDEOINFOHEADER Format;
    } Request;

    ZeroMemory(&Request, sizeof(Request));
    Request.Connect.Interface.Set = UvcInterfaceStandard;
    Request.Connect.Interface.Id = KSINTERFACE_STANDARD_STREAMING;
    Request.Connect.Medium.Set = UvcMediumStandard;
    Request.Connect.Medium.Id = KSMEDIUM_TYPE_ANYINSTANCE;
    Request.Connect.PinId = PinId;
    Request.Connect.Priority.PriorityClass = KSPRIORITY_NORMAL;
    Request.Connect.Priority.PrioritySubClass = KSPRIORITY_NORMAL;
    Request.Format = *Format;

    *Pin = NULL;
    return KsCreatePin(Filter, &Request.Connect, GENERIC_READ, Pin);
}

ULONG
UvcCheckYuy2Frame(const BYTE *Data, LONG Width, LONG Height, ULONG *Sequence)
{
    static const BYTE Bars[8][3] =
    {
        { 235, 128, 128 }, { 210, 16, 146 }, { 170, 166, 16 }, { 145, 54, 34 },
        { 106, 202, 222 }, { 81, 90, 240 }, { 41, 240, 110 }, { 16, 128, 128 },
    };
    ULONG Errors = 0;
    ULONG BoxX, BoxY;
    LONG X, Y;
    const BYTE *P = Data;

    if (memcmp(Data, "UVCQ", 4) ||
        *(const WORD *)(Data + 8) != Width ||
        *(const WORD *)(Data + 10) != Height)
    {
        return (ULONG)-1;
    }

    *Sequence = *(const ULONG *)(Data + 4);
    BoxX = (*Sequence * 4) % (Width - 64) & ~1u;
    BoxY = Height / 2 - 32;

    for (Y = 0; Y < Height; Y++)
    {
        for (X = 0; X < Width; X += 2, P += 4)
        {
            const BYTE *C = Bars[X * 8 / Width];
            BOOL Box = (ULONG)X >= BoxX && (ULONG)X < BoxX + 64 && (ULONG)Y >= BoxY && (ULONG)Y < BoxY + 64;
            BYTE Expect[4];

            if (Y == 0 && X < 12)
                continue;

            Expect[0] = Box ? 235 : C[0];
            Expect[1] = Box ? 128 : C[1];
            Expect[2] = Box ? 235 : C[0];
            Expect[3] = Box ? 128 : C[2];
            if (memcmp(P, Expect, 4))
                Errors++;
        }
    }

    return Errors;
}
