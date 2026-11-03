/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel desktop duplication entry points driven with a keyed-mutex surface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#undef DXGKDDI_INTERFACE_VERSION
#define DXGKDDI_INTERFACE_VERSION 0x11007
#include <apitest.h>
#include <initguid.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dkmthk.h>

typedef HRESULT (WINAPI *CREATE_DEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *,
                                         UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

#define LOAD(Type, Name) reinterpret_cast<Type>(GetProcAddress(GetModuleHandleW(L"gdi32.dll"), Name))

START_TEST(kmt_outputdupl)
{
    PFND3DKMT_OPENADAPTERFROMGDIDISPLAYNAME OpenName = LOAD(PFND3DKMT_OPENADAPTERFROMGDIDISPLAYNAME, "D3DKMTOpenAdapterFromGdiDisplayName");
    typedef NTSTATUS (APIENTRY *PCREATE)(D3DKMT_CREATE_OUTPUTDUPL *);
    typedef NTSTATUS (APIENTRY *PDESTROY)(D3DKMT_DESTROY_OUTPUTDUPL *);
    typedef NTSTATUS (APIENTRY *PFRAMEINFO)(D3DKMT_OUTPUTDUPL_GET_FRAMEINFO *);
    typedef NTSTATUS (APIENTRY *PMETADATA)(D3DKMT_OUTPUTDUPL_METADATA *);
    typedef NTSTATUS (APIENTRY *PSHAPE)(D3DKMT_OUTPUTDUPL_GET_POINTER_SHAPE_DATA *);
    typedef NTSTATUS (APIENTRY *PRELEASE)(D3DKMT_OUTPUTDUPL_RELEASE_FRAME *);
    PCREATE Create = LOAD(PCREATE, "D3DKMTCreateOutputDupl");
    PDESTROY Destroy = LOAD(PDESTROY, "D3DKMTDestroyOutputDupl");
    PFRAMEINFO FrameInfo = LOAD(PFRAMEINFO, "D3DKMTOutputDuplGetFrameInfo");
    PMETADATA MetaData = LOAD(PMETADATA, "D3DKMTOutputDuplGetMetaData");
    PSHAPE Shape = LOAD(PSHAPE, "D3DKMTOutputDuplGetPointerShapeData");
    PRELEASE Release = LOAD(PRELEASE, "D3DKMTOutputDuplReleaseFrame");
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME Open = {};
    D3DKMT_CREATE_OUTPUTDUPL Dupl = {};
    D3DKMT_DESTROY_OUTPUTDUPL DestroyArgs = {};
    ID3D11Device *Device = NULL;
    ID3D11Texture2D *Texture = NULL;
    IDXGIResource1 *Resource = NULL;
    HANDLE Shared = NULL;
    NTSTATUS Status;
    HRESULT Hr;

    if (!OpenName || !Create || !Destroy || !FrameInfo || !MetaData || !Shape || !Release)
    {
        skip("duplication entry points are not exported\n");
        return;
    }
    wcscpy(Open.DeviceName, L"\\\\.\\DISPLAY1");
    Status = OpenName(&Open);
    ok(Status == 0, "OpenAdapterFromGdiDisplayName: 0x%lx\n", Status);
    if (Status) return;

    Dupl.hAdapter = Open.hAdapter;
    Status = Create(&Dupl);
    ok(Status == 0 && Dupl.RequiredKeyedMutexCount == 1, "precheck: 0x%lx, required %u\n", Status, Dupl.RequiredKeyedMutexCount);

    HMODULE Module = LoadLibraryW(L"d3d11.dll");
    CREATE_DEVICE CreateDevice = Module ? reinterpret_cast<CREATE_DEVICE>(GetProcAddress(Module, "D3D11CreateDevice")) : NULL;
    if (CreateDevice)
        Hr = CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                          D3D11_SDK_VERSION, &Device, NULL, NULL);
    if (Device)
    {
        D3D11_TEXTURE2D_DESC Desc = {};

        Desc.Width = GetSystemMetrics(SM_CXSCREEN); Desc.Height = GetSystemMetrics(SM_CYSCREEN);
        Desc.MipLevels = 1; Desc.ArraySize = 1; Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; Desc.SampleDesc.Count = 1;
        Desc.Usage = D3D11_USAGE_DEFAULT; Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        Desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        Hr = Device->CreateTexture2D(&Desc, NULL, &Texture);
        trace("keyed-mutex surface %ux%u: %#lx\n", Desc.Width, Desc.Height, Hr);
        if (Texture && SUCCEEDED(Texture->QueryInterface(IID_IDXGIResource1, reinterpret_cast<void **>(&Resource))))
        {
            Hr = Resource->CreateSharedHandle(NULL, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, NULL, &Shared);
            ok(Hr == S_OK && Shared != NULL, "keyed-mutex surface NT handle: %#lx %p\n", Hr, Shared);
        }
    }
    if (Shared)
    {
        D3DKMT_OUTPUTDUPL_GET_FRAMEINFO Info = {};
        D3DKMT_OUTPUTDUPL_METADATA Meta = {};
        D3DKMT_OUTPUTDUPL_GET_POINTER_SHAPE_DATA ShapeData = {};
        D3DKMT_OUTPUTDUPL_RELEASE_FRAME ReleaseArgs = {};
        BYTE Buffer[4096];
        UINT Round;

        Dupl.KeyedMutexCount = 1;
        Dupl.KeyedMutexs[0].hSharedSurfaceNt = Shared;
        Status = Create(&Dupl);
        ok(Status == 0 && Dupl.RequiredKeyedMutexCount == 1, "create with surface: 0x%lx, required %u\n", Status, Dupl.RequiredKeyedMutexCount);
        Status = Create(&Dupl);
        ok(Status == (NTSTATUS)0xC0000718, "create again: 0x%lx\n", Status);
        for (Round = 0; Round < 3; ++Round)
        {
            if (Round == 1)
                InvalidateRect(NULL, NULL, TRUE);
            Sleep(300);
            memset(&Info, 0, sizeof(Info));
            Info.hAdapter = Open.hAdapter;
            Status = FrameInfo(&Info);
            ok(Status == (NTSTATUS)0xC0000225, "frame info %u without a frame: 0x%lx\n", Round, Status);
            trace("frame info %u: 0x%lx present %I64d mouse %I64d frames %u coalesced %d masked %d pointer (%ld,%ld) visible %d metadata %u shape %u\n",
                  Round, Status, Info.FrameInfo.LastPresentTime.QuadPart, Info.FrameInfo.LastMouseUpdateTime.QuadPart,
                  Info.FrameInfo.AccumulatedFrames, Info.FrameInfo.RectsCoalesced, Info.FrameInfo.ProtectedContentMaskedOut,
                  Info.FrameInfo.PointerPosition.Position.x, Info.FrameInfo.PointerPosition.Position.y,
                  Info.FrameInfo.PointerPosition.Visible, Info.FrameInfo.TotalMetadataBufferSize,
                  Info.FrameInfo.PointerShapeBufferSize);
            memset(&Meta, 0, sizeof(Meta));
            Meta.hAdapter = Open.hAdapter;
            Meta.Type = D3DKMT_OUTPUTDUPL_METADATATYPE_DIRTY_RECTS;
            Status = MetaData(&Meta);
            trace("  dirty size query: 0x%lx required %u\n", Status, Meta.BufferSizeRequired);
            Meta.pBuffer = Buffer;
            Meta.BufferSizeSupplied = sizeof(Buffer);
            Status = MetaData(&Meta);
            trace("  dirty rects: 0x%lx required %u first (%ld,%ld)-(%ld,%ld)\n", Status, Meta.BufferSizeRequired,
                  ((RECT *)Buffer)->left, ((RECT *)Buffer)->top, ((RECT *)Buffer)->right, ((RECT *)Buffer)->bottom);
            Meta.Type = D3DKMT_OUTPUTDUPL_METADATATYPE_MOVE_RECTS;
            Status = MetaData(&Meta);
            trace("  move rects: 0x%lx required %u\n", Status, Meta.BufferSizeRequired);
            memset(&ShapeData, 0, sizeof(ShapeData));
            ShapeData.hAdapter = Open.hAdapter;
            ShapeData.pShapeBuffer = Buffer;
            ShapeData.BufferSizeSupplied = sizeof(Buffer);
            Status = Shape(&ShapeData);
            ok(Status == (NTSTATUS)0xC0000225, "pointer shape without a frame: 0x%lx\n", Status);
            trace("  pointer shape: 0x%lx required %u type %u %ux%u pitch %u hotspot (%ld,%ld)\n", Status,
                  ShapeData.BufferSizeRequired, ShapeData.ShapeInfo.Type, ShapeData.ShapeInfo.Width,
                  ShapeData.ShapeInfo.Height, ShapeData.ShapeInfo.Pitch, ShapeData.ShapeInfo.HotSpot.x,
                  ShapeData.ShapeInfo.HotSpot.y);
            memset(&ReleaseArgs, 0, sizeof(ReleaseArgs));
            ReleaseArgs.hAdapter = Open.hAdapter;
            ReleaseArgs.NextKeyMutexIdx = 0;
            Status = Release(&ReleaseArgs);
            ok(Status == (NTSTATUS)0xC0000225, "release without a frame: 0x%lx\n", Status);
            trace("  release: 0x%lx next %u\n", Status, ReleaseArgs.NextKeyMutexIdx);
        }
        DestroyArgs.hAdapter = Open.hAdapter;
        Status = Destroy(&DestroyArgs);
        ok(Status == 0, "destroy: 0x%lx\n", Status);
        memset(&Info, 0, sizeof(Info));
        Info.hAdapter = Open.hAdapter;
        Status = FrameInfo(&Info);
        ok(Status == (NTSTATUS)0xC0000225, "frame info after destroy: 0x%lx\n", Status);
        CloseHandle(Shared);
    }
    if (Resource) Resource->Release();
    if (Texture) Texture->Release();
    if (Device) Device->Release();
    if (Module) FreeLibrary(Module);
    D3DKMT_CLOSEADAPTER Close = {};
    Close.hAdapter = Open.hAdapter;
    PFND3DKMT_CLOSEADAPTER CloseAdapter = LOAD(PFND3DKMT_CLOSEADAPTER, "D3DKMTCloseAdapter");
    if (CloseAdapter) CloseAdapter(&Close);
}
