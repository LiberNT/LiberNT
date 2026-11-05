/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for removing a USB video class camera through the PnP manager
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lntuvc_test.h"
#include <cfgmgr32.h>

typedef struct _UVC_FRAME_REQUEST
{
    KSSTREAM_HEADER Header;
    KS_FRAME_INFO FrameInfo;
} UVC_FRAME_REQUEST;

static BOOL
FindCameraInstance(WCHAR *Path, DWORD PathChars, DEVINST *Instance)
{
    HDEVINFO Set;
    SP_DEVICE_INTERFACE_DATA Interface;
    SP_DEVINFO_DATA Info;
    BYTE Buffer[1024];
    SP_DEVICE_INTERFACE_DETAIL_DATA_W *Detail = (PVOID)Buffer;
    DWORD Index;
    BOOL Found = FALSE;

    Set = SetupDiGetClassDevsW(&UvcCategoryCapture, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (Set == INVALID_HANDLE_VALUE)
        return FALSE;

    Interface.cbSize = sizeof(Interface);
    for (Index = 0; SetupDiEnumDeviceInterfaces(Set, NULL, &UvcCategoryCapture, Index, &Interface); Index++)
    {
        Detail->cbSize = sizeof(*Detail);
        Info.cbSize = sizeof(Info);
        if (!SetupDiGetDeviceInterfaceDetailW(Set, &Interface, Detail, sizeof(Buffer), NULL, &Info))
            continue;
        if (wcsstr(_wcslwr(Detail->DevicePath), L"vid_46f4&pid_0099"))
        {
            lstrcpynW(Path, Detail->DevicePath, PathChars);
            *Instance = Info.DevInst;
            Found = TRUE;
        }
    }

    SetupDiDestroyDeviceInfoList(Set);
    return Found;
}

static void
StreamOneFrame(const WCHAR *Path)
{
    KS_DATAFORMAT_VIDEOINFOHEADER Format;
    UVC_FRAME_REQUEST Request;
    HANDLE Filter, Pin = NULL;
    PBYTE Buffer;
    DWORD Size = 320 * 240 * 2, Returned = 0, Error, Start;
    ULONG Sequence = 0, Errors;

    Start = GetTickCount();
    do
    {
        Filter = UvcOpenFilter(Path);
        if (Filter == INVALID_HANDLE_VALUE)
            Sleep(100);
    } while (Filter == INVALID_HANDLE_VALUE && GetTickCount() - Start < 3000);
    ok(Filter != INVALID_HANDLE_VALUE, "Opening the filter failed %lu\n", GetLastError());
    if (Filter == INVALID_HANDLE_VALUE)
        return;

    UvcBuildFormat(&Format, &UvcSubtypeYuy2, UVC_FOURCC('Y', 'U', 'Y', '2'), 320, 240, 666666);
    Error = UvcCreatePin(Filter, 0, &Format, &Pin);
    ok(Error == ERROR_SUCCESS, "KsCreatePin failed %lu\n", Error);
    if (!Error)
    {
        Buffer = VirtualAlloc(NULL, Size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        ok(UvcSetPinState(Pin, KSSTATE_ACQUIRE) == ERROR_SUCCESS, "ACQUIRE failed %lu\n", GetLastError());
        ok(UvcSetPinState(Pin, KSSTATE_PAUSE) == ERROR_SUCCESS, "PAUSE failed %lu\n", GetLastError());
        ok(UvcSetPinState(Pin, KSSTATE_RUN) == ERROR_SUCCESS, "RUN failed %lu\n", GetLastError());

        ZeroMemory(&Request, sizeof(Request));
        Request.Header.Size = sizeof(Request);
        Request.Header.FrameExtent = Size;
        Request.Header.Data = Buffer;
        Request.FrameInfo.ExtendedHeaderSize = sizeof(KS_FRAME_INFO);
        if (UvcIoctl(Pin, IOCTL_KS_READ_STREAM, NULL, 0, &Request, sizeof(Request), &Returned))
        {
            Errors = UvcCheckYuy2Frame(Buffer, 320, 240, &Sequence);
            ok(Errors == 0, "Frame has %lu wrong pixels\n", Errors);
        }
        else
        {
            ok(0, "Reading a frame failed %lu\n", GetLastError());
        }

        UvcSetPinState(Pin, KSSTATE_STOP);
        CloseHandle(Pin);
        VirtualFree(Buffer, 0, MEM_RELEASE);
    }
    CloseHandle(Filter);
}

START_TEST(KsDisable)
{
    WCHAR Path[512], VetoName[MAX_PATH], DeviceId[MAX_DEVICE_ID_LEN];
    DEVINST Instance = 0, Root = 0;
    PNP_VETO_TYPE Veto;
    HANDLE Filter;
    CONFIGRET Result;
    DWORD Start;
    BOOL Found;

    if (!FindCameraInstance(Path, ARRAYSIZE(Path), &Instance))
    {
        skip("No usb-uvc test camera\n");
        return;
    }

    Result = CM_Get_Device_IDW(Instance, DeviceId, ARRAYSIZE(DeviceId), 0);
    ok(Result == CR_SUCCESS, "CM_Get_Device_IDW returned %lu\n", Result);

    Filter = UvcOpenFilter(Path);
    ok(Filter != INVALID_HANDLE_VALUE, "Opening the filter failed %lu\n", GetLastError());
    if (Filter == INVALID_HANDLE_VALUE)
        return;

    Veto = PNP_VetoTypeUnknown;
    ZeroMemory(VetoName, sizeof(VetoName));
    Result = CM_Query_And_Remove_SubTreeW(Instance, &Veto, VetoName, ARRAYSIZE(VetoName), 0);
    ok(Result == CR_REMOVE_VETOED, "Removing with a handle open: cr %lu\n", Result);
    ok(Veto == PNP_VetoOutstandingOpen, "Removing with a handle open: veto %d\n", Veto);
    ok(!_wcsicmp(VetoName, DeviceId), "Removing with a handle open: veto name '%ls', device '%ls'\n", VetoName, DeviceId);

    ok(UvcFindCamera(&UvcCategoryCapture, Path, ARRAYSIZE(Path)), "The camera went away after the veto\n");
    CloseHandle(Filter);
    StreamOneFrame(Path);

    if (!FindCameraInstance(Path, ARRAYSIZE(Path), &Instance))
    {
        ok(0, "The camera went away after the veto\n");
        return;
    }

    Veto = PNP_VetoTypeUnknown;
    ZeroMemory(VetoName, sizeof(VetoName));
    Result = CM_Query_And_Remove_SubTreeW(Instance, &Veto, VetoName, ARRAYSIZE(VetoName), 0);
    ok(Result == CR_SUCCESS, "Removing with no handle open: cr %lu veto %d\n", Result, Veto);
    ok(!UvcFindCamera(&UvcCategoryCapture, Path, ARRAYSIZE(Path)), "The removed camera is still listed\n");

    Result = CM_Locate_DevNodeW(&Root, NULL, CM_LOCATE_DEVNODE_NORMAL);
    ok(Result == CR_SUCCESS, "CM_Locate_DevNodeW returned %lu\n", Result);
    Result = CM_Reenumerate_DevNode(Root, CM_REENUMERATE_SYNCHRONOUS);
    ok(Result == CR_SUCCESS, "CM_Reenumerate_DevNode returned %lu\n", Result);

    Start = GetTickCount();
    do
    {
        Found = UvcFindCamera(&UvcCategoryCapture, Path, ARRAYSIZE(Path));
        if (!Found)
            Sleep(200);
    } while (!Found && GetTickCount() - Start < 4000);
    ok(Found, "The camera did not come back\n");
    if (Found)
        StreamOneFrame(Path);
}
