/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows 11 contract of the budget, shared-primary lock, runtime data, cancel and duplication entry points
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include "precomp.h"

typedef PVOID PFN_REGISTERBUDGET;
typedef PVOID PFN_UNREGISTERBUDGET;
typedef NTSTATUS (APIENTRY *PFN_LOCKNOTIFY)(D3DKMT_SHAREDPRIMARYLOCKNOTIFICATION *);
typedef NTSTATUS (APIENTRY *PFN_UNLOCKNOTIFY)(D3DKMT_SHAREDPRIMARYUNLOCKNOTIFICATION *);
typedef NTSTATUS (APIENTRY *PFN_GETRUNTIMEDATA)(D3DKMT_GETRUNTIMEDATA *);
typedef NTSTATUS (APIENTRY *PFN_CANCELPRESENTS)(D3DKMT_CANCEL_PRESENTS *);
typedef NTSTATUS (APIENTRY *PFN_CREATEOUTPUTDUPL)(D3DKMT_CREATE_OUTPUTDUPL *);
typedef NTSTATUS (APIENTRY *PFN_DESTROYOUTPUTDUPL)(D3DKMT_DESTROY_OUTPUTDUPL *);
typedef NTSTATUS (APIENTRY *PFN_OUTPUTDUPLGETFRAMEINFO)(D3DKMT_OUTPUTDUPL_GET_FRAMEINFO *);
typedef NTSTATUS (APIENTRY *PFN_OUTPUTDUPLRELEASEFRAME)(D3DKMT_OUTPUTDUPL_RELEASE_FRAME *);


#define PROBE(Name, Call)                                                        \
    do {                                                                         \
        NTSTATUS Probe_;                                                         \
        _SEH2_TRY { Probe_ = (Call); }                                           \
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { Probe_ = _SEH2_GetExceptionCode(); } \
        _SEH2_END;                                                               \
        trace("%s: 0x%08lx\n", Name, (ULONG)Probe_);                             \
    } while (0)

#define EXPECT(Name, Call, Expected)                                             \
    do {                                                                         \
        NTSTATUS Expect_;                                                        \
        _SEH2_TRY { Expect_ = (Call); }                                          \
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { Expect_ = _SEH2_GetExceptionCode(); } \
        _SEH2_END;                                                               \
        ok(Expect_ == (NTSTATUS)(Expected), "%s: 0x%08lx, expected 0x%08lx\n",   \
           Name, (ULONG)Expect_, (ULONG)(Expected));                             \
    } while (0)

#ifndef STATUS_NOT_IMPLEMENTED
#define STATUS_NOT_IMPLEMENTED ((NTSTATUS)0xC0000002L)
#endif
#ifndef STATUS_INVALID_HANDLE
#define STATUS_INVALID_HANDLE ((NTSTATUS)0xC0000008L)
#endif
#ifndef STATUS_NOT_FOUND
#define STATUS_NOT_FOUND ((NTSTATUS)0xC0000225L)
#endif

static LUID
DisplayLuid(VOID)
{
    PFN_D3DKMTOpenAdapterFromGdiDisplayName OpenName =
        (PFN_D3DKMTOpenAdapterFromGdiDisplayName)LoadD3DKMTProc("D3DKMTOpenAdapterFromGdiDisplayName");
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME Open;
    LUID Luid = { 0, 0 };

    memset(&Open, 0, sizeof(Open));
    wcscpy(Open.DeviceName, L"\\\\.\\DISPLAY1");
    if (OpenName != NULL && NT_SUCCESS(OpenName(&Open)))
    {
        Luid = Open.AdapterLuid;
        CloseAdapter(Open.hAdapter);
    }
    return Luid;
}

START_TEST(stubparity)
{
    PFN_REGISTERBUDGET pRegister = (PFN_REGISTERBUDGET)LoadD3DKMTProc("D3DKMTRegisterBudgetChangeNotification");
    PFN_UNREGISTERBUDGET pUnregister = (PFN_UNREGISTERBUDGET)LoadD3DKMTProc("D3DKMTUnregisterBudgetChangeNotification");
    PFN_LOCKNOTIFY pLock = (PFN_LOCKNOTIFY)LoadD3DKMTProc("D3DKMTSharedPrimaryLockNotification");
    PFN_UNLOCKNOTIFY pUnlock = (PFN_UNLOCKNOTIFY)LoadD3DKMTProc("D3DKMTSharedPrimaryUnLockNotification");
    PFN_GETRUNTIMEDATA pRuntime = (PFN_GETRUNTIMEDATA)LoadD3DKMTProc("D3DKMTGetRuntimeData");
    PFN_CANCELPRESENTS pCancel = (PFN_CANCELPRESENTS)LoadD3DKMTProc("D3DKMTCancelPresents");
    PFN_CREATEOUTPUTDUPL pCreateDupl = (PFN_CREATEOUTPUTDUPL)LoadD3DKMTProc("D3DKMTCreateOutputDupl");
    PFN_DESTROYOUTPUTDUPL pDestroyDupl = (PFN_DESTROYOUTPUTDUPL)LoadD3DKMTProc("D3DKMTDestroyOutputDupl");
    PFN_OUTPUTDUPLGETFRAMEINFO pFrameInfo = (PFN_OUTPUTDUPLGETFRAMEINFO)LoadD3DKMTProc("D3DKMTOutputDuplGetFrameInfo");
    PFN_OUTPUTDUPLRELEASEFRAME pRelease = (PFN_OUTPUTDUPLRELEASEFRAME)LoadD3DKMTProc("D3DKMTOutputDuplReleaseFrame");
    D3DKMT_HANDLE hDisplay, hRender, hDevice;
    LUID Luid;

    hDisplay = OpenAdapterFromDisplay1();
    hRender = OpenRenderAdapter();
    hDevice = hRender ? CreateTestDevice(hRender) : 0;
    Luid = DisplayLuid();
    trace("display adapter 0x%lx render adapter 0x%lx device 0x%lx luid %08lx:%08lx\n",
          (ULONG)hDisplay, (ULONG)hRender, (ULONG)hDevice, (ULONG)Luid.HighPart, Luid.LowPart);

    ok(pRegister == NULL && pUnregister == NULL, "gdi32 exports the budget notifications: %p %p\n", pRegister, pUnregister);
    if (pLock && pUnlock)
    {
        D3DKMT_SHAREDPRIMARYLOCKNOTIFICATION Lock;
        D3DKMT_SHAREDPRIMARYUNLOCKNOTIFICATION Unlock;

        memset(&Unlock, 0, sizeof(Unlock));
        Unlock.AdapterLuid = Luid;
        EXPECT("SharedPrimaryUnLockNotification(display, source 0, no lock)", pUnlock(&Unlock), STATUS_INVALID_PARAMETER);
        memset(&Lock, 0, sizeof(Lock));
        Lock.AdapterLuid = Luid;
        EXPECT("SharedPrimaryLockNotification(display, source 0)", pLock(&Lock), STATUS_SUCCESS);
        memset(&Unlock, 0, sizeof(Unlock));
        Unlock.AdapterLuid = Luid;
        EXPECT("SharedPrimaryUnLockNotification(display, source 0)", pUnlock(&Unlock), STATUS_INVALID_PARAMETER);
        Lock.VidPnSourceId = 7;
        EXPECT("SharedPrimaryLockNotification(source 7)", pLock(&Lock), STATUS_SUCCESS);
        Lock.LockRect.right = 16;
        Lock.LockRect.bottom = 16;
        EXPECT("SharedPrimaryLockNotification(rect)", pLock(&Lock), STATUS_SUCCESS);
        memset(&Lock, 0, sizeof(Lock));
        EXPECT("SharedPrimaryLockNotification(zero LUID)", pLock(&Lock), STATUS_SUCCESS);
        EXPECT("SharedPrimaryLockNotification(NULL)", pLock(NULL), STATUS_INVALID_PARAMETER);
        memset(&Unlock, 0, sizeof(Unlock));
        EXPECT("SharedPrimaryUnLockNotification(zero LUID)", pUnlock(&Unlock), STATUS_SUCCESS);
        Unlock.AdapterLuid = Luid;
        Unlock.VidPnSourceId = 7;
        EXPECT("SharedPrimaryUnLockNotification(source 7)", pUnlock(&Unlock), STATUS_SUCCESS);
        EXPECT("SharedPrimaryUnLockNotification(NULL)", pUnlock(NULL), STATUS_INVALID_PARAMETER);
    }

    if (pRuntime)
    {
        D3DKMT_GETRUNTIMEDATA Runtime;
        BYTE Buffer[256];

        memset(&Runtime, 0, sizeof(Runtime));
        Runtime.hAdapter = hRender;
        Runtime.pRuntimeData = Buffer;
        Runtime.RuntimeDataSize = sizeof(Buffer);
        EXPECT("GetRuntimeData(no share)", pRuntime(&Runtime), STATUS_NOT_IMPLEMENTED);
        Runtime.hGlobalShare = 0x40000042;
        EXPECT("GetRuntimeData(bogus share)", pRuntime(&Runtime), STATUS_NOT_IMPLEMENTED);
        EXPECT("GetRuntimeData(NULL)", pRuntime(NULL), STATUS_NOT_IMPLEMENTED);
    }

    if (pCancel)
    {
        D3DKMT_CANCEL_PRESENTS Cancel;

        memset(&Cancel, 0, sizeof(Cancel));
        Cancel.cbSize = sizeof(Cancel);
        Cancel.hDevice = hDevice;
        EXPECT("CancelPresents(device, operation 0)", pCancel(&Cancel), STATUS_INVALID_PARAMETER);
        Cancel.Operation = (D3DKMT_CANCEL_PRESENTS_OPERATION)1;
        EXPECT("CancelPresents(device, operation 1)", pCancel(&Cancel), STATUS_INVALID_PARAMETER);
        Cancel.Operation = (D3DKMT_CANCEL_PRESENTS_OPERATION)0;
        Cancel.cbSize = sizeof(Cancel) - 8;
        EXPECT("CancelPresents(short size)", pCancel(&Cancel), STATUS_INVALID_PARAMETER);
        Cancel.cbSize = sizeof(Cancel);
        Cancel.hDevice = 0;
        EXPECT("CancelPresents(no device)", pCancel(&Cancel), STATUS_INVALID_PARAMETER);
        EXPECT("CancelPresents(NULL)", pCancel(NULL), STATUS_INVALID_PARAMETER);
        Cancel.cbSize = sizeof(Cancel);
        Cancel.hDevice = hDevice;
        Cancel.CompSurfaceLuid.LowPart = 0x1234;
        EXPECT("CancelPresents(device, unknown surface)", pCancel(&Cancel), STATUS_SUCCESS);
        Cancel.Operation = (D3DKMT_CANCEL_PRESENTS_OPERATION)1;
        EXPECT("CancelPresents(device, unknown surface, operation 1)", pCancel(&Cancel), STATUS_SUCCESS);
        Cancel.Operation = (D3DKMT_CANCEL_PRESENTS_OPERATION)0;
        Cancel.hDevice = 0;
        EXPECT("CancelPresents(no device, surface LUID)", pCancel(&Cancel), STATUS_INVALID_PARAMETER);
    }

    if (pCreateDupl && pDestroyDupl)
    {
        D3DKMT_CREATE_OUTPUTDUPL Create;
        D3DKMT_DESTROY_OUTPUTDUPL Destroy;

        memset(&Create, 0, sizeof(Create));
        Create.hAdapter = hDisplay;
        EXPECT("CreateOutputDupl(display, source 0, precheck)", pCreateDupl(&Create), STATUS_SUCCESS);
        ok(Create.RequiredKeyedMutexCount == 1, "required keyed mutexes %u\n", Create.RequiredKeyedMutexCount);
        memset(&Destroy, 0, sizeof(Destroy));
        Destroy.hAdapter = hDisplay;
        EXPECT("DestroyOutputDupl(display, source 0)", pDestroyDupl(&Destroy), STATUS_SUCCESS);
        EXPECT("DestroyOutputDupl(again)", pDestroyDupl(&Destroy), STATUS_SUCCESS);
        Destroy.VidPnSourceId = 7;
        EXPECT("DestroyOutputDupl(source 7)", pDestroyDupl(&Destroy), STATUS_INVALID_PARAMETER);
        Create.KeyedMutexCount = 1;
        Create.KeyedMutexs[0].hSharedSurfaceNt = (HANDLE)(ULONG_PTR)0xdead0;
        EXPECT("CreateOutputDupl(bogus keyed mutex)", pCreateDupl(&Create), STATUS_INVALID_HANDLE);
        memset(&Create, 0, sizeof(Create));
        Create.hAdapter = hDisplay;
        Create.VidPnSourceId = 7;
        EXPECT("CreateOutputDupl(source 7)", pCreateDupl(&Create), STATUS_INVALID_PARAMETER);
        EXPECT("CreateOutputDupl(NULL)", pCreateDupl(NULL), STATUS_INVALID_PARAMETER);
    }
    if (pFrameInfo && pRelease)
    {
        D3DKMT_OUTPUTDUPL_GET_FRAMEINFO Info;
        D3DKMT_OUTPUTDUPL_RELEASE_FRAME Release;

        memset(&Info, 0, sizeof(Info));
        Info.hAdapter = hDisplay;
        EXPECT("OutputDuplGetFrameInfo(no duplication)", pFrameInfo(&Info), STATUS_NOT_FOUND);
        memset(&Release, 0, sizeof(Release));
        Release.hAdapter = hDisplay;
        EXPECT("OutputDuplReleaseFrame(no duplication)", pRelease(&Release), STATUS_NOT_FOUND);
        Info.VidPnSourceId = 7;
        EXPECT("OutputDuplGetFrameInfo(source 7)", pFrameInfo(&Info), STATUS_INVALID_PARAMETER);
        EXPECT("OutputDuplGetFrameInfo(NULL)", pFrameInfo(NULL), STATUS_INVALID_PARAMETER);
    }

    DestroyTestDevice(hDevice);
    CloseAdapter(hRender);
    CloseAdapter(hDisplay);
}
