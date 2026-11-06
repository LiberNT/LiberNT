/*
 * PROJECT:     LiberNT Camera
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Live preview of a camera through Media Foundation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <initguid.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wincodec.h>
#include <wchar.h>

#define IDS_TITLE       100
#define IDS_SEARCHING   101
#define IDS_NO_CAMERA   102
#define IDS_STOPPED     103
#define IDS_FORMAT      104

#define ID_FORMAT_FIRST 1000
#define ID_FORMAT_LAST  1999
#define WM_APP_FORMATS  (WM_APP + 1)
#define WM_APP_STATUS   (WM_APP + 2)
#define CAMERA_RETRY_MS 1000

#define FIRST_VIDEO_STREAM ((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM)

typedef struct _CAMERA_FORMAT
{
    GUID Subtype;
    UINT32 Width;
    UINT32 Height;
    UINT32 RateNumerator;
    UINT32 RateDenominator;
} CAMERA_FORMAT;

static HWND MainWindow;
static HINSTANCE Instance;
static CRITICAL_SECTION FrameLock;
static BYTE *FramePixels;
static UINT32 FrameWidth;
static UINT32 FrameHeight;
static UINT StatusId = IDS_SEARCHING;
static volatile LONG StopRequested;
static volatile LONG RequestedFormat = -1;
static HANDLE CaptureThreadHandle;
static CAMERA_FORMAT Formats[256];
static LONG FormatCount;
static LONG CurrentFormat = -1;

static BYTE
ClampByte(int Value)
{
    return Value < 0 ? 0 : (Value > 255 ? 255 : (BYTE)Value);
}

static void
PutPixel(BYTE *Out, int Y, int U, int V)
{
    int C = (Y - 16) * 298;
    int D = U - 128;
    int E = V - 128;

    Out[0] = ClampByte((C + 516 * D + 128) >> 8);
    Out[1] = ClampByte((C - 100 * D - 208 * E + 128) >> 8);
    Out[2] = ClampByte((C + 409 * E + 128) >> 8);
    Out[3] = 255;
}

static void
ConvertYuy2(const BYTE *Source, UINT32 Width, UINT32 Height, BYTE *Out)
{
    UINT32 X, Y;

    for (Y = 0; Y < Height; Y++)
    {
        for (X = 0; X + 1 < Width; X += 2, Source += 4, Out += 8)
        {
            PutPixel(Out, Source[0], Source[1], Source[3]);
            PutPixel(Out + 4, Source[2], Source[1], Source[3]);
        }
    }
}

static void
ConvertNv12(const BYTE *Source, UINT32 Width, UINT32 Height, BYTE *Out)
{
    const BYTE *Chroma = Source + Width * Height;
    UINT32 X, Y;

    for (Y = 0; Y < Height; Y++)
    {
        const BYTE *Row = Chroma + (Y / 2) * Width;

        for (X = 0; X < Width; X++, Out += 4)
            PutPixel(Out, Source[Y * Width + X], Row[X & ~1u], Row[(X & ~1u) + 1]);
    }
}

static BOOL
DecodeJpeg(IWICImagingFactory *Factory, const BYTE *Source, DWORD Length, UINT32 Width, UINT32 Height, BYTE *Out)
{
    IWICStream *Stream = NULL;
    IWICBitmapDecoder *Decoder = NULL;
    IWICBitmapFrameDecode *Frame = NULL;
    IWICBitmapSource *Converted = NULL;
    BOOL Result = FALSE;

    if (!Factory ||
        FAILED(IWICImagingFactory_CreateStream(Factory, &Stream)) ||
        FAILED(IWICStream_InitializeFromMemory(Stream, (BYTE *)Source, Length)) ||
        FAILED(IWICImagingFactory_CreateDecoderFromStream(Factory, (IStream *)Stream, NULL, WICDecodeMetadataCacheOnDemand, &Decoder)) ||
        FAILED(IWICBitmapDecoder_GetFrame(Decoder, 0, &Frame)) ||
        FAILED(WICConvertBitmapSource(&GUID_WICPixelFormat32bppBGRA, (IWICBitmapSource *)Frame, &Converted)))
    {
        goto done;
    }

    Result = SUCCEEDED(IWICBitmapSource_CopyPixels(Converted, NULL, Width * 4, Width * Height * 4, Out));

done:
    if (Converted)
        IWICBitmapSource_Release(Converted);
    if (Frame)
        IWICBitmapFrameDecode_Release(Frame);
    if (Decoder)
        IWICBitmapDecoder_Release(Decoder);
    if (Stream)
        IWICStream_Release(Stream);
    return Result;
}

static void
SetStatus(UINT Id)
{
    StatusId = Id;
    PostMessageW(MainWindow, WM_APP_STATUS, 0, 0);
}

static LONG
ChooseDefaultFormat(void)
{
    LONG Best = -1, Index;
    UINT64 BestScore = 0;

    for (Index = 0; Index < FormatCount; Index++)
    {
        const CAMERA_FORMAT *Format = &Formats[Index];
        UINT64 Area = (UINT64)Format->Width * Format->Height;
        UINT32 Rate = Format->RateDenominator ? Format->RateNumerator / Format->RateDenominator : 0;
        UINT64 Score;

        if (Format->Width > 1920 || Format->Height > 1080 || Rate < 15)
            continue;

        Score = Area * 2 + (IsEqualGUID(&Format->Subtype, &MFVideoFormat_YUY2) ? 1 : 0);
        if (Score > BestScore)
        {
            BestScore = Score;
            Best = Index;
        }
    }

    return Best >= 0 ? Best : (FormatCount ? 0 : -1);
}

static BOOL
IsSupportedSubtype(const GUID *Subtype)
{
    return IsEqualGUID(Subtype, &MFVideoFormat_YUY2) ||
           IsEqualGUID(Subtype, &MFVideoFormat_NV12) ||
           IsEqualGUID(Subtype, &MFVideoFormat_MJPG);
}

static void
LoadFormats(IMFSourceReader *Reader)
{
    DWORD Index;

    FormatCount = 0;
    for (Index = 0; FormatCount < (LONG)ARRAYSIZE(Formats); Index++)
    {
        IMFMediaType *Type;
        CAMERA_FORMAT Format = { 0 };
        UINT64 Value = 0;

        if (FAILED(IMFSourceReader_GetNativeMediaType(Reader, FIRST_VIDEO_STREAM, Index, &Type)))
            break;

        IMFMediaType_GetGUID(Type, &MF_MT_SUBTYPE, &Format.Subtype);
        if (SUCCEEDED(IMFMediaType_GetUINT64(Type, &MF_MT_FRAME_SIZE, &Value)))
        {
            Format.Width = (UINT32)(Value >> 32);
            Format.Height = (UINT32)Value;
        }
        Value = 0;
        if (SUCCEEDED(IMFMediaType_GetUINT64(Type, &MF_MT_FRAME_RATE, &Value)))
        {
            Format.RateNumerator = (UINT32)(Value >> 32);
            Format.RateDenominator = (UINT32)Value;
        }
        IMFMediaType_Release(Type);

        if (IsSupportedSubtype(&Format.Subtype) && Format.Width && Format.Height)
            Formats[FormatCount++] = Format;
    }
}

static HRESULT
SelectFormat(IMFSourceReader *Reader, LONG Wanted)
{
    const CAMERA_FORMAT *Target = &Formats[Wanted];
    DWORD Index;

    for (Index = 0; ; Index++)
    {
        IMFMediaType *Type;
        GUID Subtype = { 0 };
        UINT64 Size = 0, Rate = 0;
        HRESULT hr;

        if (FAILED(IMFSourceReader_GetNativeMediaType(Reader, FIRST_VIDEO_STREAM, Index, &Type)))
            return E_FAIL;

        IMFMediaType_GetGUID(Type, &MF_MT_SUBTYPE, &Subtype);
        IMFMediaType_GetUINT64(Type, &MF_MT_FRAME_SIZE, &Size);
        IMFMediaType_GetUINT64(Type, &MF_MT_FRAME_RATE, &Rate);
        if (IsEqualGUID(&Subtype, &Target->Subtype) &&
            (UINT32)(Size >> 32) == Target->Width && (UINT32)Size == Target->Height &&
            (UINT32)(Rate >> 32) == Target->RateNumerator && (UINT32)Rate == Target->RateDenominator)
        {
            hr = IMFSourceReader_SetCurrentMediaType(Reader, FIRST_VIDEO_STREAM, NULL, Type);
            IMFMediaType_Release(Type);
            return hr;
        }
        IMFMediaType_Release(Type);
    }
}

static void
PublishFrame(const CAMERA_FORMAT *Format, const BYTE *Data, DWORD Length, IWICImagingFactory *Factory, BYTE *Work)
{
    BOOL Converted = FALSE;

    if (IsEqualGUID(&Format->Subtype, &MFVideoFormat_YUY2))
    {
        if (Length >= Format->Width * Format->Height * 2)
        {
            ConvertYuy2(Data, Format->Width, Format->Height, Work);
            Converted = TRUE;
        }
    }
    else if (IsEqualGUID(&Format->Subtype, &MFVideoFormat_NV12))
    {
        if (Length >= Format->Width * Format->Height * 3 / 2)
        {
            ConvertNv12(Data, Format->Width, Format->Height, Work);
            Converted = TRUE;
        }
    }
    else
    {
        Converted = DecodeJpeg(Factory, Data, Length, Format->Width, Format->Height, Work);
    }

    if (!Converted)
        return;

    EnterCriticalSection(&FrameLock);
    if (FramePixels && FrameWidth == Format->Width && FrameHeight == Format->Height)
        memcpy(FramePixels, Work, Format->Width * Format->Height * 4);
    LeaveCriticalSection(&FrameLock);
    InvalidateRect(MainWindow, NULL, FALSE);
}

static BOOL
PrepareFrameBuffers(const CAMERA_FORMAT *Format, BYTE **Work)
{
    SIZE_T Size = (SIZE_T)Format->Width * Format->Height * 4;
    BYTE *Pixels = malloc(Size);

    free(*Work);
    *Work = malloc(Size);
    if (!Pixels || !*Work)
    {
        free(Pixels);
        return FALSE;
    }
    memset(Pixels, 0, Size);

    EnterCriticalSection(&FrameLock);
    free(FramePixels);
    FramePixels = Pixels;
    FrameWidth = Format->Width;
    FrameHeight = Format->Height;
    LeaveCriticalSection(&FrameLock);
    return TRUE;
}

static BOOL
StreamFrames(IMFSourceReader *Reader, const CAMERA_FORMAT *Format, IWICImagingFactory *Factory, BYTE *Work)
{
    while (!StopRequested && RequestedFormat < 0)
    {
        IMFSample *Sample = NULL;
        IMFMediaBuffer *Buffer = NULL;
        DWORD Stream = 0, Flags = 0, Length = 0;
        LONGLONG Time = 0;
        BYTE *Data = NULL;

        if (FAILED(IMFSourceReader_ReadSample(Reader, FIRST_VIDEO_STREAM, 0, &Stream, &Flags, &Time, &Sample)))
            return FALSE;
        if (Flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM))
            return FALSE;
        if (!Sample)
            continue;

        if (SUCCEEDED(IMFSample_ConvertToContiguousBuffer(Sample, &Buffer)))
        {
            if (SUCCEEDED(IMFMediaBuffer_Lock(Buffer, &Data, NULL, &Length)))
            {
                PublishFrame(Format, Data, Length, Factory, Work);
                IMFMediaBuffer_Unlock(Buffer);
            }
            IMFMediaBuffer_Release(Buffer);
        }
        IMFSample_Release(Sample);
    }

    return TRUE;
}

static void
RunCamera(IMFMediaSource *Source, IWICImagingFactory *Factory, BYTE **Work)
{
    FormatCount = 0;
    CurrentFormat = -1;

    while (!StopRequested)
    {
        IMFSourceReader *Reader = NULL;
        BOOL Running;
        LONG Wanted;

        if (FAILED(MFCreateSourceReaderFromMediaSource(Source, NULL, &Reader)))
        {
            SetStatus(IDS_STOPPED);
            break;
        }

        if (!FormatCount)
        {
            LoadFormats(Reader);
            PostMessageW(MainWindow, WM_APP_FORMATS, 0, 0);
        }

        Wanted = InterlockedExchange(&RequestedFormat, -1);
        if (Wanted < 0 || Wanted >= FormatCount)
            Wanted = CurrentFormat >= 0 ? CurrentFormat : ChooseDefaultFormat();

        if (Wanted < 0 || FAILED(SelectFormat(Reader, Wanted)) || !PrepareFrameBuffers(&Formats[Wanted], Work))
        {
            IMFSourceReader_Release(Reader);
            SetStatus(IDS_STOPPED);
            break;
        }

        CurrentFormat = Wanted;
        Running = StreamFrames(Reader, &Formats[Wanted], Factory, *Work);
        IMFSourceReader_Release(Reader);

        if (!Running)
        {
            SetStatus(IDS_STOPPED);
            break;
        }
    }
}

static DWORD WINAPI
CaptureThread(PVOID Parameter)
{
    IWICImagingFactory *Factory = NULL;
    IMFAttributes *Attributes = NULL;
    BYTE *Work = NULL;

    UNREFERENCED_PARAMETER(Parameter);

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&Factory);

    if (FAILED(MFCreateAttributes(&Attributes, 1)) ||
        FAILED(IMFAttributes_SetGUID(Attributes, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID)))
    {
        SetStatus(IDS_NO_CAMERA);
        goto done;
    }

    while (!StopRequested)
    {
        IMFActivate **Devices = NULL;
        IMFMediaSource *Source = NULL;
        UINT32 Count = 0, Index;

        if (SUCCEEDED(MFEnumDeviceSources(Attributes, &Devices, &Count)) &&
            Count != 0 &&
            SUCCEEDED(IMFActivate_ActivateObject(Devices[0], &IID_IMFMediaSource, (void **)&Source)))
        {
            RunCamera(Source, Factory, &Work);
            IMFMediaSource_Shutdown(Source);
            IMFMediaSource_Release(Source);
        }
        else
        {
            SetStatus(IDS_NO_CAMERA);
        }

        for (Index = 0; Index < Count; Index++)
            IMFActivate_Release(Devices[Index]);
        CoTaskMemFree(Devices);

        if (!StopRequested)
            Sleep(CAMERA_RETRY_MS);
    }

done:
    if (Attributes)
        IMFAttributes_Release(Attributes);
    if (Factory)
        IWICImagingFactory_Release(Factory);
    free(Work);
    CoUninitialize();
    return 0;
}

static const WCHAR *
SubtypeName(const GUID *Subtype)
{
    if (IsEqualGUID(Subtype, &MFVideoFormat_YUY2))
        return L"YUY2";
    if (IsEqualGUID(Subtype, &MFVideoFormat_NV12))
        return L"NV12";
    return L"MJPEG";
}

static void
BuildFormatMenu(HWND Window)
{
    HMENU Bar = CreateMenu();
    HMENU Popup = CreatePopupMenu();
    WCHAR Title[64];
    LONG Index;

    for (Index = 0; Index < FormatCount && Index <= ID_FORMAT_LAST - ID_FORMAT_FIRST; Index++)
    {
        const CAMERA_FORMAT *Format = &Formats[Index];
        WCHAR Text[96];

        _snwprintf(Text, ARRAYSIZE(Text), L"%ls %ux%u, %u fps", SubtypeName(&Format->Subtype), Format->Width, Format->Height,
                   Format->RateDenominator ? Format->RateNumerator / Format->RateDenominator : 0);
        Text[ARRAYSIZE(Text) - 1] = 0;
        AppendMenuW(Popup, MF_STRING, ID_FORMAT_FIRST + Index, Text);
    }

    LoadStringW(Instance, IDS_FORMAT, Title, ARRAYSIZE(Title));
    AppendMenuW(Bar, MF_POPUP, (UINT_PTR)Popup, Title);
    SetMenu(Window, Bar);
}

static void
PaintWindow(HWND Window)
{
    PAINTSTRUCT Paint;
    RECT Client;
    HDC Dc = BeginPaint(Window, &Paint);

    GetClientRect(Window, &Client);

    EnterCriticalSection(&FrameLock);
    if (FramePixels && StatusId == 0)
    {
        BITMAPINFO Info = { 0 };
        LONG AreaWidth = Client.right, AreaHeight = Client.bottom;
        LONG DrawWidth = AreaWidth, DrawHeight = (LONG)((LONGLONG)AreaWidth * FrameHeight / FrameWidth);
        LONG Left, Top;
        RECT Band;

        if (DrawHeight > AreaHeight)
        {
            DrawHeight = AreaHeight;
            DrawWidth = (LONG)((LONGLONG)AreaHeight * FrameWidth / FrameHeight);
        }
        Left = (AreaWidth - DrawWidth) / 2;
        Top = (AreaHeight - DrawHeight) / 2;

        Info.bmiHeader.biSize = sizeof(Info.bmiHeader);
        Info.bmiHeader.biWidth = FrameWidth;
        Info.bmiHeader.biHeight = -(LONG)FrameHeight;
        Info.bmiHeader.biPlanes = 1;
        Info.bmiHeader.biBitCount = 32;
        Info.bmiHeader.biCompression = BI_RGB;

        SetStretchBltMode(Dc, COLORONCOLOR);
        StretchDIBits(Dc, Left, Top, DrawWidth, DrawHeight, 0, 0, FrameWidth, FrameHeight, FramePixels, &Info, DIB_RGB_COLORS, SRCCOPY);

        SetRect(&Band, 0, 0, AreaWidth, Top);
        FillRect(Dc, &Band, GetStockObject(BLACK_BRUSH));
        SetRect(&Band, 0, Top + DrawHeight, AreaWidth, AreaHeight);
        FillRect(Dc, &Band, GetStockObject(BLACK_BRUSH));
        SetRect(&Band, 0, Top, Left, Top + DrawHeight);
        FillRect(Dc, &Band, GetStockObject(BLACK_BRUSH));
        SetRect(&Band, Left + DrawWidth, Top, AreaWidth, Top + DrawHeight);
        FillRect(Dc, &Band, GetStockObject(BLACK_BRUSH));
    }
    else
    {
        WCHAR Text[160] = L"";

        FillRect(Dc, &Client, GetStockObject(BLACK_BRUSH));
        LoadStringW(Instance, StatusId ? StatusId : IDS_SEARCHING, Text, ARRAYSIZE(Text));
        SetBkMode(Dc, TRANSPARENT);
        SetTextColor(Dc, RGB(255, 255, 255));
        DrawTextW(Dc, Text, -1, &Client, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    LeaveCriticalSection(&FrameLock);

    EndPaint(Window, &Paint);
}

static LRESULT CALLBACK
WindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    switch (Message)
    {
        case WM_APP_FORMATS:
            BuildFormatMenu(Window);
            StatusId = 0;
            return 0;

        case WM_APP_STATUS:
            InvalidateRect(Window, NULL, TRUE);
            return 0;

        case WM_COMMAND:
            if (LOWORD(WParam) >= ID_FORMAT_FIRST && LOWORD(WParam) <= ID_FORMAT_LAST)
            {
                InterlockedExchange(&RequestedFormat, LOWORD(WParam) - ID_FORMAT_FIRST);
                return 0;
            }
            break;

        case WM_ERASEBKGND:
            return 1;

        case WM_SIZE:
            InvalidateRect(Window, NULL, FALSE);
            return 0;

        case WM_PAINT:
            PaintWindow(Window);
            return 0;

        case WM_DESTROY:
            InterlockedExchange(&StopRequested, 1);
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(Window, Message, WParam, LParam);
}

int WINAPI
wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR CommandLine, int ShowCommand)
{
    WNDCLASSEXW Class = { sizeof(Class) };
    WCHAR Title[64];
    MSG Message;

    UNREFERENCED_PARAMETER(hPrevInstance);

    if (!_wcsnicmp(CommandLine, L"/format:", 8))
        RequestedFormat = _wtol(CommandLine + 8);

    Instance = hInstance;
    InitializeCriticalSection(&FrameLock);

    if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED)) || FAILED(MFStartup(MF_VERSION, MFSTARTUP_FULL)))
        return 1;

    Class.lpfnWndProc = WindowProc;
    Class.hInstance = hInstance;
    Class.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    Class.lpszClassName = L"LiberNTCamera";
    RegisterClassExW(&Class);

    LoadStringW(hInstance, IDS_TITLE, Title, ARRAYSIZE(Title));
    MainWindow = CreateWindowExW(0, Class.lpszClassName, Title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 800, 640, NULL, NULL, hInstance, NULL);
    if (!MainWindow)
        return 1;

    ShowWindow(MainWindow, ShowCommand);
    UpdateWindow(MainWindow);

    CaptureThreadHandle = CreateThread(NULL, 0, CaptureThread, NULL, 0, NULL);

    while (GetMessageW(&Message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&Message);
        DispatchMessageW(&Message);
    }

    if (CaptureThreadHandle)
    {
        WaitForSingleObject(CaptureThreadHandle, 5000);
        CloseHandle(CaptureThreadHandle);
    }

    MFShutdown();
    CoUninitialize();
    return 0;
}
