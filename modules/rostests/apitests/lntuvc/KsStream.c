/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for streaming from the USB video class capture pin
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lntuvc_test.h"

typedef struct _UVC_FRAME_REQUEST
{
    KSSTREAM_HEADER Header;
    KS_FRAME_INFO FrameInfo;
} UVC_FRAME_REQUEST;

static void
StreamFormat(HANDLE Filter, const GUID *Subtype, DWORD FourCc, LONG Width, LONG Height, LONGLONG Interval)
{
    KS_DATAFORMAT_VIDEOINFOHEADER Format;
    UVC_FRAME_REQUEST *Request;
    HANDLE Pin = NULL;
    DWORD Error, Returned, Start, Elapsed;
    DWORD Size = Width * Height * (IsEqualGUID(Subtype, &UvcSubtypeYuy2) ? 2 : 3);
    PBYTE Buffer;
    ULONG Frame, Good = 0, Sequence = 0, LastSequence = (ULONG)-1;
    DWORD Bits = IsEqualGUID(Subtype, &UvcSubtypeYuy2) ? 16 : 24;
    KSALLOCATOR_FRAMING_EX Framing[2];
    BOOL Result;
    KSSTATE State;

    trace("Stream %s %ldx%ld interval %ld\n", wine_dbgstr_guid(Subtype), Width, Height, (LONG)Interval);
    UvcBuildFormat(&Format, Subtype, FourCc, Width, Height, Interval);
    Format.VideoInfoHeader.bmiHeader.biBitCount = (WORD)Bits;
    Format.VideoInfoHeader.bmiHeader.biSizeImage = Size;
    Format.DataFormat.SampleSize = Size;
    Format.VideoInfoHeader.dwBitRate = (DWORD)(Size * 8ULL * (10000000 / Interval));
    Error = UvcCreatePin(Filter, 0, &Format, &Pin);
    ok(Error == ERROR_SUCCESS, "KsCreatePin failed %lu\n", Error);
    if (Error)
        return;

    ZeroMemory(Framing, sizeof(Framing));
    Error = UvcGetProperty(Pin, &UvcPropsetConnection, KSPROPERTY_CONNECTION_ALLOCATORFRAMING_EX, Framing, sizeof(Framing), &Returned);
    ok(Error == ERROR_SUCCESS && Returned == sizeof(KSALLOCATOR_FRAMING_EX), "ALLOCATORFRAMING_EX err %lu ret %lu\n", Error, Returned);
    ok(Framing[0].CountItems == 1, "Framing items %lu\n", Framing[0].CountItems);
    ok(Framing[0].PinFlags == 0 && Framing[0].PinWeight == 0, "Framing pin %lx %lu\n", Framing[0].PinFlags, Framing[0].PinWeight);
    ok(Framing[0].OutputCompression.RatioNumerator == 1 && Framing[0].OutputCompression.RatioDenominator == 1 &&
       Framing[0].OutputCompression.RatioConstantMargin == 0, "Framing compression\n");
    ok(IsEqualGUID(&Framing[0].FramingItem[0].MemoryType, &UvcMemoryNonPaged), "Framing memory %s\n", wine_dbgstr_guid(&Framing[0].FramingItem[0].MemoryType));
    ok(Framing[0].FramingItem[0].Flags == 0x80004002, "Framing flags %lx\n", Framing[0].FramingItem[0].Flags);
    ok(Framing[0].FramingItem[0].Frames == 10, "Framing frames %lu\n", Framing[0].FramingItem[0].Frames);
    ok(Framing[0].FramingItem[0].FileAlignment == 0, "Framing alignment %lu\n", Framing[0].FramingItem[0].FileAlignment);
    ok(Framing[0].FramingItem[0].PhysicalRange.MinFrameSize == (DWORD)(Width * Height * 2), "Framing physical %lu\n", Framing[0].FramingItem[0].PhysicalRange.MinFrameSize);
    ok(Framing[0].FramingItem[0].PhysicalRange.MaxFrameSize == (DWORD)(Width * Height * 2), "Framing physical %lu\n", Framing[0].FramingItem[0].PhysicalRange.MaxFrameSize);
    ok(Framing[0].FramingItem[0].FramingRange.Range.MinFrameSize == (DWORD)(Width * Height * 2), "Framing range %lu\n", Framing[0].FramingItem[0].FramingRange.Range.MinFrameSize);
    ok(Framing[0].FramingItem[0].FramingRange.Range.MaxFrameSize == (DWORD)(Width * Height * 2), "Framing range %lu\n", Framing[0].FramingItem[0].FramingRange.Range.MaxFrameSize);
    {
        BYTE Current[512];
        KSDATAFORMAT *CurrentFormat = (PVOID)Current;

        ZeroMemory(Current, sizeof(Current));
        Error = UvcGetProperty(Pin, &UvcPropsetConnection, KSPROPERTY_CONNECTION_DATAFORMAT, Current, sizeof(Current), &Returned);
        ok(Error == ERROR_SUCCESS && Returned == sizeof(Format) && CurrentFormat->FormatSize == sizeof(Format) && CurrentFormat->SampleSize == Size,
           "CONNECTION_DATAFORMAT err %lu ret %lu size %lu sample %lu\n", Error, Returned, CurrentFormat->FormatSize, CurrentFormat->SampleSize);
    }

    State = KSSTATE_STOP;
    Error = UvcGetProperty(Pin, &UvcPropsetConnection, KSPROPERTY_CONNECTION_STATE, &State, sizeof(State), &Returned);
    ok(Error == ERROR_SUCCESS && State == KSSTATE_STOP, "Initial state err %lu state %d\n", Error, State);

    ok(UvcSetPinState(Pin, KSSTATE_ACQUIRE) == ERROR_SUCCESS, "ACQUIRE failed %lu\n", GetLastError());
    ok(UvcSetPinState(Pin, KSSTATE_PAUSE) == ERROR_SUCCESS, "PAUSE failed %lu\n", GetLastError());
    ok(UvcSetPinState(Pin, KSSTATE_RUN) == ERROR_SUCCESS, "RUN failed %lu\n", GetLastError());

    Buffer = VirtualAlloc(NULL, Size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    Request = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Request));
    Start = GetTickCount();
    for (Frame = 0; Frame < UVC_TEST_FRAMES; Frame++)
    {
        ZeroMemory(Request, sizeof(*Request));
        Request->Header.Size = sizeof(*Request);
        Request->Header.FrameExtent = Size;
        Request->Header.Data = Buffer;
        Request->FrameInfo.ExtendedHeaderSize = sizeof(KS_FRAME_INFO);

        Result = UvcIoctl(Pin, IOCTL_KS_READ_STREAM, NULL, 0, Request, sizeof(*Request), &Returned);
        ok(Result, "Frame %lu read failed %lu\n", Frame, GetLastError());
        if (!Result)
            break;

        ok(Returned == sizeof(*Request), "Frame %lu returned %lu\n", Frame, Returned);
        ok(Request->Header.Size == sizeof(*Request), "Frame %lu header size %lu\n", Frame, Request->Header.Size);
        ok(Request->Header.FrameExtent == Size, "Frame %lu extent %lu\n", Frame, Request->Header.FrameExtent);
        ok(Request->Header.Data == Buffer, "Frame %lu data pointer changed to %p\n", Frame, Request->Header.Data);
        if (Frame == 0)
            ok(Request->Header.OptionsFlags == 0x111, "Frame 0 options %lx\n", Request->Header.OptionsFlags);
        else
            ok((Request->Header.OptionsFlags & ~KSSTREAM_HEADER_OPTIONSF_DATADISCONTINUITY) == 0x111, "Frame %lu options %lx\n", Frame, Request->Header.OptionsFlags);
        ok(Request->Header.Duration == Interval, "Frame %lu duration %ld\n", Frame, (LONG)Request->Header.Duration);
        ok(Request->Header.PresentationTime.Time != 0, "Frame %lu has no time\n", Frame);
        ok(Request->Header.PresentationTime.Numerator == 0 && Request->Header.PresentationTime.Denominator == 0, "Frame %lu time scale %lu/%lu\n",
           Frame, Request->Header.PresentationTime.Numerator, Request->Header.PresentationTime.Denominator);
        ok(Request->FrameInfo.ExtendedHeaderSize == sizeof(KS_FRAME_INFO), "Frame %lu extended size %lu\n", Frame, Request->FrameInfo.ExtendedHeaderSize);
        ok(Request->FrameInfo.dwFrameFlags == 0, "Frame %lu flags %lx\n", Frame, Request->FrameInfo.dwFrameFlags);
        ok(Request->FrameInfo.PictureNumber == 0 && Request->FrameInfo.DropCount == 0, "Frame %lu picture %ld drops %ld\n", Frame,
           (LONG)Request->FrameInfo.PictureNumber, (LONG)Request->FrameInfo.DropCount);

        if (IsEqualGUID(Subtype, &UvcSubtypeYuy2))
        {
            ULONG Errors = UvcCheckYuy2Frame(Buffer, Width, Height, &Sequence);

            ok(Request->Header.DataUsed == Size, "Frame %lu DataUsed %lu, expected %lu\n", Frame, Request->Header.DataUsed, Size);
            ok(Errors == 0, "Frame %lu has %lu wrong pixels (first bytes %02x %02x %02x %02x)\n", Frame, Errors, Buffer[0], Buffer[1], Buffer[2], Buffer[3]);
            if (Errors == 0)
            {
                ok(LastSequence == (ULONG)-1 || Sequence > LastSequence, "Frame %lu sequence %lu after %lu\n", Frame, Sequence, LastSequence);
                LastSequence = Sequence;
                Good++;
            }
        }
        else
        {
            ok(Request->Header.DataUsed > 16 && Request->Header.DataUsed < Size, "Frame %lu DataUsed %lu\n", Frame, Request->Header.DataUsed);
            ok(Buffer[0] == 0xFF && Buffer[1] == 0xD8, "Frame %lu does not start with SOI: %02x %02x\n", Frame, Buffer[0], Buffer[1]);
            ok(!memcmp(Buffer + 6, "UVCQ", 4), "Frame %lu has no signature\n", Frame);
            ok(Buffer[Request->Header.DataUsed - 2] == 0xFF && Buffer[Request->Header.DataUsed - 1] == 0xD9, "Frame %lu does not end with EOI\n", Frame);
            Good++;
        }
    }
    Elapsed = GetTickCount() - Start;
    trace("%lu good frames in %lu ms\n", Good, Elapsed);
    ok(Good == UVC_TEST_FRAMES, "Only %lu good frames of %u\n", Good, UVC_TEST_FRAMES);

    ok(UvcSetPinState(Pin, KSSTATE_PAUSE) == ERROR_SUCCESS, "PAUSE failed %lu\n", GetLastError());
    ok(UvcSetPinState(Pin, KSSTATE_ACQUIRE) == ERROR_SUCCESS, "ACQUIRE failed %lu\n", GetLastError());
    ok(UvcSetPinState(Pin, KSSTATE_STOP) == ERROR_SUCCESS, "STOP failed %lu\n", GetLastError());

    HeapFree(GetProcessHeap(), 0, Request);
    VirtualFree(Buffer, 0, MEM_RELEASE);
    CloseHandle(Pin);
}

static BOOL
IsWow64(void)
{
    BOOL Wow64 = FALSE;

    return IsWow64Process(GetCurrentProcess(), &Wow64) && Wow64;
}

static void
ReadOnce(HANDLE Pin, DWORD Size, DWORD HeaderSize, const char *What, BOOL ExpectResult, DWORD ExpectError, DWORD ExpectReturned, DWORD ExpectUsed)
{
    UVC_FRAME_REQUEST Request;
    PBYTE Buffer;
    DWORD Returned = 0, Error;
    BOOL Result;

    trace("%s\n", What);
    Buffer = VirtualAlloc(NULL, 4 * 1024 * 1024, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    ZeroMemory(&Request, sizeof(Request));
    Request.Header.Size = HeaderSize;
    Request.Header.FrameExtent = Size;
    Request.Header.Data = Buffer;
    Request.FrameInfo.ExtendedHeaderSize = sizeof(KS_FRAME_INFO);
    Result = UvcIoctl(Pin, IOCTL_KS_READ_STREAM, NULL, 0, &Request, HeaderSize, &Returned);
    Error = Result ? 0 : GetLastError();
    ok(Result == ExpectResult && Error == ExpectError, "%s: result %d err %lu\n", What, Result, Error);
    ok(Returned == ExpectReturned, "%s: returned %lu\n", What, Returned);
    ok(Request.Header.DataUsed == ExpectUsed, "%s: used %lu\n", What, Request.Header.DataUsed);
    ok(Request.Header.FrameExtent == Size, "%s: extent %lu\n", What, Request.Header.FrameExtent);
    VirtualFree(Buffer, 0, MEM_RELEASE);
}

static void
ReadAcrossStop(HANDLE Pin, DWORD Size)
{
    UVC_FRAME_REQUEST Request;
    OVERLAPPED Overlapped = { 0 };
    PBYTE Buffer;
    DWORD Returned = 0;
    BOOL Result;

    Buffer = VirtualAlloc(NULL, Size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    ZeroMemory(&Request, sizeof(Request));
    Request.Header.Size = sizeof(Request);
    Request.Header.FrameExtent = Size;
    Request.Header.Data = Buffer;
    Result = DeviceIoControl(Pin, IOCTL_KS_READ_STREAM, NULL, 0, &Request, sizeof(Request), &Returned, &Overlapped);
    ok(!Result && GetLastError() == ERROR_IO_PENDING, "Read in PAUSE: result %d err %lu\n", Result, GetLastError());

    ok(UvcSetPinState(Pin, KSSTATE_STOP) == ERROR_SUCCESS, "STOP failed %lu\n", GetLastError());
    if (WaitForSingleObject(Overlapped.hEvent, 2000) == WAIT_OBJECT_0)
    {
        Result = GetOverlappedResult(Pin, &Overlapped, &Returned, FALSE);
        ok(Result && Returned == sizeof(Request), "Read across STOP: result %d err %lu ret %lu\n", Result, Result ? 0 : GetLastError(), Returned);
        ok(Request.Header.DataUsed == 0, "Read across STOP: used %lu\n", Request.Header.DataUsed);
        ok(Request.Header.Data == Buffer, "Read across STOP: data pointer changed to %p\n", Request.Header.Data);
    }
    else
    {
        ok(0, "Read across STOP did not complete\n");
        CancelIo(Pin);
        GetOverlappedResult(Pin, &Overlapped, &Returned, TRUE);
    }
    CloseHandle(Overlapped.hEvent);
    VirtualFree(Buffer, 0, MEM_RELEASE);
}

static void
StreamEdges(HANDLE Filter)
{
    KS_DATAFORMAT_VIDEOINFOHEADER Format;
    HANDLE Pin = NULL;
    DWORD Size = 320 * 240 * 2;
    DWORD Header = sizeof(UVC_FRAME_REQUEST);
    DWORD Error;

    UvcBuildFormat(&Format, &UvcSubtypeYuy2, UVC_FOURCC('Y', 'U', 'Y', '2'), 320, 240, 333333);
    Error = UvcCreatePin(Filter, 0, &Format, &Pin);
    ok(Error == ERROR_SUCCESS, "KsCreatePin failed %lu\n", Error);
    if (Error)
        return;

    UvcIoctlTimeout = 300;
    ReadOnce(Pin, Size, Header, "Read in STOP", FALSE, ERROR_INVALID_FUNCTION, 0, 0);
    UvcSetPinState(Pin, KSSTATE_ACQUIRE);
    ReadOnce(Pin, Size, Header, "Read in ACQUIRE", FALSE, WAIT_TIMEOUT, 0, 0);
    UvcSetPinState(Pin, KSSTATE_PAUSE);
    UvcSetPinState(Pin, KSSTATE_RUN);
    UvcIoctlTimeout = 5000;
    ReadOnce(Pin, Size, Header, "Read in RUN", TRUE, 0, Header, Size);
    UvcIoctlTimeout = 300;
    ReadOnce(Pin, Size / 2, Header, "Read with half a buffer", FALSE, WAIT_TIMEOUT, 0, 0);
    ReadOnce(Pin, 0, Header, "Read with no buffer", FALSE, IsWow64() ? ERROR_NOACCESS : WAIT_TIMEOUT, 0, 0);
    UvcIoctlTimeout = 5000;
    ReadOnce(Pin, Size * 2, Header, "Read with a double buffer", TRUE, 0, Header, Size);
    ReadOnce(Pin, Size, sizeof(KSSTREAM_HEADER), "Read with a bare header", TRUE, 0, sizeof(KSSTREAM_HEADER), Size);
    UvcSetPinState(Pin, KSSTATE_PAUSE);
    UvcIoctlTimeout = 300;
    ReadOnce(Pin, Size, Header, "Read in PAUSE", FALSE, WAIT_TIMEOUT, 0, 0);
    UvcIoctlTimeout = 5000;
    ReadAcrossStop(Pin, Size);
    CloseHandle(Pin);
}

static void
RestartCycles(HANDLE Filter)
{
    KS_DATAFORMAT_VIDEOINFOHEADER Format;
    DWORD Size = 160 * 120 * 2;
    ULONG Cycle;

    UvcBuildFormat(&Format, &UvcSubtypeYuy2, UVC_FOURCC('Y', 'U', 'Y', '2'), 160, 120, 333333);

    for (Cycle = 0; Cycle < 12; Cycle++)
    {
        HANDLE Pin = NULL;
        DWORD Error = UvcCreatePin(Filter, 0, &Format, &Pin);

        ok(Error == ERROR_SUCCESS, "Cycle %lu: KsCreatePin failed %lu\n", Cycle, Error);
        if (Error)
            return;

        UvcSetPinState(Pin, KSSTATE_ACQUIRE);
        UvcSetPinState(Pin, KSSTATE_PAUSE);
        UvcSetPinState(Pin, KSSTATE_RUN);
        ReadOnce(Pin, Size, sizeof(UVC_FRAME_REQUEST), "Read after a restart", TRUE, 0, sizeof(UVC_FRAME_REQUEST), Size);
        UvcSetPinState(Pin, KSSTATE_PAUSE);
        UvcSetPinState(Pin, KSSTATE_STOP);
        CloseHandle(Pin);
    }
}

static void
CheckIntersection(HANDLE Filter)
{
    static BYTE Ranges[64 * 1024];
    KSMULTIPLE_ITEM *Multiple = (PVOID)Ranges;
    KS_DATARANGE_VIDEO *First = (PVOID)(Multiple + 1);
    struct
    {
        KSP_PIN Pin;
        KSMULTIPLE_ITEM Items;
        KS_DATARANGE_VIDEO Range;
    } Request;
    KS_DATAFORMAT_VIDEOINFOHEADER Output;
    DWORD Returned = 0, Error;
    BOOL Result;

    Error = UvcGetPinProperty(Filter, 0, KSPROPERTY_PIN_DATARANGES, Ranges, sizeof(Ranges), &Returned);
    ok(Error == ERROR_SUCCESS, "PIN_DATARANGES failed %lu\n", Error);
    if (Error)
        return;

    ZeroMemory(&Request, sizeof(Request));
    Request.Pin.Property.Set = UvcPropsetPin;
    Request.Pin.Property.Id = KSPROPERTY_PIN_DATAINTERSECTION;
    Request.Pin.Property.Flags = KSPROPERTY_TYPE_GET;
    Request.Pin.PinId = 0;
    Request.Items.Size = sizeof(KSMULTIPLE_ITEM) + sizeof(KS_DATARANGE_VIDEO);
    Request.Items.Count = 1;
    Request.Range = *First;

    ZeroMemory(&Output, sizeof(Output));
    Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Request, sizeof(Request), &Output, sizeof(Output), &Returned);
    ok(Result && Returned == sizeof(Output), "DATAINTERSECTION err %lu ret %lu\n", Result ? 0 : GetLastError(), Returned);
    if (Result)
    {
        ok(Output.DataFormat.FormatSize == sizeof(Output), "Intersection size %lu\n", Output.DataFormat.FormatSize);
        ok(IsEqualGUID(&Output.DataFormat.SubFormat, &UvcSubtypeYuy2), "Intersection subtype %s\n", wine_dbgstr_guid(&Output.DataFormat.SubFormat));
        ok(Output.VideoInfoHeader.bmiHeader.biWidth == 640 && Output.VideoInfoHeader.bmiHeader.biHeight == 480, "Intersection size %ldx%ld\n",
           Output.VideoInfoHeader.bmiHeader.biWidth, Output.VideoInfoHeader.bmiHeader.biHeight);
        ok(Output.VideoInfoHeader.AvgTimePerFrame == 333333, "Intersection interval %ld\n", (LONG)Output.VideoInfoHeader.AvgTimePerFrame);
    }

    Returned = 0;
    Result = UvcIoctl(Filter, IOCTL_KS_PROPERTY, &Request, sizeof(Request), NULL, 0, &Returned);
    ok(!Result && GetLastError() == ERROR_MORE_DATA && Returned == sizeof(Output), "Intersection size query: result %d err %lu ret %lu\n", Result, GetLastError(), Returned);
}

START_TEST(KsStream)
{
    WCHAR Path[512];
    HANDLE Filter, Pin = NULL, Second = NULL;
    KS_DATAFORMAT_VIDEOINFOHEADER Format;
    DWORD Error;

    if (!UvcFindCamera(&UvcCategoryCapture, Path, ARRAYSIZE(Path)))
    {
        skip("No usb-uvc test camera\n");
        return;
    }

    Filter = UvcOpenFilter(Path);
    ok(Filter != INVALID_HANDLE_VALUE, "Opening the filter failed %lu\n", GetLastError());
    if (Filter == INVALID_HANDLE_VALUE)
        return;

    StreamFormat(Filter, &UvcSubtypeYuy2, UVC_FOURCC('Y', 'U', 'Y', '2'), 320, 240, 666666);
    StreamFormat(Filter, &UvcSubtypeYuy2, UVC_FOURCC('Y', 'U', 'Y', '2'), 640, 480, 333333);
    StreamFormat(Filter, &UvcSubtypeYuy2, UVC_FOURCC('Y', 'U', 'Y', '2'), 640, 480, 666666);
    StreamFormat(Filter, &UvcSubtypeYuy2, UVC_FOURCC('Y', 'U', 'Y', '2'), 1280, 720, 1000000);
    StreamFormat(Filter, &UvcSubtypeMjpg, UVC_FOURCC('M', 'J', 'P', 'G'), 640, 480, 333333);

    StreamEdges(Filter);
    RestartCycles(Filter);
    CheckIntersection(Filter);

    UvcBuildFormat(&Format, &UvcSubtypeYuy2, UVC_FOURCC('Y', 'U', 'Y', '2'), 641, 481, 333333);
    Error = UvcCreatePin(Filter, 0, &Format, &Pin);
    ok(Error == ERROR_NO_MATCH, "Pin with unknown size: err %lu\n", Error);
    if (!Error)
        CloseHandle(Pin);

    UvcBuildFormat(&Format, &UvcSubtypeYuy2, UVC_FOURCC('Y', 'U', 'Y', '2'), 640, 480, 333333);
    Error = UvcCreatePin(Filter, 7, &Format, &Pin);
    ok(Error == ERROR_INVALID_PARAMETER, "Pin with unknown id: err %lu\n", Error);
    if (!Error)
        CloseHandle(Pin);

    Error = UvcCreatePin(Filter, 0, &Format, &Pin);
    ok(Error == ERROR_SUCCESS, "First pin failed %lu\n", Error);
    if (!Error)
    {
        Error = UvcCreatePin(Filter, 0, &Format, &Second);
        ok(Error == ERROR_GEN_FAILURE, "Second pin instance: err %lu\n", Error);
        if (!Error)
            CloseHandle(Second);
        CloseHandle(Pin);
    }

    CloseHandle(Filter);
}
