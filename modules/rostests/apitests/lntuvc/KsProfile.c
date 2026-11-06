/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the layouts of real cameras: discrete intervals, repeated sizes, vendor formats
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lntuvc_test.h"

#define PROFILE_BUFFER_SIZE (16 * 1024 * 1024)
#define PROFILE_FRAMES 3

typedef struct _UVC_FRAME_REQUEST
{
    KSSTREAM_HEADER Header;
    KS_FRAME_INFO FrameInfo;
} UVC_FRAME_REQUEST;

static void
StreamRange(HANDLE Filter, const KS_DATARANGE_VIDEO *Range, ULONG Index, PBYTE Buffer)
{
    KS_DATAFORMAT_VIDEOINFOHEADER Format;
    UVC_FRAME_REQUEST Request;
    HANDLE Pin = NULL;
    DWORD Error, Returned, Start;
    DWORD Size = Range->VideoInfoHeader.bmiHeader.biSizeImage;
    LONG Width = Range->VideoInfoHeader.bmiHeader.biWidth;
    LONG Height = Range->VideoInfoHeader.bmiHeader.biHeight;
    BOOL Yuy2 = IsEqualGUID(&Range->DataRange.SubFormat, &UvcSubtypeYuy2);
    ULONG Frame, Good = 0, Sequence = 0;

    ZeroMemory(&Format, sizeof(Format));
    Format.DataFormat = Range->DataRange;
    Format.DataFormat.FormatSize = sizeof(Format);
    Format.VideoInfoHeader = Range->VideoInfoHeader;

    Start = GetTickCount();
    Error = UvcCreatePin(Filter, 0, &Format, &Pin);
    ok(Error == ERROR_SUCCESS, "Range %lu %ldx%ld: KsCreatePin failed %lu\n", Index, Width, Height, Error);
    if (Error)
        return;

    ok(UvcSetPinState(Pin, KSSTATE_ACQUIRE) == ERROR_SUCCESS, "Range %lu: ACQUIRE failed %lu\n", Index, GetLastError());
    ok(UvcSetPinState(Pin, KSSTATE_PAUSE) == ERROR_SUCCESS, "Range %lu: PAUSE failed %lu\n", Index, GetLastError());
    ok(UvcSetPinState(Pin, KSSTATE_RUN) == ERROR_SUCCESS, "Range %lu: RUN failed %lu\n", Index, GetLastError());

    for (Frame = 0; Frame < PROFILE_FRAMES; Frame++)
    {
        ZeroMemory(&Request, sizeof(Request));
        Request.Header.Size = sizeof(Request);
        Request.Header.FrameExtent = Size;
        Request.Header.Data = Buffer;
        Request.FrameInfo.ExtendedHeaderSize = sizeof(KS_FRAME_INFO);
        if (!UvcIoctl(Pin, IOCTL_KS_READ_STREAM, NULL, 0, &Request, sizeof(Request), &Returned))
        {
            ok(0, "Range %lu %ldx%ld frame %lu failed %lu\n", Index, Width, Height, Frame, GetLastError());
            break;
        }
        if (Yuy2)
        {
            ULONG Wrong = UvcCheckYuy2Frame(Buffer, Width, Height, &Sequence);

            ok(Request.Header.DataUsed == (DWORD)(Width * Height * 2), "Range %lu frame %lu DataUsed %lu\n", Index, Frame, Request.Header.DataUsed);
            ok(Wrong == 0, "Range %lu %ldx%ld frame %lu has %lu wrong pixels\n", Index, Width, Height, Frame, Wrong);
            if (Wrong == 0)
                Good++;
        }
        else
        {
            ok(Buffer[0] == 0xFF && Buffer[1] == 0xD8, "Range %lu frame %lu has no SOI: used %lu bytes %02x %02x %02x %02x %02x %02x\n", Index, Frame,
               Request.Header.DataUsed, Buffer[0], Buffer[1], Buffer[2], Buffer[3], Buffer[4], Buffer[5]);
            Good++;
        }
    }
    trace("MEASURE stream %lu %s %ldx%ld: %lu good frames in %lu ms\n", Index, Yuy2 ? "yuy2" : "other", Width, Height, Good, GetTickCount() - Start);
    ok(Good == PROFILE_FRAMES, "Range %lu %ldx%ld: only %lu good frames\n", Index, Width, Height, Good);

    UvcSetPinState(Pin, KSSTATE_STOP);
    CloseHandle(Pin);
}

static void
CheckProfile(const WCHAR *Id)
{
    static BYTE Ranges[256 * 1024];
    KSMULTIPLE_ITEM *Multiple = (PVOID)Ranges;
    WCHAR Path[512];
    HANDLE Filter;
    PBYTE Buffer, Cursor;
    DWORD Returned = 0, Error;
    ULONG Pins = 0, Index;

    if (!UvcFindCameraId(&UvcCategoryCapture, Id, Path, ARRAYSIZE(Path)))
        return;

    trace("MEASURE camera %ls\n", Id);
    Filter = UvcOpenFilter(Path);
    ok(Filter != INVALID_HANDLE_VALUE, "Opening the filter failed %lu\n", GetLastError());
    if (Filter == INVALID_HANDLE_VALUE)
        return;

    Error = UvcGetPinProperty(Filter, 0, KSPROPERTY_PIN_CTYPES, &Pins, sizeof(Pins), &Returned);
    trace("MEASURE pin types err %lu count %lu\n", Error, Pins);

    Error = UvcGetPinProperty(Filter, 0, KSPROPERTY_PIN_DATARANGES, Ranges, sizeof(Ranges), &Returned);
    ok(Error == ERROR_SUCCESS, "PIN_DATARANGES failed %lu\n", Error);
    if (Error)
    {
        CloseHandle(Filter);
        return;
    }
    trace("MEASURE ranges size %lu count %lu returned %lu\n", Multiple->Size, Multiple->Count, Returned);

    Cursor = (PBYTE)(Multiple + 1);
    for (Index = 0; Index < Multiple->Count; Index++)
    {
        KS_DATARANGE_VIDEO *Range = (PVOID)Cursor;
        const KS_VIDEO_STREAM_CONFIG_CAPS *Caps = &Range->ConfigCaps;
        const KS_BITMAPINFOHEADER *Bmi = &Range->VideoInfoHeader.bmiHeader;

        trace("MEASURE range %lu size %lu sub %08lx spec %08lx sample %lu fixed %d temporal %d flags %lx memflags %lx\n",
              Index, Range->DataRange.FormatSize, Range->DataRange.SubFormat.Data1, Range->DataRange.Specifier.Data1,
              Range->DataRange.SampleSize, Range->bFixedSizeSamples, Range->bTemporalCompression,
              Range->StreamDescriptionFlags, Range->MemoryAllocationFlags);
        trace("MEASURE  caps video %ldx%ld..%ldx%ld gran %ld %ld interval %ld..%ld bits %ld..%ld\n",
              (LONG)Caps->MinOutputSize.cx, (LONG)Caps->MinOutputSize.cy, (LONG)Caps->MaxOutputSize.cx, (LONG)Caps->MaxOutputSize.cy,
              (LONG)Caps->OutputGranularityX, (LONG)Caps->OutputGranularityY,
              (LONG)Caps->MinFrameInterval, (LONG)Caps->MaxFrameInterval, Caps->MinBitsPerSecond, Caps->MaxBitsPerSecond);
        trace("MEASURE  vih %ldx%ld bits %u comp %08lx image %lu avg %ld rate %lu\n",
              Bmi->biWidth, Bmi->biHeight, Bmi->biBitCount, Bmi->biCompression, Bmi->biSizeImage,
              (LONG)Range->VideoInfoHeader.AvgTimePerFrame, Range->VideoInfoHeader.dwBitRate);

        Cursor += (Range->DataRange.FormatSize + 7) & ~7u;
    }

    Buffer = VirtualAlloc(NULL, PROFILE_BUFFER_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    Cursor = (PBYTE)(Multiple + 1);
    for (Index = 0; Index < Multiple->Count; Index++)
    {
        KS_DATARANGE_VIDEO *Range = (PVOID)Cursor;

        if (IsEqualGUID(&Range->DataRange.Specifier, &UvcSpecifierVideoInfo))
            StreamRange(Filter, Range, Index, Buffer);
        Cursor += (Range->DataRange.FormatSize + 7) & ~7u;
    }
    VirtualFree(Buffer, 0, MEM_RELEASE);
    CloseHandle(Filter);
}

START_TEST(KsProfile)
{
    WCHAR Path[512];

    if (!UvcFindCameraId(&UvcCategoryCapture, L"vid_46f4&pid_009b", Path, ARRAYSIZE(Path)) &&
        !UvcFindCameraId(&UvcCategoryCapture, L"vid_46f4&pid_009c", Path, ARRAYSIZE(Path)))
    {
        skip("No usb-uvc camera with a profile\n");
        return;
    }

    CheckProfile(L"vid_46f4&pid_009b");
    CheckProfile(L"vid_46f4&pid_009c");
}
