/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows Runtime Direct3D device and surface objects created from DXGI interfaces
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <initguid.h>
#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Graphics_DirectX
#define WIDL_using_Windows_Graphics_DirectX_Direct3D11
#include <windows.foundation.h>
#include <windows.graphics.directx.direct3d11.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <weakreference.h>
#include <winstring.h>
#include <wine/test.h>

#ifndef RO_E_CLOSED
#define RO_E_CLOSED ((HRESULT)0x80000013)
#endif

DEFINE_GUID(IID_IAgileObject_, 0x94ea2b94, 0xe9cc, 0x49e0, 0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90);
DEFINE_GUID(IID_IMarshal_, 0x00000003, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
DEFINE_GUID(CLSID_InProcFreeMarshaler_, 0x0000033a, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

static HRESULT (WINAPI *pCreateDirect3D11DeviceFromDXGIDevice)(IDXGIDevice *, IInspectable **);
static HRESULT (WINAPI *pCreateDirect3D11SurfaceFromDXGISurface)(IDXGISurface *, IInspectable **);

static ULONG RefCount(IUnknown *Object)
{
    IUnknown_AddRef(Object);
    return IUnknown_Release(Object);
}

static HRESULT QueryAndRelease(IUnknown *Object, REFIID Iid)
{
    IUnknown *out = (IUnknown *)(ULONG_PTR)0xdeadbeef;
    HRESULT hr = IUnknown_QueryInterface(Object, Iid, (void **)&out);

    if (SUCCEEDED(hr))
        IUnknown_Release(out);
    else
        ok(out == NULL, "QueryInterface failure left %p\n", out);
    return hr;
}

static void TestInspectable(IInspectable *Object, const WCHAR *ClassName, REFIID FirstIid)
{
    HSTRING name = NULL;
    TrustLevel trust = FullTrust;
    ULONG count = 0xdeadbeef;
    const IID *expected[] = {FirstIid, &IID_IWeakReferenceSource, &IID_IClosable};
    IID *iids = NULL;
    HRESULT hr;
    ULONG i;

    hr = IInspectable_GetRuntimeClassName(Object, &name);
    ok(hr == S_OK, "GetRuntimeClassName: 0x%08lx\n", hr);
    if (hr == S_OK)
    {
        ok(!lstrcmpW(WindowsGetStringRawBuffer(name, NULL), ClassName), "Runtime class %s\n",
           wine_dbgstr_w(WindowsGetStringRawBuffer(name, NULL)));
        WindowsDeleteString(name);
    }

    hr = IInspectable_GetTrustLevel(Object, &trust);
    ok(hr == S_OK, "GetTrustLevel: 0x%08lx\n", hr);
    ok(trust == BaseTrust, "Trust level %d\n", trust);

    hr = IInspectable_GetIids(Object, &count, &iids);
    ok(hr == S_OK, "GetIids: 0x%08lx\n", hr);
    ok(count == ARRAYSIZE(expected), "GetIids count %lu\n", count);
    for (i = 0; i < count && i < ARRAYSIZE(expected) && iids; i++)
        ok(IsEqualGUID(&iids[i], expected[i]), "IID %lu %s\n", i, wine_dbgstr_guid(&iids[i]));
    CoTaskMemFree(iids);
}

static IWeakReference *TestWeakAndMarshal(IInspectable *Object)
{
    IWeakReferenceSource *source = NULL;
    IWeakReference *weak = NULL;
    IInspectable *resolved;
    IMarshal *marshal = NULL;
    CLSID clsid;
    HRESULT hr;

    hr = IInspectable_QueryInterface(Object, &IID_IMarshal_, (void **)&marshal);
    ok(hr == S_OK, "IMarshal: 0x%08lx\n", hr);
    if (marshal)
    {
        hr = IMarshal_GetUnmarshalClass(marshal, &IID_IInspectable, Object, MSHCTX_INPROC, NULL,
                                        MSHLFLAGS_NORMAL, &clsid);
        ok(hr == S_OK, "GetUnmarshalClass: 0x%08lx\n", hr);
        ok(IsEqualGUID(&clsid, &CLSID_InProcFreeMarshaler_), "Unmarshal class %s\n", wine_dbgstr_guid(&clsid));
        IMarshal_Release(marshal);
    }

    hr = IInspectable_QueryInterface(Object, &IID_IWeakReferenceSource, (void **)&source);
    ok(hr == S_OK, "IWeakReferenceSource: 0x%08lx\n", hr);
    if (!source)
        return NULL;
    hr = IWeakReferenceSource_GetWeakReference(source, &weak);
    ok(hr == S_OK, "GetWeakReference: 0x%08lx\n", hr);
    IWeakReferenceSource_Release(source);
    if (!weak)
        return NULL;

    resolved = NULL;
    hr = IWeakReference_Resolve(weak, &IID_IInspectable, &resolved);
    ok(hr == S_OK, "Resolve: 0x%08lx\n", hr);
    ok(resolved == Object, "Resolved %p, expected %p\n", resolved, Object);
    if (resolved)
        IInspectable_Release(resolved);
    resolved = (IInspectable *)(ULONG_PTR)0xdeadbeef;
    hr = IWeakReference_Resolve(weak, &IID_ID3D11Device, &resolved);
    ok(hr == E_NOINTERFACE, "Resolve ID3D11Device: 0x%08lx\n", hr);
    ok(resolved == NULL, "Resolve ID3D11Device left %p\n", resolved);
    return weak;
}

static void TestWeakReleased(IWeakReference *Weak)
{
    IInspectable *resolved = (IInspectable *)(ULONG_PTR)0xdeadbeef;
    HRESULT hr;

    if (!Weak)
        return;
    hr = IWeakReference_Resolve(Weak, &IID_IInspectable, &resolved);
    ok(hr == S_OK, "Resolve after Release: 0x%08lx\n", hr);
    ok(resolved == NULL, "Resolve after Release gave %p\n", resolved);
    ok(IWeakReference_Release(Weak) == 0, "Weak reference still referenced\n");
}

static void TestDevice(ID3D11Device *Device)
{
    IDirect3DDxgiInterfaceAccess *access = NULL;
    IInspectable *object = NULL, *second = NULL;
    IWeakReference *weak = NULL;
    IDirect3DDevice *winrt = NULL;
    IClosable *closable = NULL;
    IDXGIDevice *dxgi = NULL;
    IUnknown *out, *unk1, *unk2;
    ULONG before;
    HRESULT hr;

    hr = ID3D11Device_QueryInterface(Device, &IID_IDXGIDevice, (void **)&dxgi);
    ok(hr == S_OK, "IDXGIDevice: 0x%08lx\n", hr);
    if (FAILED(hr))
        return;

    object = (IInspectable *)(ULONG_PTR)0xdeadbeef;
    hr = pCreateDirect3D11DeviceFromDXGIDevice(NULL, &object);
    ok(hr == E_INVALIDARG, "NULL device: 0x%08lx\n", hr);
    ok(object == (IInspectable *)(ULONG_PTR)0xdeadbeef, "NULL device set %p\n", object);

    before = RefCount((IUnknown *)dxgi);
    object = NULL;
    hr = pCreateDirect3D11DeviceFromDXGIDevice(dxgi, &object);
    ok(hr == S_OK, "CreateDirect3D11DeviceFromDXGIDevice: 0x%08lx\n", hr);
    if (FAILED(hr))
    {
        IDXGIDevice_Release(dxgi);
        return;
    }
    ok(RefCount((IUnknown *)dxgi) == before + 1, "DXGI device references %lu, was %lu\n",
       RefCount((IUnknown *)dxgi), before);

    hr = pCreateDirect3D11DeviceFromDXGIDevice(dxgi, &second);
    ok(hr == S_OK, "Second call: 0x%08lx\n", hr);
    ok(second != object, "Second call returned the same object\n");
    if (second)
        IInspectable_Release(second);

    ok(QueryAndRelease((IUnknown *)object, &IID_IUnknown) == S_OK, "IUnknown\n");
    ok(QueryAndRelease((IUnknown *)object, &IID_IInspectable) == S_OK, "IInspectable\n");
    ok(QueryAndRelease((IUnknown *)object, &IID_IAgileObject_) == S_OK, "IAgileObject\n");
    weak = TestWeakAndMarshal(object);
    ok(QueryAndRelease((IUnknown *)object, &IID_IDXGIDevice) == E_NOINTERFACE, "IDXGIDevice\n");
    ok(QueryAndRelease((IUnknown *)object, &IID_ID3D11Device) == E_NOINTERFACE, "ID3D11Device\n");
    ok(QueryAndRelease((IUnknown *)object, &IID_IDirect3DSurface) == E_NOINTERFACE, "IDirect3DSurface\n");

    hr = IInspectable_QueryInterface(object, &IID_IDirect3DDevice, (void **)&winrt);
    ok(hr == S_OK, "IDirect3DDevice: 0x%08lx\n", hr);
    hr = IInspectable_QueryInterface(object, &IID_IClosable, (void **)&closable);
    ok(hr == S_OK, "IClosable: 0x%08lx\n", hr);
    hr = IInspectable_QueryInterface(object, &IID_IDirect3DDxgiInterfaceAccess, (void **)&access);
    ok(hr == S_OK, "IDirect3DDxgiInterfaceAccess: 0x%08lx\n", hr);
    if (!winrt || !closable || !access)
        goto done;

    IInspectable_QueryInterface(object, &IID_IUnknown, (void **)&unk1);
    IDirect3DDxgiInterfaceAccess_QueryInterface(access, &IID_IUnknown, (void **)&unk2);
    ok(unk1 == unk2, "IUnknown identity %p, %p\n", unk1, unk2);
    IUnknown_Release(unk1);
    IUnknown_Release(unk2);

    TestInspectable((IInspectable *)winrt, L"Windows.Graphics.DirectX.Direct3D11.IDirect3DDevice",
                    &IID_IDirect3DDevice);

    out = NULL;
    hr = IDirect3DDxgiInterfaceAccess_GetInterface(access, &IID_IDXGIDevice, (void **)&out);
    ok(hr == S_OK, "GetInterface IDXGIDevice: 0x%08lx\n", hr);
    ok(out == (IUnknown *)dxgi, "IDXGIDevice %p, expected %p\n", out, dxgi);
    if (out)
        IUnknown_Release(out);
    out = NULL;
    hr = IDirect3DDxgiInterfaceAccess_GetInterface(access, &IID_ID3D11Device, (void **)&out);
    ok(hr == S_OK, "GetInterface ID3D11Device: 0x%08lx\n", hr);
    ok(out == (IUnknown *)Device, "ID3D11Device %p, expected %p\n", out, Device);
    if (out)
        IUnknown_Release(out);
    out = (IUnknown *)(ULONG_PTR)0xdeadbeef;
    hr = IDirect3DDxgiInterfaceAccess_GetInterface(access, &IID_ID3D11DeviceContext, (void **)&out);
    ok(hr == E_NOINTERFACE, "GetInterface ID3D11DeviceContext: 0x%08lx\n", hr);
    ok(out == NULL, "ID3D11DeviceContext left %p\n", out);
    hr = IDirect3DDxgiInterfaceAccess_GetInterface(access, &IID_IDXGIDevice, NULL);
    ok(hr == E_POINTER, "GetInterface NULL output: 0x%08lx\n", hr);

    hr = IDirect3DDevice_Trim(winrt);
    ok(hr == S_OK, "Trim: 0x%08lx\n", hr);

    before = RefCount((IUnknown *)dxgi);
    hr = IClosable_Close(closable);
    ok(hr == S_OK, "Close: 0x%08lx\n", hr);
    ok(RefCount((IUnknown *)dxgi) == before - 1, "DXGI device references after Close %lu, was %lu\n",
       RefCount((IUnknown *)dxgi), before);
    hr = IClosable_Close(closable);
    ok(hr == S_OK, "Second Close: 0x%08lx\n", hr);
    out = (IUnknown *)(ULONG_PTR)0xdeadbeef;
    hr = IDirect3DDxgiInterfaceAccess_GetInterface(access, &IID_IDXGIDevice, (void **)&out);
    ok(hr == RO_E_CLOSED, "GetInterface after Close: 0x%08lx\n", hr);
    ok(out == (IUnknown *)(ULONG_PTR)0xdeadbeef, "GetInterface after Close set %p\n", out);
    hr = IDirect3DDevice_Trim(winrt);
    ok(hr == RO_E_CLOSED, "Trim after Close: 0x%08lx\n", hr);

done:
    if (access)
        IDirect3DDxgiInterfaceAccess_Release(access);
    if (closable)
        IClosable_Release(closable);
    if (winrt)
        IDirect3DDevice_Release(winrt);
    before = RefCount((IUnknown *)dxgi);
    ok(IInspectable_Release(object) == 0, "Object still referenced\n");
    TestWeakReleased(weak);
    ok(RefCount((IUnknown *)dxgi) == before, "DXGI device references after Release %lu, was %lu\n",
       RefCount((IUnknown *)dxgi), before);
    IDXGIDevice_Release(dxgi);
}

static void TestSurface(ID3D11Device *Device)
{
    D3D11_TEXTURE2D_DESC desc = {64, 32, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT,
                                 D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0, 0};
    IDirect3DDxgiInterfaceAccess *access = NULL;
    Direct3DSurfaceDescription description;
    IDirect3DSurface *winrt = NULL;
    IClosable *closable = NULL;
    IInspectable *object = NULL;
    IWeakReference *weak = NULL;
    ID3D11Texture2D *texture;
    IDXGISurface *surface;
    IUnknown *out;
    HRESULT hr;

    hr = ID3D11Device_CreateTexture2D(Device, &desc, NULL, &texture);
    ok(hr == S_OK, "CreateTexture2D: 0x%08lx\n", hr);
    if (FAILED(hr))
        return;
    hr = ID3D11Texture2D_QueryInterface(texture, &IID_IDXGISurface, (void **)&surface);
    ok(hr == S_OK, "IDXGISurface: 0x%08lx\n", hr);
    if (FAILED(hr))
    {
        ID3D11Texture2D_Release(texture);
        return;
    }

    object = (IInspectable *)(ULONG_PTR)0xdeadbeef;
    hr = pCreateDirect3D11SurfaceFromDXGISurface(NULL, &object);
    ok(hr == E_INVALIDARG, "NULL surface: 0x%08lx\n", hr);
    ok(object == (IInspectable *)(ULONG_PTR)0xdeadbeef, "NULL surface set %p\n", object);

    object = NULL;
    hr = pCreateDirect3D11SurfaceFromDXGISurface(surface, &object);
    ok(hr == S_OK, "CreateDirect3D11SurfaceFromDXGISurface: 0x%08lx\n", hr);
    if (FAILED(hr))
        goto done;

    ok(QueryAndRelease((IUnknown *)object, &IID_IAgileObject_) == S_OK, "IAgileObject\n");
    weak = TestWeakAndMarshal(object);
    ok(QueryAndRelease((IUnknown *)object, &IID_IDXGISurface) == E_NOINTERFACE, "IDXGISurface\n");
    ok(QueryAndRelease((IUnknown *)object, &IID_IDirect3DDevice) == E_NOINTERFACE, "IDirect3DDevice\n");

    hr = IInspectable_QueryInterface(object, &IID_IDirect3DSurface, (void **)&winrt);
    ok(hr == S_OK, "IDirect3DSurface: 0x%08lx\n", hr);
    hr = IInspectable_QueryInterface(object, &IID_IClosable, (void **)&closable);
    ok(hr == S_OK, "IClosable: 0x%08lx\n", hr);
    hr = IInspectable_QueryInterface(object, &IID_IDirect3DDxgiInterfaceAccess, (void **)&access);
    ok(hr == S_OK, "IDirect3DDxgiInterfaceAccess: 0x%08lx\n", hr);
    if (!winrt || !closable || !access)
        goto done;

    TestInspectable((IInspectable *)winrt, L"Windows.Graphics.DirectX.Direct3D11.IDirect3DSurface",
                    &IID_IDirect3DSurface);

    memset(&description, 0xcc, sizeof(description));
    hr = IDirect3DSurface_get_Description(winrt, &description);
    ok(hr == S_OK, "get_Description: 0x%08lx\n", hr);
    ok(description.Width == 64 && description.Height == 32, "Size %dx%d\n", description.Width, description.Height);
    ok(description.Format == DirectXPixelFormat_B8G8R8A8UIntNormalized, "Format %d\n", description.Format);
    ok(description.MultisampleDescription.Count == 1 && description.MultisampleDescription.Quality == 0,
       "Multisample %d/%d\n", description.MultisampleDescription.Count, description.MultisampleDescription.Quality);
    hr = IDirect3DSurface_get_Description(winrt, NULL);
    ok(hr == E_INVALIDARG, "get_Description NULL: 0x%08lx\n", hr);

    out = NULL;
    hr = IDirect3DDxgiInterfaceAccess_GetInterface(access, &IID_ID3D11Texture2D, (void **)&out);
    ok(hr == S_OK, "GetInterface ID3D11Texture2D: 0x%08lx\n", hr);
    ok(out == (IUnknown *)texture, "ID3D11Texture2D %p, expected %p\n", out, texture);
    if (out)
        IUnknown_Release(out);
    out = NULL;
    hr = IDirect3DDxgiInterfaceAccess_GetInterface(access, &IID_IDXGISurface, (void **)&out);
    ok(hr == S_OK, "GetInterface IDXGISurface: 0x%08lx\n", hr);
    ok(out == (IUnknown *)surface, "IDXGISurface %p, expected %p\n", out, surface);
    if (out)
        IUnknown_Release(out);

    hr = IClosable_Close(closable);
    ok(hr == S_OK, "Close: 0x%08lx\n", hr);
    hr = IDirect3DSurface_get_Description(winrt, &description);
    ok(hr == RO_E_CLOSED, "get_Description after Close: 0x%08lx\n", hr);
    out = (IUnknown *)(ULONG_PTR)0xdeadbeef;
    hr = IDirect3DDxgiInterfaceAccess_GetInterface(access, &IID_IDXGISurface, (void **)&out);
    ok(hr == RO_E_CLOSED, "GetInterface after Close: 0x%08lx\n", hr);
    ok(out == (IUnknown *)(ULONG_PTR)0xdeadbeef, "GetInterface after Close set %p\n", out);

done:
    if (access)
        IDirect3DDxgiInterfaceAccess_Release(access);
    if (closable)
        IClosable_Release(closable);
    if (winrt)
        IDirect3DSurface_Release(winrt);
    if (object)
        ok(IInspectable_Release(object) == 0, "Object still referenced\n");
    TestWeakReleased(weak);
    IDXGISurface_Release(surface);
    ID3D11Texture2D_Release(texture);
}

START_TEST(winrt_interop)
{
    static const D3D_DRIVER_TYPE types[] = {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP};
    HMODULE module = GetModuleHandleW(L"d3d11.dll");
    ID3D11Device *device = NULL;
    HRESULT hr = E_FAIL;
    UINT i;

    pCreateDirect3D11DeviceFromDXGIDevice = (void *)GetProcAddress(module, "CreateDirect3D11DeviceFromDXGIDevice");
    pCreateDirect3D11SurfaceFromDXGISurface = (void *)GetProcAddress(module, "CreateDirect3D11SurfaceFromDXGISurface");
    ok(pCreateDirect3D11DeviceFromDXGIDevice != NULL, "CreateDirect3D11DeviceFromDXGIDevice is not exported\n");
    ok(pCreateDirect3D11SurfaceFromDXGISurface != NULL, "CreateDirect3D11SurfaceFromDXGISurface is not exported\n");

    for (i = 0; i < ARRAYSIZE(types) && FAILED(hr); i++)
        hr = D3D11CreateDevice(NULL, types[i], NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                               D3D11_SDK_VERSION, &device, NULL, NULL);
    if (FAILED(hr))
    {
        skip("No Direct3D 11 device: 0x%08lx\n", hr);
        return;
    }

    if (pCreateDirect3D11DeviceFromDXGIDevice)
        TestDevice(device);
    if (pCreateDirect3D11SurfaceFromDXGISurface)
        TestSurface(device);
    ID3D11Device_Release(device);
}
