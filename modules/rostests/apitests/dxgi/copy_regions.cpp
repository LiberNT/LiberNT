/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Empty copy and update boxes are no-ops and never reach the driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#include <apitest.h>
#include <windows.h>
#include <d3d11.h>

typedef HRESULT (WINAPI *PFN_CREATE_DEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *,
                                            UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

static ID3D11Texture2D *
CreateTexture(ID3D11Device *Device, D3D11_USAGE Usage, UINT CpuAccess, UINT Bind, const DWORD *Data)
{
    D3D11_TEXTURE2D_DESC Desc = {4, 4, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, Usage, Bind, CpuAccess, 0};
    D3D11_SUBRESOURCE_DATA Init = {Data, 16, 0};
    ID3D11Texture2D *Texture = NULL;

    ok_hex(Device->CreateTexture2D(&Desc, Data ? &Init : NULL, &Texture), S_OK);
    return Texture;
}

static void
CheckUnchanged(ID3D11DeviceContext *Context, ID3D11Texture2D *Staging, ID3D11Texture2D *Dst, const char *Case)
{
    D3D11_MAPPED_SUBRESOURCE Map;
    UINT Row, Column;

    Context->CopyResource(Staging, Dst);
    ok_hex(Context->Map(Staging, 0, D3D11_MAP_READ, 0, &Map), S_OK);
    if (Map.pData == NULL)
        return;
    for (Row = 0; Row < 4; ++Row)
    {
        const DWORD *Line = (const DWORD *)((const BYTE *)Map.pData + Row * Map.RowPitch);

        for (Column = 0; Column < 4; ++Column)
            ok(Line[Column] == 0, "%s: texel %u,%u is %08lx\n", Case, Column, Row, Line[Column]);
    }
    Context->Unmap(Staging, 0);
}

START_TEST(copy_regions)
{
    HMODULE D3d11 = LoadLibraryW(L"d3d11.dll");
    PFN_CREATE_DEVICE Create = D3d11 ? (PFN_CREATE_DEVICE)GetProcAddress(D3d11, "D3D11CreateDevice") : NULL;
    D3D_FEATURE_LEVEL Level = D3D_FEATURE_LEVEL_10_0;
    D3D11_BOX Inverted = {2, 2, 0, 1, 1, 1}, Flat = {0, 0, 0, 4, 4, 0};
    ID3D11Device *Device = NULL;
    ID3D11DeviceContext *Context = NULL;
    ID3D11Texture2D *Src, *Dst, *Staging;
    DWORD Zero[16] = {0}, Pattern[16];
    UINT Index;

    if (Create == NULL || FAILED(Create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, &Level, 1, D3D11_SDK_VERSION,
                                        &Device, NULL, &Context)))
    {
        skip("No Direct3D 11 hardware device\n");
        return;
    }
    for (Index = 0; Index < 16; ++Index)
        Pattern[Index] = 0xff000000 | (Index + 1) * 0x010101;
    Src = CreateTexture(Device, D3D11_USAGE_DEFAULT, 0, D3D11_BIND_SHADER_RESOURCE, Pattern);
    Dst = CreateTexture(Device, D3D11_USAGE_DEFAULT, 0, D3D11_BIND_SHADER_RESOURCE, Zero);
    Staging = CreateTexture(Device, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ, 0, NULL);
    if (Src != NULL && Dst != NULL && Staging != NULL)
    {
        Context->CopySubresourceRegion(Dst, 0, 1, 0, 0, Src, 0, &Inverted);
        CheckUnchanged(Context, Staging, Dst, "inverted copy box");
        Context->CopySubresourceRegion(Dst, 0, 0, 0, 0, Src, 0, &Flat);
        CheckUnchanged(Context, Staging, Dst, "zero-depth copy box");
        Context->UpdateSubresource(Dst, 0, &Inverted, Pattern, 16, 0);
        CheckUnchanged(Context, Staging, Dst, "inverted update box");
        ok_hex(Device->GetDeviceRemovedReason(), S_OK);
    }
    if (Staging != NULL)
        Staging->Release();
    if (Dst != NULL)
        Dst->Release();
    if (Src != NULL)
        Src->Release();
    Context->Release();
    Device->Release();
}
