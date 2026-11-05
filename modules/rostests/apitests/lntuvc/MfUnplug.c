/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for Media Foundation capture from a camera that goes away
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include "lntuvc_test.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#define UNPLUG_MAX_FRAMES 300
#define FIRST_VIDEO_STREAM ((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM)

typedef struct _READ_RESULT
{
    IMFSourceReader *Reader;
    ULONG Limit;
    ULONG Frames;
    HRESULT Status;
    DWORD Flags;
} READ_RESULT;

static IMFMediaSource *
OpenUnplugCamera(void)
{
    IMFAttributes *Attributes = NULL;
    IMFActivate **Devices = NULL;
    IMFMediaSource *Source = NULL;
    UINT32 Count = 0, Index;

    if (FAILED(MFCreateAttributes(&Attributes, 1)))
        return NULL;
    IMFAttributes_SetGUID(Attributes, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (SUCCEEDED(MFEnumDeviceSources(Attributes, &Devices, &Count)))
    {
        for (Index = 0; Index < Count; Index++)
        {
            WCHAR *Link = NULL;
            UINT32 Length = 0;

            IMFActivate_GetAllocatedString(Devices[Index], &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &Link, &Length);
            if (!Source && Link && wcsstr(_wcslwr(Link), L"vid_46f4&pid_009a"))
                IMFActivate_ActivateObject(Devices[Index], &IID_IMFMediaSource, (void **)&Source);
            CoTaskMemFree(Link);
            IMFActivate_Release(Devices[Index]);
        }
        CoTaskMemFree(Devices);
    }
    IMFAttributes_Release(Attributes);
    return Source;
}

static DWORD WINAPI
ReadThread(PVOID Parameter)
{
    READ_RESULT *Result = Parameter;

    while (Result->Frames < Result->Limit)
    {
        IMFSample *Sample = NULL;
        DWORD Stream = 0;
        LONGLONG Time = 0;

        Result->Status = IMFSourceReader_ReadSample(Result->Reader, FIRST_VIDEO_STREAM, 0, &Stream, &Result->Flags, &Time, &Sample);
        if (Sample)
        {
            if (++Result->Frames % 15 == 0)
                trace("Read %lu samples\n", Result->Frames);
            IMFSample_Release(Sample);
        }
        if (FAILED(Result->Status) || (Result->Flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM)))
            break;
    }
    return 0;
}

static BOOL
ReadUntilFailure(IMFSourceReader *Reader, ULONG Limit, READ_RESULT *Result)
{
    HANDLE Thread;
    BOOL Finished;

    ZeroMemory(Result, sizeof(*Result));
    Result->Reader = Reader;
    Result->Limit = Limit;
    Thread = CreateThread(NULL, 0, ReadThread, Result, 0, NULL);
    Finished = WaitForSingleObject(Thread, 25000) == WAIT_OBJECT_0;
    CloseHandle(Thread);
    return Finished;
}

START_TEST(MfUnplug)
{
    IMFMediaSource *Source;
    IMFSourceReader *Reader = NULL;
    READ_RESULT Result;
    DWORD Start, Characteristics = 0;
    HRESULT hr;
    ULONG Round;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION, MFSTARTUP_FULL);

    Start = GetTickCount();
    do
    {
        Source = OpenUnplugCamera();
        if (!Source)
            Sleep(500);
    } while (!Source && GetTickCount() - Start < 3000);

    if (!Source)
    {
        skip("No usb-uvc camera with the unplug-frames property\n");
        goto done;
    }

    hr = MFCreateSourceReaderFromMediaSource(Source, NULL, &Reader);
    ok(hr == S_OK, "MFCreateSourceReaderFromMediaSource returned %#lx\n", hr);
    if (FAILED(hr))
        goto done;

    Start = GetTickCount();
    ok(ReadUntilFailure(Reader, UNPLUG_MAX_FRAMES, &Result), "ReadSample never returned after the unplug\n");
    ok(Result.Frames > 0 && Result.Frames < UNPLUG_MAX_FRAMES, "Frames before the read failed %lu\n", Result.Frames);
    ok(Result.Status == MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED, "Read after the unplug returned %#lx\n", Result.Status);
    ok(Result.Flags == MF_SOURCE_READERF_ERROR, "Read after the unplug flags %#lx\n", Result.Flags);

    ok(ReadUntilFailure(Reader, 1, &Result), "Second ReadSample never returned\n");
    ok(Result.Frames == 0, "Second read got %lu frames\n", Result.Frames);
    ok(Result.Status == MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED, "Second read returned %#lx\n", Result.Status);
    ok(Result.Flags == MF_SOURCE_READERF_ERROR, "Second read flags %#lx\n", Result.Flags);

    hr = IMFMediaSource_GetCharacteristics(Source, &Characteristics);
    ok(hr == S_OK && Characteristics == (MFMEDIASOURCE_IS_LIVE | MFMEDIASOURCE_CAN_PAUSE), "Characteristics after the unplug hr %#lx value %#lx\n", hr, Characteristics);

    IMFSourceReader_Release(Reader);
    Reader = NULL;
    IMFMediaSource_Shutdown(Source);
    IMFMediaSource_Release(Source);
    Source = NULL;

    Start = GetTickCount();
    for (Round = 0; !Source && GetTickCount() - Start < 20000; Round++)
    {
        Source = OpenUnplugCamera();
        if (!Source)
        {
            Sleep(500);
            if (Round % 4 == 3)
                trace("Waiting for the camera, %lu ms\n", GetTickCount() - Start);
        }
    }
    ok(Source != NULL, "The camera did not come back\n");
    if (!Source)
        goto done;

    hr = MFCreateSourceReaderFromMediaSource(Source, NULL, &Reader);
    ok(hr == S_OK, "MFCreateSourceReaderFromMediaSource after the replug returned %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        ok(ReadUntilFailure(Reader, 4, &Result), "ReadSample after the replug never returned\n");
        ok(Result.Frames == 4, "Frames after the replug %lu, hr %#lx flags %#lx\n", Result.Frames, Result.Status, Result.Flags);
    }

done:
    if (Reader)
        IMFSourceReader_Release(Reader);
    if (Source)
    {
        IMFMediaSource_Shutdown(Source);
        IMFMediaSource_Release(Source);
    }
    MFShutdown();
    CoUninitialize();
}
