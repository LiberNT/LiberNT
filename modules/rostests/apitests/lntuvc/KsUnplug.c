/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for a USB video class camera that goes away while it streams
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lntuvc_test.h"

#define UNPLUG_CAMERA_ID L"vid_46f4&pid_009a"
#define UNPLUG_MAX_FRAMES 300

typedef struct _UVC_FRAME_REQUEST
{
    KSSTREAM_HEADER Header;
    KS_FRAME_INFO FrameInfo;
} UVC_FRAME_REQUEST;

static BOOL
ReadFrame(HANDLE Pin, UVC_FRAME_REQUEST *Request, PBYTE Buffer, DWORD Size)
{
    DWORD Returned;

    ZeroMemory(Request, sizeof(*Request));
    Request->Header.Size = sizeof(*Request);
    Request->Header.FrameExtent = Size;
    Request->Header.Data = Buffer;
    Request->FrameInfo.ExtendedHeaderSize = sizeof(KS_FRAME_INFO);
    return UvcIoctl(Pin, IOCTL_KS_READ_STREAM, NULL, 0, Request, sizeof(*Request), &Returned);
}

static BOOL
WaitForCamera(DWORD TimeoutMs, WCHAR *Path, DWORD PathChars, DWORD *Elapsed)
{
    DWORD Start = GetTickCount();
    ULONG Round = 0;

    do
    {
        if (UvcFindCameraId(&UvcCategoryCapture, UNPLUG_CAMERA_ID, Path, PathChars))
        {
            *Elapsed = GetTickCount() - Start;
            return TRUE;
        }
        Sleep(200);
        if (++Round % 10 == 0)
            trace("Waiting for the camera, %lu ms\n", GetTickCount() - Start);
    } while (GetTickCount() - Start < TimeoutMs);

    *Elapsed = GetTickCount() - Start;
    return FALSE;
}

static HANDLE
EnableDeviceLost(HANDLE Filter, KSEVENTDATA *Data)
{
    static const GUID Set = { 0x288296ec, 0x9f94, 0x41b4, { 0xa1, 0x53, 0xaa, 0x31, 0xae, 0xec, 0xb3, 0x3f } };
    KSEVENT Event;
    DWORD Returned = 0;
    HANDLE Signal = CreateEventW(NULL, TRUE, FALSE, NULL);
    BOOL Result;

    Event.Set = Set;
    Event.Id = 0;
    Event.Flags = KSEVENT_TYPE_ENABLE;
    ZeroMemory(Data, sizeof(*Data));
    Data->NotificationType = KSEVENTF_EVENT_HANDLE;
    Data->EventHandle.Event = Signal;
    Result = UvcIoctl(Filter, IOCTL_KS_ENABLE_EVENT, &Event, sizeof(Event), Data, sizeof(*Data), &Returned);
    ok(Result, "Enabling the device lost event failed %lu\n", GetLastError());
    return Signal;
}

static HANDLE
RunningPin(HANDLE Filter, DWORD *Size)
{
    KS_DATAFORMAT_VIDEOINFOHEADER Format;
    HANDLE Pin = NULL;
    DWORD Error;

    UvcBuildFormat(&Format, &UvcSubtypeYuy2, UVC_FOURCC('Y', 'U', 'Y', '2'), 320, 240, 666666);
    *Size = 320 * 240 * 2;
    Error = UvcCreatePin(Filter, 0, &Format, &Pin);
    ok(Error == ERROR_SUCCESS, "KsCreatePin failed %lu\n", Error);
    if (Error)
        return NULL;

    ok(UvcSetPinState(Pin, KSSTATE_ACQUIRE) == ERROR_SUCCESS, "ACQUIRE failed %lu\n", GetLastError());
    ok(UvcSetPinState(Pin, KSSTATE_PAUSE) == ERROR_SUCCESS, "PAUSE failed %lu\n", GetLastError());
    ok(UvcSetPinState(Pin, KSSTATE_RUN) == ERROR_SUCCESS, "RUN failed %lu\n", GetLastError());
    return Pin;
}

static void
StreamGoodFrames(const WCHAR *Path, const char *When, UVC_FRAME_REQUEST *Request, PBYTE Buffer)
{
    HANDLE Filter, Pin;
    DWORD Size = 0;
    ULONG Frame, Good = 0, Sequence = 0;

    Filter = UvcOpenFilter(Path);
    ok(Filter != INVALID_HANDLE_VALUE, "Opening the filter %s failed %lu\n", When, GetLastError());
    if (Filter == INVALID_HANDLE_VALUE)
        return;

    Pin = RunningPin(Filter, &Size);
    if (Pin)
    {
        for (Frame = 0; Frame < UVC_TEST_FRAMES; Frame++)
        {
            if (!ReadFrame(Pin, Request, Buffer, Size))
            {
                ok(0, "Frame %lu %s failed %lu\n", Frame, When, GetLastError());
                break;
            }
            if (UvcCheckYuy2Frame(Buffer, 320, 240, &Sequence) == 0)
                Good++;
        }
        ok(Good == UVC_TEST_FRAMES, "Only %lu good frames of %u %s\n", Good, UVC_TEST_FRAMES, When);
        UvcSetPinState(Pin, KSSTATE_STOP);
        CloseHandle(Pin);
    }
    CloseHandle(Filter);
}

START_TEST(KsUnplug)
{
    WCHAR Path[512];
    UVC_FRAME_REQUEST *Request;
    PBYTE Buffer;
    HANDLE Filter, Pin, Lost;
    KSEVENTDATA LostData;
    DWORD Size = 0, Error, Returned, Elapsed, Start;
    ULONG Frames = 0;
    BOOL Found;
    KSSTATE State;
    KSPIN_CINSTANCES Instances;
    ULONG Short;

    if (!UvcFindCameraId(&UvcCategoryCapture, UNPLUG_CAMERA_ID, Path, ARRAYSIZE(Path)))
    {
        skip("No usb-uvc camera with the unplug-frames property\n");
        return;
    }

    Buffer = VirtualAlloc(NULL, 320 * 240 * 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    Request = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Request));

    Filter = UvcOpenFilter(Path);
    ok(Filter != INVALID_HANDLE_VALUE, "Opening the filter failed %lu\n", GetLastError());
    if (Filter == INVALID_HANDLE_VALUE)
        return;
    Lost = EnableDeviceLost(Filter, &LostData);
    Error = WaitForSingleObject(Lost, 0);
    ok(Error == WAIT_TIMEOUT, "Device lost before the unplug %lu\n", Error);
    Pin = RunningPin(Filter, &Size);
    if (!Pin)
        return;

    Error = ERROR_SUCCESS;
    Start = GetTickCount();
    while (Frames < UNPLUG_MAX_FRAMES)
    {
        if (!ReadFrame(Pin, Request, Buffer, Size))
        {
            Error = GetLastError();
            break;
        }
        if (++Frames % 15 == 0)
            trace("Read %lu frames\n", Frames);
    }
    trace("First read failed after %lu frames, %lu ms, error %lu\n", Frames, GetTickCount() - Start, Error);
    ok(Frames > 0 && Frames < UNPLUG_MAX_FRAMES, "Frames before the unplug %lu\n", Frames);
    ok(Error == WAIT_TIMEOUT, "A read pending across the unplug ended with %lu\n", Error);
    Error = WaitForSingleObject(Lost, 3000);
    ok(Error == WAIT_OBJECT_0, "Device lost after the unplug %lu\n", Error);

    Start = GetTickCount();
    Error = ReadFrame(Pin, Request, Buffer, Size) ? ERROR_SUCCESS : GetLastError();
    trace("Second read after the unplug: error %lu after %lu ms\n", Error, GetTickCount() - Start);
    ok(Error == ERROR_INVALID_FUNCTION, "A read after the unplug ended with %lu\n", Error);

    Returned = 0;
    State = KSSTATE_STOP;
    Error = UvcGetProperty(Pin, &UvcPropsetConnection, KSPROPERTY_CONNECTION_STATE, &State, sizeof(State), &Returned);
    ok(Error == ERROR_SUCCESS && State == KSSTATE_RUN, "Pin state after the unplug: err %lu state %d\n", Error, State);

    Short = 0;
    Error = UvcGetPinProperty(Filter, 0, KSPROPERTY_PIN_CINSTANCES, &Short, sizeof(Short), &Returned);
    ok(Error == ERROR_INSUFFICIENT_BUFFER, "Short CINSTANCES after the unplug: err %lu\n", Error);

    ZeroMemory(&Instances, sizeof(Instances));
    Error = UvcGetPinProperty(Filter, 0, KSPROPERTY_PIN_CINSTANCES, &Instances, sizeof(Instances), &Returned);
    ok(Error == ERROR_SUCCESS && Instances.PossibleCount == 1 && Instances.CurrentCount == 1,
       "CINSTANCES after the unplug: err %lu possible %lu current %lu\n", Error, Instances.PossibleCount, Instances.CurrentCount);

    ok(UvcSetPinState(Pin, KSSTATE_PAUSE) == ERROR_SUCCESS, "PAUSE after the unplug failed %lu\n", GetLastError());
    ok(UvcSetPinState(Pin, KSSTATE_ACQUIRE) == ERROR_SUCCESS, "ACQUIRE after the unplug failed %lu\n", GetLastError());
    ok(UvcSetPinState(Pin, KSSTATE_STOP) == ERROR_SUCCESS, "STOP after the unplug failed %lu\n", GetLastError());

    Found = WaitForCamera(8000, Path, ARRAYSIZE(Path), &Elapsed);
    trace("Camera with the old handles open: found %d after %lu ms\n", Found, Elapsed);
    ok(Found, "The camera did not come back with the old handles open, %lu ms\n", Elapsed);
    if (Found)
        StreamGoodFrames(Path, "with the old handles open", Request, Buffer);

    UvcIoctl(Filter, IOCTL_KS_DISABLE_EVENT, &LostData, sizeof(LostData), NULL, 0, &Returned);
    CloseHandle(Lost);
    CloseHandle(Pin);
    CloseHandle(Filter);

    Sleep(1000);
    Found = WaitForCamera(4000, Path, ARRAYSIZE(Path), &Elapsed);
    ok(Found, "The camera went away when the old handles closed, %lu ms\n", Elapsed);
    if (Found)
        StreamGoodFrames(Path, "after closing the old handles", Request, Buffer);
}
