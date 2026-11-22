/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Mapping default-usage buffers with CPU access on a hardware device
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <wine/test.h>

#define BYTES 256

static UINT CountWrong(const BYTE *Data, UINT First, UINT Count, BYTE Seed)
{
    UINT Index, Wrong = 0;

    for (Index = First; Index < First + Count; ++Index)
    {
        if (Data[Index] != (BYTE)(Index * 3 + Seed))
            ++Wrong;
    }
    return Wrong;
}

static void TestDefaultBuffer(ID3D11Device *Device, ID3D11DeviceContext *Context)
{
    D3D11_SUBRESOURCE_DATA Initial;
    D3D11_MAPPED_SUBRESOURCE Map;
    D3D11_BUFFER_DESC Desc;
    ID3D11Buffer *Buffer = NULL, *Staging = NULL;
    BYTE Data[BYTES];
    UINT Index;
    HRESULT hr;

    for (Index = 0; Index < BYTES; ++Index)
        Data[Index] = (BYTE)(Index * 3 + 1);
    Initial.pSysMem = Data;
    Initial.SysMemPitch = Initial.SysMemSlicePitch = 0;
    memset(&Desc, 0, sizeof(Desc));
    Desc.ByteWidth = BYTES;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
    hr = ID3D11Device_CreateBuffer(Device, &Desc, &Initial, &Buffer);
    ok(hr == S_OK && Buffer != NULL, "default buffer with CPU access: 0x%08lx\n", hr);
    if (Buffer == NULL)
        return;

    hr = ID3D11DeviceContext_Map(Context, (ID3D11Resource *)Buffer, 0, D3D11_MAP_READ, 0, &Map);
    ok(hr == S_OK, "map for reading: 0x%08lx\n", hr);
    if (hr == S_OK)
    {
        ok(CountWrong(Map.pData, 0, BYTES, 1) == 0, "the initial data does not read back\n");
        ID3D11DeviceContext_Unmap(Context, (ID3D11Resource *)Buffer, 0);
    }

    hr = ID3D11DeviceContext_Map(Context, (ID3D11Resource *)Buffer, 0, D3D11_MAP_WRITE, 0, &Map);
    ok(hr == S_OK, "map for writing: 0x%08lx\n", hr);
    if (hr == S_OK)
    {
        for (Index = 0; Index < BYTES / 2; ++Index)
            ((BYTE *)Map.pData)[Index] = (BYTE)(Index * 3 + 7);
        ID3D11DeviceContext_Unmap(Context, (ID3D11Resource *)Buffer, 0);
    }

    hr = ID3D11DeviceContext_Map(Context, (ID3D11Resource *)Buffer, 0, D3D11_MAP_READ, 0, &Map);
    ok(hr == S_OK, "map for reading after a write: 0x%08lx\n", hr);
    if (hr == S_OK)
    {
        ok(CountWrong(Map.pData, 0, BYTES / 2, 7) == 0, "the written half does not read back\n");
        ok(CountWrong(Map.pData, BYTES / 2, BYTES / 2, 1) == 0, "a write map lost the unwritten half\n");
        ID3D11DeviceContext_Unmap(Context, (ID3D11Resource *)Buffer, 0);
    }

    Desc.Usage = D3D11_USAGE_STAGING;
    Desc.BindFlags = 0;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = ID3D11Device_CreateBuffer(Device, &Desc, NULL, &Staging);
    ok(hr == S_OK && Staging != NULL, "staging buffer: 0x%08lx\n", hr);
    if (Staging != NULL)
    {
        ID3D11DeviceContext_CopyResource(Context, (ID3D11Resource *)Staging, (ID3D11Resource *)Buffer);
        hr = ID3D11DeviceContext_Map(Context, (ID3D11Resource *)Staging, 0, D3D11_MAP_READ, 0, &Map);
        ok(hr == S_OK, "map the GPU copy: 0x%08lx\n", hr);
        if (hr == S_OK)
        {
            ok(CountWrong(Map.pData, 0, BYTES / 2, 7) == 0, "the GPU does not see the CPU write\n");
            ID3D11DeviceContext_Unmap(Context, (ID3D11Resource *)Staging, 0);
        }
        ID3D11Buffer_Release(Staging);
    }
    ID3D11Buffer_Release(Buffer);
}

static void TestMultisampleTarget(ID3D11Device *Device)
{
    D3D11_TEXTURE2D_DESC Desc;
    ID3D11Texture2D *Texture = NULL;
    UINT Levels = 0;
    HRESULT hr;

    hr = ID3D11Device_CheckMultisampleQualityLevels(Device, DXGI_FORMAT_R8G8B8A8_UNORM, 4, &Levels);
    if (FAILED(hr) || Levels == 0)
    {
        skip("no 4-sample render targets: 0x%08lx\n", hr);
        return;
    }
    memset(&Desc, 0, sizeof(Desc));
    Desc.Width = 320;
    Desc.Height = 240;
    Desc.MipLevels = Desc.ArraySize = 1;
    Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.SampleDesc.Count = 4;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    hr = ID3D11Device_CreateTexture2D(Device, &Desc, NULL, &Texture);
    ok(hr == S_OK && Texture != NULL, "4-sample render target after a mapped default buffer: 0x%08lx\n", hr);
    if (Texture != NULL)
        ID3D11Texture2D_Release(Texture);
}

START_TEST(map_default)
{
    D3D11_FEATURE_DATA_D3D11_OPTIONS1 Options;
    ID3D11DeviceContext *Context = NULL;
    ID3D11Device *Device = NULL;
    HRESULT hr;

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &Device, NULL, &Context);
    if (FAILED(hr))
    {
        skip("no hardware device: 0x%08lx\n", hr);
        return;
    }
    memset(&Options, 0, sizeof(Options));
    hr = ID3D11Device_CheckFeatureSupport(Device, D3D11_FEATURE_D3D11_OPTIONS1, &Options, sizeof(Options));
    if (FAILED(hr) || !Options.MapOnDefaultBuffers)
        skip("default buffers are not mappable: 0x%08lx\n", hr);
    else
    {
        TestDefaultBuffer(Device, Context);
        TestMultisampleTarget(Device);
    }
    ID3D11DeviceContext_Release(Context);
    ID3D11Device_Release(Device);
}
