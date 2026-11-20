/*
 * PROJECT:     LiberNT Direct3D diagnostics
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Report the Direct3D 11 and 12 feature levels each adapter delivers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#define COBJMACROS
#define WIDL_C_INLINE_WRAPPERS
#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <initguid.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <d3d11on12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <d3dkmthk.h>
#include <d3d12umddi.h>
#include <reactos/drivers/directx/umd_adapter.h>
#include <stdio.h>
#include <stdarg.h>

static VOID
FeatPrint(
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...)
{
    CHAR Buffer[1024];
    va_list Arguments;

    va_start(Arguments, Format);
    _vsnprintf(Buffer, sizeof(Buffer) - 1, Format, Arguments);
    va_end(Arguments);
    Buffer[sizeof(Buffer) - 1] = '\0';
    OutputDebugStringA(Buffer);
    fputs(Buffer, stdout);
    fflush(stdout);
}

static const D3D_FEATURE_LEVEL FeatLevels[] =
{
    D3D_FEATURE_LEVEL_12_1,
    D3D_FEATURE_LEVEL_12_0,
    D3D_FEATURE_LEVEL_11_1,
    D3D_FEATURE_LEVEL_11_0,
    D3D_FEATURE_LEVEL_10_1,
    D3D_FEATURE_LEVEL_10_0,
};

static VOID
FeatDirect3D11Interfaces(
    _In_ ULONG Adapter,
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context)
{
    static const struct
    {
        const IID *Iid;
        PCSTR Name;
    } DeviceInterfaces[] =
    {
        { &IID_ID3D11Device1, "Device1" },
        { &IID_ID3D11Device2, "Device2" },
        { &IID_ID3D11Device3, "Device3" },
        { &IID_ID3D11Device4, "Device4" },
        { &IID_ID3D11Device5, "Device5" },
    }, ContextInterfaces[] =
    {
        { &IID_ID3D11DeviceContext1, "Context1" },
        { &IID_ID3D11DeviceContext2, "Context2" },
        { &IID_ID3D11DeviceContext3, "Context3" },
        { &IID_ID3D11DeviceContext4, "Context4" },
    };
    IUnknown *Unknown;
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(DeviceInterfaces); ++Index)
    {
        HRESULT Result = ID3D11Device_QueryInterface(Device, DeviceInterfaces[Index].Iid, (void **)&Unknown);
        FeatPrint("D3DFEAT_D3D11_INTERFACE adapter=%lu name=%s hr=0x%08lx\n",
                  Adapter, DeviceInterfaces[Index].Name, Result);
        if (SUCCEEDED(Result))
            IUnknown_Release(Unknown);
    }

    for (Index = 0; Index < RTL_NUMBER_OF(ContextInterfaces); ++Index)
    {
        HRESULT Result = ID3D11DeviceContext_QueryInterface(Context, ContextInterfaces[Index].Iid, (void **)&Unknown);
        FeatPrint("D3DFEAT_D3D11_INTERFACE adapter=%lu name=%s hr=0x%08lx\n",
                  Adapter, ContextInterfaces[Index].Name, Result);
        if (SUCCEEDED(Result))
            IUnknown_Release(Unknown);
    }
}

static VOID
FeatDirect3D11Options(
    _In_ ULONG Adapter,
    _In_ ID3D11Device *Device)
{
    D3D11_FEATURE_DATA_D3D11_OPTIONS Options;
    D3D11_FEATURE_DATA_D3D11_OPTIONS1 Options1;
    D3D11_FEATURE_DATA_D3D11_OPTIONS2 Options2;
    D3D11_FEATURE_DATA_D3D11_OPTIONS3 Options3;
    D3D11_FEATURE_DATA_D3D11_OPTIONS4 Options4;
    D3D11_FEATURE_DATA_D3D11_OPTIONS5 Options5;
    D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS Hardware;
    D3D11_FEATURE_DATA_DOUBLES Doubles;
    D3D11_FEATURE_DATA_THREADING Threading;
    D3D11_FEATURE_DATA_ARCHITECTURE_INFO Architecture;
    D3D11_FEATURE_DATA_SHADER_MIN_PRECISION_SUPPORT Precision;
    D3D11_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT Address;
    HRESULT Result;

    ZeroMemory(&Options, sizeof(Options));
    Result = ID3D11Device_CheckFeatureSupport(Device, D3D11_FEATURE_D3D11_OPTIONS, &Options, sizeof(Options));
    FeatPrint("D3DFEAT_D3D11_OPTIONS adapter=%lu hr=0x%08lx logic_op=%d uav_only_forced_samples=%d discard=%d "
              "update_copy_flags=%d clear_view=%d copy_overlap=%d cb_partial=%d cb_offset=%d map_nooverwrite_cb=%d "
              "map_nooverwrite_srv=%d msaa_forced_one=%d sad4=%d ext_doubles=%d ext_sharing=%d\n",
              Adapter, Result, Options.OutputMergerLogicOp, Options.UAVOnlyRenderingForcedSampleCount,
              Options.DiscardAPIsSeenByDriver, Options.FlagsForUpdateAndCopySeenByDriver, Options.ClearView,
              Options.CopyWithOverlap, Options.ConstantBufferPartialUpdate, Options.ConstantBufferOffsetting,
              Options.MapNoOverwriteOnDynamicConstantBuffer, Options.MapNoOverwriteOnDynamicBufferSRV,
              Options.MultisampleRTVWithForcedSampleCountOne, Options.SAD4ShaderInstructions,
              Options.ExtendedDoublesShaderInstructions, Options.ExtendedResourceSharing);

    ZeroMemory(&Options1, sizeof(Options1));
    Result = ID3D11Device_CheckFeatureSupport(Device, D3D11_FEATURE_D3D11_OPTIONS1, &Options1, sizeof(Options1));
    FeatPrint("D3DFEAT_D3D11_OPTIONS1 adapter=%lu hr=0x%08lx tiled_tier=%d minmax_filter=%d clear_view_depth=%d "
              "map_default_buffers=%d\n",
              Adapter, Result, Options1.TiledResourcesTier, Options1.MinMaxFiltering,
              Options1.ClearViewAlsoSupportsDepthOnlyFormats, Options1.MapOnDefaultBuffers);

    ZeroMemory(&Options2, sizeof(Options2));
    Result = ID3D11Device_CheckFeatureSupport(Device, D3D11_FEATURE_D3D11_OPTIONS2, &Options2, sizeof(Options2));
    FeatPrint("D3DFEAT_D3D11_OPTIONS2 adapter=%lu hr=0x%08lx ps_stencil_ref=%d typed_uav_load=%d rovs=%d "
              "conservative_tier=%d tiled_tier=%d map_default_textures=%d standard_swizzle=%d uma=%d\n",
              Adapter, Result, Options2.PSSpecifiedStencilRefSupported,
              Options2.TypedUAVLoadAdditionalFormats, Options2.ROVsSupported,
              Options2.ConservativeRasterizationTier, Options2.TiledResourcesTier,
              Options2.MapOnDefaultTextures, Options2.StandardSwizzle, Options2.UnifiedMemoryArchitecture);

    ZeroMemory(&Options3, sizeof(Options3));
    Result = ID3D11Device_CheckFeatureSupport(Device, D3D11_FEATURE_D3D11_OPTIONS3, &Options3, sizeof(Options3));
    FeatPrint("D3DFEAT_D3D11_OPTIONS3 adapter=%lu hr=0x%08lx vp_rt_index_any_shader=%d\n",
              Adapter, Result, Options3.VPAndRTArrayIndexFromAnyShaderFeedingRasterizer);

    ZeroMemory(&Options4, sizeof(Options4));
    Result = ID3D11Device_CheckFeatureSupport(Device, D3D11_FEATURE_D3D11_OPTIONS4, &Options4, sizeof(Options4));
    FeatPrint("D3DFEAT_D3D11_OPTIONS4 adapter=%lu hr=0x%08lx nv12_shared=%d\n",
              Adapter, Result, Options4.ExtendedNV12SharedTextureSupported);

    ZeroMemory(&Options5, sizeof(Options5));
    Result = ID3D11Device_CheckFeatureSupport(Device, D3D11_FEATURE_D3D11_OPTIONS5, &Options5, sizeof(Options5));
    FeatPrint("D3DFEAT_D3D11_OPTIONS5 adapter=%lu hr=0x%08lx shared_resource_tier=%d\n",
              Adapter, Result, Options5.SharedResourceTier);

    ZeroMemory(&Hardware, sizeof(Hardware));
    Result = ID3D11Device_CheckFeatureSupport(Device, D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS, &Hardware, sizeof(Hardware));
    ZeroMemory(&Doubles, sizeof(Doubles));
    ID3D11Device_CheckFeatureSupport(Device, D3D11_FEATURE_DOUBLES, &Doubles, sizeof(Doubles));
    ZeroMemory(&Threading, sizeof(Threading));
    ID3D11Device_CheckFeatureSupport(Device, D3D11_FEATURE_THREADING, &Threading, sizeof(Threading));
    ZeroMemory(&Architecture, sizeof(Architecture));
    ID3D11Device_CheckFeatureSupport(Device, D3D11_FEATURE_ARCHITECTURE_INFO, &Architecture, sizeof(Architecture));
    ZeroMemory(&Precision, sizeof(Precision));
    ID3D11Device_CheckFeatureSupport(Device, D3D11_FEATURE_SHADER_MIN_PRECISION_SUPPORT, &Precision, sizeof(Precision));
    ZeroMemory(&Address, sizeof(Address));
    ID3D11Device_CheckFeatureSupport(Device, D3D11_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT, &Address, sizeof(Address));
    FeatPrint("D3DFEAT_D3D11_CAPS adapter=%lu hr=0x%08lx cs_4x=%d doubles=%d concurrent_creates=%d command_lists=%d "
              "tbdr=%d ps_min_precision=0x%x other_min_precision=0x%x va_bits_resource=%u va_bits_process=%u\n",
              Adapter, Result, Hardware.ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x,
              Doubles.DoublePrecisionFloatShaderOps, Threading.DriverConcurrentCreates,
              Threading.DriverCommandLists, Architecture.TileBasedDeferredRenderer,
              Precision.PixelShaderMinPrecision, Precision.AllOtherShaderStagesMinPrecision,
              Address.MaxGPUVirtualAddressBitsPerResource, Address.MaxGPUVirtualAddressBitsPerProcess);
}

static const CHAR FeatVertexShader[] =
    "float4 main(float4 position : POSITION) : SV_POSITION { return position; }";
static const CHAR FeatPixelShader[] =
    "float4 main() : SV_TARGET { return float4(0.0, 1.0, 0.0, 1.0); }";

static DWORD
FeatReadPixel(
    _In_ ID3D11DeviceContext *Context,
    _In_ ID3D11Texture2D *Target,
    _In_ ID3D11Texture2D *Staging,
    _In_ UINT X,
    _In_ UINT Y)
{
    D3D11_MAPPED_SUBRESOURCE Mapped;
    DWORD Pixel = 0xdeadbeef;

    ID3D11DeviceContext_CopyResource(Context, (ID3D11Resource *)Staging, (ID3D11Resource *)Target);
    if (SUCCEEDED(ID3D11DeviceContext_Map(Context, (ID3D11Resource *)Staging, 0, D3D11_MAP_READ, 0, &Mapped)))
    {
        Pixel = *(const DWORD *)((const BYTE *)Mapped.pData + Y * Mapped.RowPitch + X * 4);
        ID3D11DeviceContext_Unmap(Context, (ID3D11Resource *)Staging, 0);
    }

    return Pixel;
}

static VOID
FeatDirect3D11Render(
    _In_ ULONG Adapter,
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context)
{
    static const FLOAT Vertices[] =
    {
        -1.0f, -1.0f, 0.0f, 1.0f,
        -1.0f,  3.0f, 0.0f, 1.0f,
         3.0f, -1.0f, 0.0f, 1.0f,
    };
    static const D3D11_INPUT_ELEMENT_DESC Layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    static const FLOAT Blue[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    ID3D11Texture2D *Target = NULL, *Staging = NULL;
    ID3D11RenderTargetView *View = NULL;
    ID3D11VertexShader *VertexShader = NULL;
    ID3D11PixelShader *PixelShader = NULL;
    ID3D11InputLayout *InputLayout = NULL;
    ID3D11Buffer *VertexBuffer = NULL;
    ID3DBlob *VertexCode = NULL, *PixelCode = NULL;
    D3D11_SUBRESOURCE_DATA Initial;
    D3D11_TEXTURE2D_DESC Texture;
    D3D11_BUFFER_DESC Buffer;
    D3D11_VIEWPORT Viewport;
    UINT Stride = 4 * sizeof(FLOAT), Offset = 0;
    DWORD Cleared, Drawn;
    HRESULT Result;

    ZeroMemory(&Texture, sizeof(Texture));
    Texture.Width = 64;
    Texture.Height = 64;
    Texture.MipLevels = 1;
    Texture.ArraySize = 1;
    Texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Texture.SampleDesc.Count = 1;
    Texture.Usage = D3D11_USAGE_DEFAULT;
    Texture.BindFlags = D3D11_BIND_RENDER_TARGET;
    Result = ID3D11Device_CreateTexture2D(Device, &Texture, NULL, &Target);
    if (FAILED(Result))
        goto Done;

    Texture.Usage = D3D11_USAGE_STAGING;
    Texture.BindFlags = 0;
    Texture.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Result = ID3D11Device_CreateTexture2D(Device, &Texture, NULL, &Staging);
    if (FAILED(Result))
        goto Done;

    Result = ID3D11Device_CreateRenderTargetView(Device, (ID3D11Resource *)Target, NULL, &View);
    if (FAILED(Result))
        goto Done;

    ID3D11DeviceContext_ClearRenderTargetView(Context, View, Blue);
    Cleared = FeatReadPixel(Context, Target, Staging, 32, 32);

    Result = D3DCompile(FeatVertexShader, sizeof(FeatVertexShader) - 1, NULL, NULL, NULL, "main", "vs_4_0", 0, 0,
                        &VertexCode, NULL);
    if (FAILED(Result))
        goto Done;

    Result = D3DCompile(FeatPixelShader, sizeof(FeatPixelShader) - 1, NULL, NULL, NULL, "main", "ps_4_0", 0, 0,
                        &PixelCode, NULL);
    if (FAILED(Result))
        goto Done;

    Result = ID3D11Device_CreateVertexShader(Device, ID3D10Blob_GetBufferPointer(VertexCode),
                                             ID3D10Blob_GetBufferSize(VertexCode), NULL, &VertexShader);
    if (FAILED(Result))
        goto Done;

    Result = ID3D11Device_CreatePixelShader(Device, ID3D10Blob_GetBufferPointer(PixelCode),
                                            ID3D10Blob_GetBufferSize(PixelCode), NULL, &PixelShader);
    if (FAILED(Result))
        goto Done;

    Result = ID3D11Device_CreateInputLayout(Device, Layout, RTL_NUMBER_OF(Layout),
                                            ID3D10Blob_GetBufferPointer(VertexCode),
                                            ID3D10Blob_GetBufferSize(VertexCode), &InputLayout);
    if (FAILED(Result))
        goto Done;

    ZeroMemory(&Buffer, sizeof(Buffer));
    Buffer.ByteWidth = sizeof(Vertices);
    Buffer.Usage = D3D11_USAGE_DEFAULT;
    Buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    ZeroMemory(&Initial, sizeof(Initial));
    Initial.pSysMem = Vertices;
    Result = ID3D11Device_CreateBuffer(Device, &Buffer, &Initial, &VertexBuffer);
    if (FAILED(Result))
        goto Done;

    Viewport.TopLeftX = 0.0f;
    Viewport.TopLeftY = 0.0f;
    Viewport.Width = 64.0f;
    Viewport.Height = 64.0f;
    Viewport.MinDepth = 0.0f;
    Viewport.MaxDepth = 1.0f;
    ID3D11DeviceContext_OMSetRenderTargets(Context, 1, &View, NULL);
    ID3D11DeviceContext_RSSetViewports(Context, 1, &Viewport);
    ID3D11DeviceContext_IASetInputLayout(Context, InputLayout);
    ID3D11DeviceContext_IASetPrimitiveTopology(Context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_IASetVertexBuffers(Context, 0, 1, &VertexBuffer, &Stride, &Offset);
    ID3D11DeviceContext_VSSetShader(Context, VertexShader, NULL, 0);
    ID3D11DeviceContext_PSSetShader(Context, PixelShader, NULL, 0);
    ID3D11DeviceContext_Draw(Context, 3, 0);
    Drawn = FeatReadPixel(Context, Target, Staging, 32, 32);
    FeatPrint("D3DFEAT_D3D11_RENDER adapter=%lu cleared=0x%08lx drawn=0x%08lx result=%s\n",
              Adapter, Cleared, Drawn,
              (Cleared == 0xffff0000 && Drawn == 0xff00ff00) ? "pass" : "fail");
    Result = S_OK;

Done:
    if (FAILED(Result))
        FeatPrint("D3DFEAT_D3D11_RENDER adapter=%lu hr=0x%08lx result=fail\n", Adapter, Result);
    if (VertexBuffer != NULL) ID3D11Buffer_Release(VertexBuffer);
    if (InputLayout != NULL) ID3D11InputLayout_Release(InputLayout);
    if (PixelShader != NULL) ID3D11PixelShader_Release(PixelShader);
    if (VertexShader != NULL) ID3D11VertexShader_Release(VertexShader);
    if (PixelCode != NULL) ID3D10Blob_Release(PixelCode);
    if (VertexCode != NULL) ID3D10Blob_Release(VertexCode);
    if (View != NULL) ID3D11RenderTargetView_Release(View);
    if (Staging != NULL) ID3D11Texture2D_Release(Staging);
    if (Target != NULL) ID3D11Texture2D_Release(Target);
}

static VOID
FeatDirect3D11Tiled(
    _In_ ULONG Adapter,
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context)
{
    ID3D11Texture2D *Tiled = NULL, *Staging = NULL, *Pattern = NULL;
    ID3D11DeviceContext2 *Context2 = NULL;
    ID3D11Device2 *Device2 = NULL;
    ID3D11Buffer *Pool = NULL;
    D3D11_TILED_RESOURCE_COORDINATE Coordinate;
    D3D11_SUBRESOURCE_DATA Initial;
    D3D11_SUBRESOURCE_TILING Tiling;
    D3D11_PACKED_MIP_DESC Packed;
    D3D11_TILE_REGION_SIZE Region;
    D3D11_TEXTURE2D_DESC Texture;
    D3D11_BUFFER_DESC Buffer;
    D3D11_TILE_SHAPE Shape;
    UINT Tiles = 0, TilingCount = 1, Offset = 0, Count, RangeFlags, X, Y;
    DWORD Mapped = 0, First = 0, Unmapped = 0, Kept = 0, Remapped = 0;
    DWORD *Pixels = NULL;
    HRESULT Result;

    ZeroMemory(&Shape, sizeof(Shape));
    ZeroMemory(&Tiling, sizeof(Tiling));
    ZeroMemory(&Packed, sizeof(Packed));
    Result = ID3D11Device_QueryInterface(Device, &IID_ID3D11Device2, (void **)&Device2);
    if (FAILED(Result))
        goto Done;

    Result = ID3D11DeviceContext_QueryInterface(Context, &IID_ID3D11DeviceContext2, (void **)&Context2);
    if (FAILED(Result))
        goto Done;

    ZeroMemory(&Buffer, sizeof(Buffer));
    Buffer.ByteWidth = 4 * D3D11_2_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
    Buffer.Usage = D3D11_USAGE_DEFAULT;
    Buffer.MiscFlags = D3D11_RESOURCE_MISC_TILE_POOL;
    Result = ID3D11Device_CreateBuffer(Device, &Buffer, NULL, &Pool);
    if (FAILED(Result))
        goto Done;

    ZeroMemory(&Texture, sizeof(Texture));
    Texture.Width = 256;
    Texture.Height = 256;
    Texture.MipLevels = 1;
    Texture.ArraySize = 1;
    Texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Texture.SampleDesc.Count = 1;
    Texture.Usage = D3D11_USAGE_DEFAULT;
    Texture.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    Texture.MiscFlags = D3D11_RESOURCE_MISC_TILED;
    Result = ID3D11Device_CreateTexture2D(Device, &Texture, NULL, &Tiled);
    if (FAILED(Result))
        goto Done;

    Pixels = HeapAlloc(GetProcessHeap(), 0, 256 * 256 * sizeof(DWORD));
    if (Pixels == NULL)
    {
        Result = E_OUTOFMEMORY;
        goto Done;
    }
    for (Y = 0; Y < 256; ++Y)
        for (X = 0; X < 256; ++X)
            Pixels[Y * 256 + X] = 0xff800000 | (Y << 8) | X;
    Initial.pSysMem = Pixels;
    Initial.SysMemPitch = 256 * sizeof(DWORD);
    Initial.SysMemSlicePitch = 0;
    Texture.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    Texture.MiscFlags = 0;
    Result = ID3D11Device_CreateTexture2D(Device, &Texture, &Initial, &Pattern);
    if (FAILED(Result))
        goto Done;

    Texture.Usage = D3D11_USAGE_STAGING;
    Texture.BindFlags = 0;
    Texture.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Result = ID3D11Device_CreateTexture2D(Device, &Texture, NULL, &Staging);
    if (FAILED(Result))
        goto Done;

    ID3D11Device2_GetResourceTiling(Device2, (ID3D11Resource *)Tiled, &Tiles, &Packed, &Shape, &TilingCount, 0,
                                    &Tiling);
    Count = Tiles;
    Result = ID3D11DeviceContext2_UpdateTileMappings(Context2, (ID3D11Resource *)Tiled, 1, NULL, NULL, Pool, 1,
                                                     NULL, &Offset, &Count, 0);
    if (FAILED(Result))
        goto Done;

    ID3D11DeviceContext_CopyResource(Context, (ID3D11Resource *)Tiled, (ID3D11Resource *)Pattern);
    Mapped = FeatReadPixel(Context, Tiled, Staging, 200, 200);
    First = FeatReadPixel(Context, Tiled, Staging, 20, 20);

    ZeroMemory(&Coordinate, sizeof(Coordinate));
    Coordinate.X = 1;
    Coordinate.Y = 1;
    ZeroMemory(&Region, sizeof(Region));
    Region.NumTiles = 1;
    RangeFlags = D3D11_TILE_RANGE_NULL;
    Count = 1;
    Result = ID3D11DeviceContext2_UpdateTileMappings(Context2, (ID3D11Resource *)Tiled, 1, &Coordinate, &Region,
                                                     NULL, 1, &RangeFlags, NULL, &Count, 0);
    if (FAILED(Result))
        goto Done;

    Unmapped = FeatReadPixel(Context, Tiled, Staging, 200, 200);
    Kept = FeatReadPixel(Context, Tiled, Staging, 20, 20);

    RangeFlags = 0;
    Offset = 3;
    Result = ID3D11DeviceContext2_UpdateTileMappings(Context2, (ID3D11Resource *)Tiled, 1, &Coordinate, &Region,
                                                     Pool, 1, &RangeFlags, &Offset, &Count, 0);
    if (FAILED(Result))
        goto Done;

    Remapped = FeatReadPixel(Context, Tiled, Staging, 200, 200);

Done:
    FeatPrint("D3DFEAT_D3D11_TILED adapter=%lu hr=0x%08lx tiles=%u shape=%ux%ux%u tiling=%ux%u packed=%u "
              "mapped=0x%08lx first=0x%08lx unmapped=0x%08lx kept=0x%08lx remapped=0x%08lx result=%s\n",
              Adapter, Result, Tiles, Shape.WidthInTexels, Shape.HeightInTexels, Shape.DepthInTexels,
              Tiling.WidthInTiles, Tiling.HeightInTiles, Packed.NumPackedMips, Mapped, First, Unmapped, Kept,
              Remapped,
              (SUCCEEDED(Result) && Tiles == 4 && Mapped == 0xff80c8c8 && First == 0xff801414 &&
               Unmapped == 0 && Kept == 0xff801414 && Remapped == 0xff80c8c8) ? "pass" : "fail");
    if (Pixels != NULL) HeapFree(GetProcessHeap(), 0, Pixels);
    if (Staging != NULL) ID3D11Texture2D_Release(Staging);
    if (Pattern != NULL) ID3D11Texture2D_Release(Pattern);
    if (Tiled != NULL) ID3D11Texture2D_Release(Tiled);
    if (Pool != NULL) ID3D11Buffer_Release(Pool);
    if (Context2 != NULL) ID3D11DeviceContext2_Release(Context2);
    if (Device2 != NULL) ID3D11Device2_Release(Device2);
}

static VOID
FeatDirect3D11Modern(
    _In_ ULONG Adapter,
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context)
{
    ID3D11RenderTargetView1 *View1 = NULL;
    ID3D11RasterizerState2 *Rasterizer = NULL;
    ID3D11DeviceContext4 *Context4 = NULL;
    ID3D11Texture2D1 *Texture1 = NULL;
    ID3D11Device5 *Device5 = NULL;
    ID3D11Query1 *Query = NULL;
    ID3D11Fence *Fence = NULL;
    D3D11_RENDER_TARGET_VIEW_DESC1 ViewDesc;
    D3D11_RASTERIZER_DESC2 RasterizerDesc;
    D3D11_TEXTURE2D_DESC1 Texture, Returned;
    D3D11_QUERY_DESC1 QueryDesc;
    HRESULT TextureResult, ViewResult = E_FAIL, RasterizerResult, QueryResult, FenceResult, SignalResult = E_FAIL;
    HRESULT RemovedResult;
    UINT64 Completed = 0;
    DWORD Cookie = 0, Wait = WAIT_FAILED;
    HANDLE Event;

    if (FAILED(ID3D11Device_QueryInterface(Device, &IID_ID3D11Device5, (void **)&Device5)) ||
        FAILED(ID3D11DeviceContext_QueryInterface(Context, &IID_ID3D11DeviceContext4, (void **)&Context4)))
    {
        FeatPrint("D3DFEAT_D3D11_MODERN adapter=%lu result=missing\n", Adapter);
        goto Done;
    }

    ZeroMemory(&Texture, sizeof(Texture));
    ZeroMemory(&Returned, sizeof(Returned));
    ZeroMemory(&ViewDesc, sizeof(ViewDesc));
    Texture.Width = 64;
    Texture.Height = 32;
    Texture.MipLevels = 1;
    Texture.ArraySize = 1;
    Texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Texture.SampleDesc.Count = 1;
    Texture.Usage = D3D11_USAGE_DEFAULT;
    Texture.BindFlags = D3D11_BIND_RENDER_TARGET;
    TextureResult = ID3D11Device5_CreateTexture2D1(Device5, &Texture, NULL, &Texture1);
    if (SUCCEEDED(TextureResult))
    {
        ID3D11Texture2D1_GetDesc1(Texture1, &Returned);
        ViewResult = ID3D11Device5_CreateRenderTargetView1(Device5, (ID3D11Resource *)Texture1, NULL, &View1);
        if (SUCCEEDED(ViewResult))
            ID3D11RenderTargetView1_GetDesc1(View1, &ViewDesc);
    }

    ZeroMemory(&RasterizerDesc, sizeof(RasterizerDesc));
    RasterizerDesc.FillMode = D3D11_FILL_SOLID;
    RasterizerDesc.CullMode = D3D11_CULL_BACK;
    RasterizerDesc.DepthClipEnable = TRUE;
    RasterizerDesc.ConservativeRaster = D3D11_CONSERVATIVE_RASTERIZATION_MODE_ON;
    RasterizerResult = ID3D11Device5_CreateRasterizerState2(Device5, &RasterizerDesc, &Rasterizer);
    if (SUCCEEDED(RasterizerResult))
    {
        ZeroMemory(&RasterizerDesc, sizeof(RasterizerDesc));
        ID3D11RasterizerState2_GetDesc2(Rasterizer, &RasterizerDesc);
    }

    QueryDesc.Query = D3D11_QUERY_EVENT;
    QueryDesc.MiscFlags = 0;
    QueryDesc.ContextType = D3D11_CONTEXT_TYPE_ALL;
    QueryResult = ID3D11Device5_CreateQuery1(Device5, &QueryDesc, &Query);
    FeatPrint("D3DFEAT_D3D11_DESC1 adapter=%lu texture_hr=0x%08lx size=%ux%u layout=%d view_hr=0x%08lx "
              "view_dimension=%d rasterizer_hr=0x%08lx conservative=%d query_hr=0x%08lx\n",
              Adapter, TextureResult, Returned.Width, Returned.Height, Returned.TextureLayout, ViewResult,
              ViewDesc.ViewDimension, RasterizerResult, RasterizerDesc.ConservativeRaster, QueryResult);

    Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    FenceResult = ID3D11Device5_CreateFence(Device5, 0, D3D11_FENCE_FLAG_NONE, &IID_ID3D11Fence, (void **)&Fence);
    if (SUCCEEDED(FenceResult))
    {
        SignalResult = ID3D11DeviceContext4_Signal(Context4, Fence, 5);
        if (SUCCEEDED(SignalResult) && SUCCEEDED(ID3D11Fence_SetEventOnCompletion(Fence, 5, Event)))
            Wait = WaitForSingleObject(Event, 3000);
        Completed = ID3D11Fence_GetCompletedValue(Fence);
    }
    FeatPrint("D3DFEAT_D3D11_FENCE adapter=%lu create_hr=0x%08lx signal_hr=0x%08lx wait=%lu completed=%I64u "
              "result=%s\n",
              Adapter, FenceResult, SignalResult, Wait, Completed,
              (SUCCEEDED(FenceResult) && SUCCEEDED(SignalResult) && Wait == WAIT_OBJECT_0 && Completed == 5) ?
              "pass" : "fail");

    ResetEvent(Event);
    RemovedResult = ID3D11Device5_RegisterDeviceRemovedEvent(Device5, Event, &Cookie);
    FeatPrint("D3DFEAT_D3D11_REMOVED adapter=%lu hr=0x%08lx cookie=%lu signaled=%d\n",
              Adapter, RemovedResult, Cookie, WaitForSingleObject(Event, 0) == WAIT_OBJECT_0);
    if (SUCCEEDED(RemovedResult))
        ID3D11Device5_UnregisterDeviceRemoved(Device5, Cookie);
    CloseHandle(Event);

Done:
    if (Fence != NULL) ID3D11Fence_Release(Fence);
    if (Query != NULL) ID3D11Query1_Release(Query);
    if (Rasterizer != NULL) ID3D11RasterizerState2_Release(Rasterizer);
    if (View1 != NULL) ID3D11RenderTargetView1_Release(View1);
    if (Texture1 != NULL) ID3D11Texture2D1_Release(Texture1);
    if (Context4 != NULL) ID3D11DeviceContext4_Release(Context4);
    if (Device5 != NULL) ID3D11Device5_Release(Device5);
}

static VOID
FeatDirect3D11SharedNt(
    _In_ ULONG AdapterIndex,
    _In_ IDXGIAdapter1 *Adapter,
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context)
{
    static const FLOAT Blue[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    ID3D11Texture2D *Texture = NULL, *Opened = NULL, *Staging = NULL;
    ID3D11RenderTargetView *View = NULL;
    ID3D11Query *Query = NULL;
    IDXGIResource1 *Resource = NULL;
    ID3D11Device *Second = NULL;
    ID3D11Device1 *Second1 = NULL;
    ID3D11DeviceContext *SecondContext = NULL;
    D3D11_TEXTURE2D_DESC Desc;
    D3D11_QUERY_DESC QueryDesc;
    HANDLE Shared = NULL, Legacy = NULL;
    HRESULT CreateResult, ShareResult = E_FAIL, LegacyResult = E_FAIL, OpenResult = E_FAIL;
    DWORD Pixel = 0;
    BOOL Done = FALSE;
    ULONG Spin;

    ZeroMemory(&Desc, sizeof(Desc));
    Desc.Width = 64;
    Desc.Height = 64;
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    Desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    CreateResult = ID3D11Device_CreateTexture2D(Device, &Desc, NULL, &Texture);
    if (FAILED(CreateResult))
        goto Done;

    if (SUCCEEDED(ID3D11Device_CreateRenderTargetView(Device, (ID3D11Resource *)Texture, NULL, &View)))
        ID3D11DeviceContext_ClearRenderTargetView(Context, View, Blue);
    ZeroMemory(&QueryDesc, sizeof(QueryDesc));
    QueryDesc.Query = D3D11_QUERY_EVENT;
    if (SUCCEEDED(ID3D11Device_CreateQuery(Device, &QueryDesc, &Query)))
    {
        ID3D11DeviceContext_End(Context, (ID3D11Asynchronous *)Query);
        ID3D11DeviceContext_Flush(Context);
        for (Spin = 0; Spin < 5000 && !Done; ++Spin)
        {
            if (ID3D11DeviceContext_GetData(Context, (ID3D11Asynchronous *)Query, &Done, sizeof(Done), 0) != S_OK)
                Sleep(1);
        }
    }

    ShareResult = ID3D11Texture2D_QueryInterface(Texture, &IID_IDXGIResource1, (void **)&Resource);
    if (SUCCEEDED(ShareResult))
    {
        LegacyResult = IDXGIResource1_GetSharedHandle(Resource, &Legacy);
        ShareResult = IDXGIResource1_CreateSharedHandle(Resource, NULL,
                                                        DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                                        NULL, &Shared);
    }
    if (FAILED(ShareResult))
        goto Done;

    OpenResult = D3D11CreateDevice((IDXGIAdapter *)Adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, NULL, 0,
                                   D3D11_SDK_VERSION, &Second, NULL, &SecondContext);
    if (SUCCEEDED(OpenResult))
        OpenResult = ID3D11Device_QueryInterface(Second, &IID_ID3D11Device1, (void **)&Second1);
    if (SUCCEEDED(OpenResult))
        OpenResult = ID3D11Device1_OpenSharedResource1(Second1, Shared, &IID_ID3D11Texture2D, (void **)&Opened);
    if (FAILED(OpenResult))
        goto Done;

    Desc.Usage = D3D11_USAGE_STAGING;
    Desc.BindFlags = 0;
    Desc.MiscFlags = 0;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (SUCCEEDED(ID3D11Device_CreateTexture2D(Second, &Desc, NULL, &Staging)))
        Pixel = FeatReadPixel(SecondContext, Opened, Staging, 10, 10);

Done:
    FeatPrint("D3DFEAT_D3D11_SHARED_NT adapter=%lu create=0x%08lx share=0x%08lx legacy=0x%08lx open=0x%08lx "
              "pixel=0x%08lx result=%s\n",
              AdapterIndex, CreateResult, ShareResult, LegacyResult, OpenResult, Pixel,
              (SUCCEEDED(CreateResult) && SUCCEEDED(ShareResult) && LegacyResult == E_INVALIDARG &&
               SUCCEEDED(OpenResult) && Pixel == 0xff0000ff) ? "pass" : "fail");
    if (Staging != NULL) ID3D11Texture2D_Release(Staging);
    if (Opened != NULL) ID3D11Texture2D_Release(Opened);
    if (SecondContext != NULL) ID3D11DeviceContext_Release(SecondContext);
    if (Second1 != NULL) ID3D11Device1_Release(Second1);
    if (Second != NULL) ID3D11Device_Release(Second);
    if (Shared != NULL) CloseHandle(Shared);
    if (Resource != NULL) IDXGIResource1_Release(Resource);
    if (Query != NULL) ID3D11Query_Release(Query);
    if (View != NULL) ID3D11RenderTargetView_Release(View);
    if (Texture != NULL) ID3D11Texture2D_Release(Texture);
}

static VOID
FeatDirect3D11(
    _In_ ULONG AdapterIndex,
    _In_ IDXGIAdapter1 *Adapter)
{
    ID3D11DeviceContext *Context = NULL;
    ID3D11Device *Device = NULL;
    D3D_FEATURE_LEVEL Level = 0;
    HRESULT Result = E_FAIL;
    ULONG First;

    /* A runtime that does not know the newest levels rejects the whole list
     * with E_INVALIDARG, so applications retry with a shorter one. */
    for (First = 0; First < RTL_NUMBER_OF(FeatLevels); ++First)
    {
        Result = D3D11CreateDevice((IDXGIAdapter *)Adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0,
                                   &FeatLevels[First], RTL_NUMBER_OF(FeatLevels) - First,
                                   D3D11_SDK_VERSION, &Device, &Level, &Context);
        FeatPrint("D3DFEAT_D3D11_CREATE adapter=%lu first_level=0x%04x hr=0x%08lx level=0x%04x\n",
                  AdapterIndex, FeatLevels[First], Result, Level);
        if (SUCCEEDED(Result))
            break;
    }

    if (FAILED(Result))
        return;

    FeatPrint("D3DFEAT_D3D11_DEVICE adapter=%lu level=0x%04x device_level=0x%04x\n",
              AdapterIndex, Level, ID3D11Device_GetFeatureLevel(Device));
    FeatDirect3D11Interfaces(AdapterIndex, Device, Context);
    FeatDirect3D11Options(AdapterIndex, Device);
    FeatDirect3D11Render(AdapterIndex, Device, Context);
    FeatDirect3D11Modern(AdapterIndex, Device, Context);
    FeatDirect3D11Tiled(AdapterIndex, Device, Context);
    FeatDirect3D11SharedNt(AdapterIndex, Adapter, Device, Context);
    ID3D11DeviceContext_Release(Context);
    ID3D11Device_Release(Device);
}

static HRESULT
FeatExecute12(
    _In_ ID3D12CommandQueue *Queue,
    _In_ ID3D12GraphicsCommandList *List,
    _In_ ID3D12Fence *Fence,
    _In_ HANDLE Event,
    _Inout_ UINT64 *Value)
{
    ID3D12CommandList *Lists[1];
    HRESULT Result;

    Result = ID3D12GraphicsCommandList_Close(List);
    if (FAILED(Result))
        return Result;

    Lists[0] = (ID3D12CommandList *)List;
    ID3D12CommandQueue_ExecuteCommandLists(Queue, 1, Lists);
    ++*Value;
    Result = ID3D12CommandQueue_Signal(Queue, Fence, *Value);
    if (FAILED(Result))
        return Result;

    if (ID3D12Fence_GetCompletedValue(Fence) < *Value)
    {
        Result = ID3D12Fence_SetEventOnCompletion(Fence, *Value, Event);
        if (FAILED(Result))
            return Result;

        if (WaitForSingleObject(Event, 5000) != WAIT_OBJECT_0)
        {
            FeatPrint("D3DFEAT_D3D12_FENCE expected=%I64u completed=%I64u\n",
                      *Value, ID3D12Fence_GetCompletedValue(Fence));
            return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        }
    }

    return S_OK;
}

static const CHAR FeatScenePixelShader[] =
    "Texture2D image : register(t0);\n"
    "SamplerState point_sampler : register(s0);\n"
    "cbuffer constants : register(b0) { float4 tint; };\n"
    "float4 main(float4 position : SV_POSITION) : SV_TARGET\n"
    "{ return image.Sample(point_sampler, float2(0.5, 0.5)) * tint; }";

static VOID
FeatDirect3D12Scene(
    _In_ ULONG Adapter,
    _In_ ID3D12Device *Device,
    _In_ ID3D12CommandQueue *Queue,
    _In_ ID3D12CommandAllocator *Allocator,
    _In_ ID3D12GraphicsCommandList *List,
    _In_ ID3D12Fence *Fence,
    _In_ HANDLE Event,
    _Inout_ UINT64 *FenceValue,
    _In_ const D3D12_VERTEX_BUFFER_VIEW *VertexView,
    _In_ D3D12_CPU_DESCRIPTOR_HANDLE View,
    _In_ ID3D12Resource *Target,
    _In_ ID3D12Resource *Readback,
    _In_ const D3D12_PLACED_SUBRESOURCE_FOOTPRINT *Footprint)
{
    static const D3D12_INPUT_ELEMENT_DESC Layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    static const FLOAT Tint[4] = { 0.5f, 1.0f, 1.0f, 1.0f };
    static const FLOAT Black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    ID3D12Resource *Texture = NULL, *Upload = NULL, *Constants = NULL, *Depth = NULL;
    ID3D12DescriptorHeap *Views = NULL, *Samplers = NULL, *DepthViews = NULL;
    ID3D12DescriptorHeap *Heaps[2];
    ID3D12RootSignature *RootSignature = NULL;
    ID3D12PipelineState *Pipeline = NULL;
    ID3DBlob *VertexCode = NULL, *PixelCode = NULL, *SignatureBlob = NULL;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC PipelineDesc;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT TextureFootprint;
    D3D12_TEXTURE_COPY_LOCATION Source, Destination;
    D3D12_DESCRIPTOR_RANGE Ranges[3];
    D3D12_ROOT_PARAMETER Parameters[2];
    D3D12_ROOT_SIGNATURE_DESC SignatureDesc;
    D3D12_CONSTANT_BUFFER_VIEW_DESC ConstantView;
    D3D12_DESCRIPTOR_HEAP_DESC HeapDesc;
    D3D12_HEAP_PROPERTIES HeapProperties;
    D3D12_CPU_DESCRIPTOR_HANDLE Handle, DepthHandle;
    D3D12_GPU_DESCRIPTOR_HANDLE Table;
    D3D12_SAMPLER_DESC Sampler;
    D3D12_CLEAR_VALUE DepthClear;
    D3D12_RESOURCE_BARRIER Barrier;
    D3D12_RESOURCE_DESC Desc;
    D3D12_VIEWPORT Viewport;
    D3D12_RECT Scissor;
    UINT64 Total = 0;
    UINT Row, Column, Increment;
    DWORD Pixel = 0xdeadbeef;
    PCSTR Step = "texture";
    HRESULT Result;
    BYTE *Bytes;
    PVOID Data;

    ZeroMemory(&HeapProperties, sizeof(HeapProperties));
    HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    ZeroMemory(&Desc, sizeof(Desc));
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    Desc.Width = 4;
    Desc.Height = 4;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Result = ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &Desc,
                                                  D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource,
                                                  (void **)&Texture);
    if (FAILED(Result))
        goto Done;
    ID3D12Device_GetCopyableFootprints(Device, &Desc, 0, 1, 0, &TextureFootprint, NULL, NULL, &Total);

    Step = "depth";
    Desc.Width = 64;
    Desc.Height = 64;
    Desc.Format = DXGI_FORMAT_D32_FLOAT;
    Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    ZeroMemory(&DepthClear, sizeof(DepthClear));
    DepthClear.Format = DXGI_FORMAT_D32_FLOAT;
    DepthClear.DepthStencil.Depth = 1.0f;
    Result = ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &Desc,
                                                  D3D12_RESOURCE_STATE_DEPTH_WRITE, &DepthClear,
                                                  &IID_ID3D12Resource, (void **)&Depth);
    if (FAILED(Result))
        goto Done;

    Step = "upload";
    HeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
    ZeroMemory(&Desc, sizeof(Desc));
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    Desc.Width = Total;
    Desc.Height = 1;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.SampleDesc.Count = 1;
    Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Result = ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &Desc,
                                                  D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource,
                                                  (void **)&Upload);
    if (SUCCEEDED(Result))
        Result = ID3D12Resource_Map(Upload, 0, NULL, &Data);
    if (FAILED(Result))
        goto Done;
    for (Row = 0; Row < 4; Row++)
    {
        Bytes = (BYTE *)Data + TextureFootprint.Offset + Row * TextureFootprint.Footprint.RowPitch;
        for (Column = 0; Column < 4; Column++)
        {
            Bytes[Column * 4 + 0] = 0xff;
            Bytes[Column * 4 + 1] = 0x80;
            Bytes[Column * 4 + 2] = 0x40;
            Bytes[Column * 4 + 3] = 0xff;
        }
    }
    ID3D12Resource_Unmap(Upload, 0, NULL);

    Desc.Width = 256;
    Result = ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &Desc,
                                                  D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource,
                                                  (void **)&Constants);
    if (SUCCEEDED(Result))
        Result = ID3D12Resource_Map(Constants, 0, NULL, &Data);
    if (FAILED(Result))
        goto Done;
    CopyMemory(Data, Tint, sizeof(Tint));
    ID3D12Resource_Unmap(Constants, 0, NULL);

    Step = "descriptor heaps";
    ZeroMemory(&HeapDesc, sizeof(HeapDesc));
    HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    HeapDesc.NumDescriptors = 2;
    HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    Result = ID3D12Device_CreateDescriptorHeap(Device, &HeapDesc, &IID_ID3D12DescriptorHeap, (void **)&Views);
    if (FAILED(Result))
        goto Done;
    HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    HeapDesc.NumDescriptors = 1;
    Result = ID3D12Device_CreateDescriptorHeap(Device, &HeapDesc, &IID_ID3D12DescriptorHeap, (void **)&Samplers);
    if (FAILED(Result))
        goto Done;
    HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    Result = ID3D12Device_CreateDescriptorHeap(Device, &HeapDesc, &IID_ID3D12DescriptorHeap, (void **)&DepthViews);
    if (FAILED(Result))
        goto Done;

    Increment = ID3D12Device_GetDescriptorHandleIncrementSize(Device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    Handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(Views);
    ID3D12Device_CreateShaderResourceView(Device, Texture, NULL, Handle);
    Handle.ptr += Increment;
    ConstantView.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(Constants);
    ConstantView.SizeInBytes = 256;
    ID3D12Device_CreateConstantBufferView(Device, &ConstantView, Handle);
    ZeroMemory(&Sampler, sizeof(Sampler));
    Sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    Sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    Sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    Sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    Sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    Sampler.MaxLOD = D3D12_FLOAT32_MAX;
    ID3D12Device_CreateSampler(Device, &Sampler, ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(Samplers));
    DepthHandle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(DepthViews);
    ID3D12Device_CreateDepthStencilView(Device, Depth, NULL, DepthHandle);

    Step = "root signature";
    ZeroMemory(Ranges, sizeof(Ranges));
    Ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    Ranges[0].NumDescriptors = 1;
    Ranges[0].OffsetInDescriptorsFromTableStart = 0;
    Ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    Ranges[1].NumDescriptors = 1;
    Ranges[1].OffsetInDescriptorsFromTableStart = 1;
    Ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    Ranges[2].NumDescriptors = 1;
    ZeroMemory(Parameters, sizeof(Parameters));
    Parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    Parameters[0].DescriptorTable.NumDescriptorRanges = 2;
    Parameters[0].DescriptorTable.pDescriptorRanges = &Ranges[0];
    Parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    Parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    Parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    Parameters[1].DescriptorTable.pDescriptorRanges = &Ranges[2];
    Parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    ZeroMemory(&SignatureDesc, sizeof(SignatureDesc));
    SignatureDesc.NumParameters = 2;
    SignatureDesc.pParameters = Parameters;
    SignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    Result = D3D12SerializeRootSignature(&SignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &SignatureBlob, NULL);
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateRootSignature(Device, 0, ID3D10Blob_GetBufferPointer(SignatureBlob),
                                                  ID3D10Blob_GetBufferSize(SignatureBlob),
                                                  &IID_ID3D12RootSignature, (void **)&RootSignature);
    if (FAILED(Result))
        goto Done;

    Step = "pipeline state";
    Result = D3DCompile(FeatVertexShader, sizeof(FeatVertexShader) - 1, NULL, NULL, NULL, "main", "vs_5_0", 0, 0,
                        &VertexCode, NULL);
    if (SUCCEEDED(Result))
        Result = D3DCompile(FeatScenePixelShader, sizeof(FeatScenePixelShader) - 1, NULL, NULL, NULL, "main",
                            "ps_5_0", 0, 0, &PixelCode, NULL);
    if (FAILED(Result))
        goto Done;
    ZeroMemory(&PipelineDesc, sizeof(PipelineDesc));
    PipelineDesc.pRootSignature = RootSignature;
    PipelineDesc.VS.pShaderBytecode = ID3D10Blob_GetBufferPointer(VertexCode);
    PipelineDesc.VS.BytecodeLength = ID3D10Blob_GetBufferSize(VertexCode);
    PipelineDesc.PS.pShaderBytecode = ID3D10Blob_GetBufferPointer(PixelCode);
    PipelineDesc.PS.BytecodeLength = ID3D10Blob_GetBufferSize(PixelCode);
    PipelineDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    PipelineDesc.SampleMask = 0xffffffff;
    PipelineDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    PipelineDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    PipelineDesc.RasterizerState.DepthClipEnable = TRUE;
    PipelineDesc.DepthStencilState.DepthEnable = TRUE;
    PipelineDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    PipelineDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    PipelineDesc.InputLayout.pInputElementDescs = Layout;
    PipelineDesc.InputLayout.NumElements = RTL_NUMBER_OF(Layout);
    PipelineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    PipelineDesc.NumRenderTargets = 1;
    PipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    PipelineDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    PipelineDesc.SampleDesc.Count = 1;
    Result = ID3D12Device_CreateGraphicsPipelineState(Device, &PipelineDesc, &IID_ID3D12PipelineState,
                                                      (void **)&Pipeline);
    if (FAILED(Result))
        goto Done;

    Step = "draw";
    Result = ID3D12CommandAllocator_Reset(Allocator);
    if (SUCCEEDED(Result))
        Result = ID3D12GraphicsCommandList_Reset(List, Allocator, Pipeline);
    if (FAILED(Result))
        goto Done;
    ZeroMemory(&Destination, sizeof(Destination));
    Destination.pResource = Texture;
    Destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    ZeroMemory(&Source, sizeof(Source));
    Source.pResource = Upload;
    Source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    Source.PlacedFootprint = TextureFootprint;
    ID3D12GraphicsCommandList_CopyTextureRegion(List, &Destination, 0, 0, 0, &Source, NULL);
    ZeroMemory(&Barrier, sizeof(Barrier));
    Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    Barrier.Transition.pResource = Texture;
    Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    ID3D12GraphicsCommandList_ResourceBarrier(List, 1, &Barrier);
    Barrier.Transition.pResource = Target;
    Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    ID3D12GraphicsCommandList_ResourceBarrier(List, 1, &Barrier);
    Viewport.TopLeftX = 0.0f;
    Viewport.TopLeftY = 0.0f;
    Viewport.Width = 64.0f;
    Viewport.Height = 64.0f;
    Viewport.MinDepth = 0.0f;
    Viewport.MaxDepth = 1.0f;
    Scissor.left = 0;
    Scissor.top = 0;
    Scissor.right = 64;
    Scissor.bottom = 64;
    Heaps[0] = Views;
    Heaps[1] = Samplers;
    ID3D12GraphicsCommandList_SetDescriptorHeaps(List, 2, Heaps);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(List, RootSignature);
    Table = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(Views);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(List, 0, Table);
    Table = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(Samplers);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(List, 1, Table);
    ID3D12GraphicsCommandList_RSSetViewports(List, 1, &Viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(List, 1, &Scissor);
    ID3D12GraphicsCommandList_OMSetRenderTargets(List, 1, &View, FALSE, &DepthHandle);
    ID3D12GraphicsCommandList_ClearRenderTargetView(List, View, Black, 0, NULL);
    ID3D12GraphicsCommandList_ClearDepthStencilView(List, DepthHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(List, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_IASetVertexBuffers(List, 0, 1, VertexView);
    ID3D12GraphicsCommandList_DrawInstanced(List, 3, 1, 0, 0);
    Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    ID3D12GraphicsCommandList_ResourceBarrier(List, 1, &Barrier);
    ZeroMemory(&Source, sizeof(Source));
    Source.pResource = Target;
    Source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    ZeroMemory(&Destination, sizeof(Destination));
    Destination.pResource = Readback;
    Destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    Destination.PlacedFootprint = *Footprint;
    ID3D12GraphicsCommandList_CopyTextureRegion(List, &Destination, 0, 0, 0, &Source, NULL);
    Result = FeatExecute12(Queue, List, Fence, Event, FenceValue);
    if (FAILED(Result))
        goto Done;

    Step = "map";
    Result = ID3D12Resource_Map(Readback, 0, NULL, &Data);
    if (FAILED(Result))
        goto Done;
    Pixel = *(const DWORD *)((const BYTE *)Data + Footprint->Offset + 32 * Footprint->Footprint.RowPitch + 32 * 4);
    ID3D12Resource_Unmap(Readback, 0, NULL);

Done:
    FeatPrint("D3DFEAT_D3D12_SCENE adapter=%lu step=\"%s\" hr=0x%08lx removed=0x%08lx pixel=0x%08lx result=%s\n",
              Adapter, Step, Result, ID3D12Device_GetDeviceRemovedReason(Device), Pixel,
              SUCCEEDED(Result) && (Pixel == 0xff408080 || Pixel == 0xff40807f) ? "pass" : "fail");
    if (Pipeline != NULL) ID3D12PipelineState_Release(Pipeline);
    if (RootSignature != NULL) ID3D12RootSignature_Release(RootSignature);
    if (SignatureBlob != NULL) ID3D10Blob_Release(SignatureBlob);
    if (PixelCode != NULL) ID3D10Blob_Release(PixelCode);
    if (VertexCode != NULL) ID3D10Blob_Release(VertexCode);
    if (DepthViews != NULL) ID3D12DescriptorHeap_Release(DepthViews);
    if (Samplers != NULL) ID3D12DescriptorHeap_Release(Samplers);
    if (Views != NULL) ID3D12DescriptorHeap_Release(Views);
    if (Constants != NULL) ID3D12Resource_Release(Constants);
    if (Upload != NULL) ID3D12Resource_Release(Upload);
    if (Depth != NULL) ID3D12Resource_Release(Depth);
    if (Texture != NULL) ID3D12Resource_Release(Texture);
}

static HRESULT
FeatDirect3D12Extras(
    _In_ ULONG Adapter,
    _In_ ID3D12Device *Device,
    _In_ ID3D12CommandQueue *Queue,
    _In_ ID3D12CommandAllocator *Allocator,
    _In_ ID3D12GraphicsCommandList *List,
    _In_ ID3D12Fence *Fence,
    _In_ HANDLE Event,
    _Inout_ UINT64 *FenceValue,
    _In_ ID3D12PipelineState *Pipeline,
    _In_ ID3D12RootSignature *RootSignature,
    _In_ const D3D12_VERTEX_BUFFER_VIEW *VertexView,
    _In_ D3D12_CPU_DESCRIPTOR_HANDLE View,
    _In_ ID3D12Resource *Target,
    _In_ ID3D12Resource *Readback,
    _In_ const D3D12_PLACED_SUBRESOURCE_FOOTPRINT *Footprint)
{
    D3D12_FEATURE_DATA_FORMAT_SUPPORT Format;
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS Quality;
    D3D12_QUERY_HEAP_DESC QueryDesc;
    D3D12_HEAP_DESC HeapDesc;
    D3D12_RESOURCE_DESC Desc;
    D3D12_RESOURCE_BARRIER Barrier;
    D3D12_VIEWPORT Viewport;
    D3D12_RECT Scissor;
    ID3D12QueryHeap *Queries = NULL;
    ID3D12Heap *Heap = NULL;
    ID3D12Resource *First = NULL, *Second = NULL;
    UINT64 Samples = 0, FirstAddress = 0, SecondAddress = 0;
    DWORD *Words;
    PVOID Data;
    HRESULT Result;

    UNREFERENCED_PARAMETER(Footprint);

    ZeroMemory(&Format, sizeof(Format));
    Format.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Result = ID3D12Device_CheckFeatureSupport(Device, D3D12_FEATURE_FORMAT_SUPPORT, &Format, sizeof(Format));
    ZeroMemory(&Quality, sizeof(Quality));
    Quality.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Quality.SampleCount = 4;
    ID3D12Device_CheckFeatureSupport(Device, D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &Quality, sizeof(Quality));
    FeatPrint("D3DFEAT_D3D12_FORMAT adapter=%lu hr=0x%08lx support1=0x%08x support2=0x%08x msaa4_levels=%u\n",
              Adapter, Result, Format.Support1, Format.Support2, Quality.NumQualityLevels);

    ZeroMemory(&QueryDesc, sizeof(QueryDesc));
    QueryDesc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
    QueryDesc.Count = 1;
    Result = ID3D12Device_CreateQueryHeap(Device, &QueryDesc, &IID_ID3D12QueryHeap, (void **)&Queries);
    if (SUCCEEDED(Result))
        Result = ID3D12CommandAllocator_Reset(Allocator);
    if (SUCCEEDED(Result))
        Result = ID3D12GraphicsCommandList_Reset(List, Allocator, Pipeline);
    if (SUCCEEDED(Result))
    {
        Viewport.TopLeftX = 0.0f;
        Viewport.TopLeftY = 0.0f;
        Viewport.Width = 64.0f;
        Viewport.Height = 64.0f;
        Viewport.MinDepth = 0.0f;
        Viewport.MaxDepth = 1.0f;
        Scissor.left = 0;
        Scissor.top = 0;
        Scissor.right = 64;
        Scissor.bottom = 64;
        ZeroMemory(&Barrier, sizeof(Barrier));
        Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        Barrier.Transition.pResource = Target;
        Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ID3D12GraphicsCommandList_ResourceBarrier(List, 1, &Barrier);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(List, RootSignature);
        ID3D12GraphicsCommandList_RSSetViewports(List, 1, &Viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(List, 1, &Scissor);
        ID3D12GraphicsCommandList_OMSetRenderTargets(List, 1, &View, FALSE, NULL);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(List, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_IASetVertexBuffers(List, 0, 1, VertexView);
        ID3D12GraphicsCommandList_BeginQuery(List, Queries, D3D12_QUERY_TYPE_OCCLUSION, 0);
        ID3D12GraphicsCommandList_DrawInstanced(List, 3, 1, 0, 0);
        ID3D12GraphicsCommandList_EndQuery(List, Queries, D3D12_QUERY_TYPE_OCCLUSION, 0);
        Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        ID3D12GraphicsCommandList_ResourceBarrier(List, 1, &Barrier);
        ID3D12GraphicsCommandList_ResolveQueryData(List, Queries, D3D12_QUERY_TYPE_OCCLUSION, 0, 1, Readback, 0);
        Result = FeatExecute12(Queue, List, Fence, Event, FenceValue);
    }
    if (SUCCEEDED(Result))
        Result = ID3D12Resource_Map(Readback, 0, NULL, &Data);
    if (SUCCEEDED(Result))
    {
        Samples = *(const UINT64 *)Data;
        ID3D12Resource_Unmap(Readback, 0, NULL);
    }
    FeatPrint("D3DFEAT_D3D12_QUERY adapter=%lu hr=0x%08lx samples=%I64u result=%s\n", Adapter, Result, Samples,
              SUCCEEDED(Result) && Samples == 64 * 64 ? "pass" : "fail");
    if (Queries != NULL) ID3D12QueryHeap_Release(Queries);

    ZeroMemory(&HeapDesc, sizeof(HeapDesc));
    HeapDesc.SizeInBytes = 2 * 65536;
    HeapDesc.Properties.Type = D3D12_HEAP_TYPE_UPLOAD;
    HeapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    Result = ID3D12Device_CreateHeap(Device, &HeapDesc, &IID_ID3D12Heap, (void **)&Heap);
    ZeroMemory(&Desc, sizeof(Desc));
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    Desc.Width = 65536;
    Desc.Height = 1;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.SampleDesc.Count = 1;
    Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreatePlacedResource(Device, Heap, 0, &Desc, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                                                   &IID_ID3D12Resource, (void **)&First);
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreatePlacedResource(Device, Heap, 65536, &Desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                   NULL, &IID_ID3D12Resource, (void **)&Second);
    if (SUCCEEDED(Result))
    {
        FirstAddress = ID3D12Resource_GetGPUVirtualAddress(First);
        SecondAddress = ID3D12Resource_GetGPUVirtualAddress(Second);
        Result = ID3D12Resource_Map(Second, 0, NULL, &Data);
    }
    if (SUCCEEDED(Result))
    {
        Words = Data;
        Words[0] = 0x12345678;
        Words[1] = 0x9abcdef0;
        ID3D12Resource_Unmap(Second, 0, NULL);
        Result = ID3D12CommandAllocator_Reset(Allocator);
    }
    if (SUCCEEDED(Result))
        Result = ID3D12GraphicsCommandList_Reset(List, Allocator, NULL);
    if (SUCCEEDED(Result))
    {
        ID3D12GraphicsCommandList_CopyBufferRegion(List, Readback, 64, Second, 0, 8);
        Result = FeatExecute12(Queue, List, Fence, Event, FenceValue);
    }
    Samples = 0;
    if (SUCCEEDED(Result))
        Result = ID3D12Resource_Map(Readback, 0, NULL, &Data);
    if (SUCCEEDED(Result))
    {
        Samples = *(const UINT64 *)((const BYTE *)Data + 64);
        ID3D12Resource_Unmap(Readback, 0, NULL);
    }
    FeatPrint("D3DFEAT_D3D12_PLACED adapter=%lu hr=0x%08lx first=0x%I64x second=0x%I64x copied=0x%I64x result=%s\n",
              Adapter, Result, FirstAddress, SecondAddress, Samples,
              SUCCEEDED(Result) && SecondAddress == FirstAddress + 65536 && Samples == 0x9abcdef012345678ULL
              ? "pass" : "fail");
    if (Second != NULL) ID3D12Resource_Release(Second);
    if (First != NULL) ID3D12Resource_Release(First);
    if (Heap != NULL) ID3D12Heap_Release(Heap);

    {
        D3D12_TILED_RESOURCE_COORDINATE Coordinate;
        D3D12_TILE_REGION_SIZE Region;
        D3D12_TILE_RANGE_FLAGS RangeFlags = D3D12_TILE_RANGE_FLAG_NONE;
        ID3D12Resource *Reserved = NULL, *Placed = NULL, *Source = NULL;
        UINT HeapOffset = 1, TileCount = 1, Tiles = 0;
        UINT64 Copied = 0;

        Heap = NULL;
        ZeroMemory(&HeapDesc, sizeof(HeapDesc));
        HeapDesc.SizeInBytes = 2 * 65536;
        HeapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        HeapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
        Result = ID3D12Device_CreateHeap(Device, &HeapDesc, &IID_ID3D12Heap, (void **)&Heap);
        if (SUCCEEDED(Result))
            Result = ID3D12Device_CreateReservedResource(Device, &Desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL,
                                                         &IID_ID3D12Resource, (void **)&Reserved);
        if (SUCCEEDED(Result))
            Result = ID3D12Device_CreatePlacedResource(Device, Heap, 65536, &Desc, D3D12_RESOURCE_STATE_COPY_SOURCE,
                                                       NULL, &IID_ID3D12Resource, (void **)&Placed);
        if (SUCCEEDED(Result))
        {
            D3D12_HEAP_PROPERTIES Upload;

            ZeroMemory(&Upload, sizeof(Upload));
            Upload.Type = D3D12_HEAP_TYPE_UPLOAD;
            Result = ID3D12Device_CreateCommittedResource(Device, &Upload, D3D12_HEAP_FLAG_NONE, &Desc,
                                                          D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                                                          &IID_ID3D12Resource, (void **)&Source);
        }
        if (SUCCEEDED(Result))
            Result = ID3D12Resource_Map(Source, 0, NULL, &Data);
        if (SUCCEEDED(Result))
        {
            Words = Data;
            Words[0] = 0x0badf00d;
            Words[1] = 0x600dcafe;
            ID3D12Resource_Unmap(Source, 0, NULL);
            ID3D12Device_GetResourceTiling(Device, Reserved, &Tiles, NULL, NULL, NULL, 0, NULL);
            ZeroMemory(&Coordinate, sizeof(Coordinate));
            ZeroMemory(&Region, sizeof(Region));
            Region.NumTiles = 1;
            ID3D12CommandQueue_UpdateTileMappings(Queue, Reserved, 1, &Coordinate, &Region, Heap, 1, &RangeFlags,
                                                  &HeapOffset, &TileCount, D3D12_TILE_MAPPING_FLAG_NONE);
            Result = ID3D12CommandAllocator_Reset(Allocator);
        }
        if (SUCCEEDED(Result))
            Result = ID3D12GraphicsCommandList_Reset(List, Allocator, NULL);
        if (SUCCEEDED(Result))
        {
            ID3D12GraphicsCommandList_CopyBufferRegion(List, Reserved, 0, Source, 0, 8);
            Result = FeatExecute12(Queue, List, Fence, Event, FenceValue);
        }
        /* The reserved and placed buffers alias one heap tile; separate submissions order the two uses (ResourceHeaps.md) */
        if (SUCCEEDED(Result))
            Result = ID3D12CommandAllocator_Reset(Allocator);
        if (SUCCEEDED(Result))
            Result = ID3D12GraphicsCommandList_Reset(List, Allocator, NULL);
        if (SUCCEEDED(Result))
        {
            ID3D12GraphicsCommandList_CopyBufferRegion(List, Readback, 128, Placed, 0, 8);
            Result = FeatExecute12(Queue, List, Fence, Event, FenceValue);
        }
        if (SUCCEEDED(Result))
            Result = ID3D12Resource_Map(Readback, 0, NULL, &Data);
        if (SUCCEEDED(Result))
        {
            Copied = *(const UINT64 *)((const BYTE *)Data + 128);
            ID3D12Resource_Unmap(Readback, 0, NULL);
        }
        FeatPrint("D3DFEAT_D3D12_TILED adapter=%lu hr=0x%08lx removed=0x%08lx tiles=%u copied=0x%I64x result=%s\n",
                  Adapter, Result, ID3D12Device_GetDeviceRemovedReason(Device), Tiles, Copied,
                  SUCCEEDED(Result) && Copied == 0x600dcafe0badf00dULL ? "pass" : "fail");
        if (Source != NULL) ID3D12Resource_Release(Source);
        if (Placed != NULL) ID3D12Resource_Release(Placed);
        if (Reserved != NULL) ID3D12Resource_Release(Reserved);
        if (Heap != NULL) ID3D12Heap_Release(Heap);
    }
    return S_OK;
}

static VOID
FeatDirect3D12Render(
    _In_ ULONG Adapter,
    _In_ ID3D12Device *Device)
{
    static const FLOAT Blue[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    static const FLOAT Vertices[] =
    {
        -1.0f, -1.0f, 0.0f, 1.0f,
        -1.0f,  3.0f, 0.0f, 1.0f,
         3.0f, -1.0f, 0.0f, 1.0f,
    };
    static const D3D12_INPUT_ELEMENT_DESC Layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    ID3D12CommandQueue *Queue = NULL;
    ID3D12CommandAllocator *Allocator = NULL;
    ID3D12GraphicsCommandList *List = NULL;
    ID3D12Fence *Fence = NULL;
    ID3D12Resource *Target = NULL, *Readback = NULL, *VertexBuffer = NULL;
    ID3D12DescriptorHeap *Heap = NULL;
    ID3D12RootSignature *RootSignature = NULL;
    ID3D12PipelineState *Pipeline = NULL;
    ID3DBlob *VertexCode = NULL, *PixelCode = NULL, *SignatureBlob = NULL;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC PipelineDesc;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT Footprint;
    D3D12_TEXTURE_COPY_LOCATION Source, Destination;
    D3D12_ROOT_SIGNATURE_DESC SignatureDesc;
    D3D12_COMMAND_QUEUE_DESC QueueDesc;
    D3D12_DESCRIPTOR_HEAP_DESC HeapDesc;
    D3D12_HEAP_PROPERTIES HeapProperties;
    D3D12_VERTEX_BUFFER_VIEW VertexView;
    D3D12_CPU_DESCRIPTOR_HANDLE View;
    D3D12_RESOURCE_BARRIER Barrier;
    D3D12_RESOURCE_DESC Desc;
    D3D12_VIEWPORT Viewport;
    D3D12_RECT Scissor;
    UINT64 Total = 0, FenceValue = 0;
    DWORD Cleared = 0xdeadbeef, Drawn = 0xdeadbeef;
    HANDLE Event = NULL;
    PCSTR Step = "queue";
    HRESULT Result;
    PVOID Data;

    ZeroMemory(&QueueDesc, sizeof(QueueDesc));
    QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Result = ID3D12Device_CreateCommandQueue(Device, &QueueDesc, &IID_ID3D12CommandQueue, (void **)&Queue);
    if (FAILED(Result))
        goto Done;

    Step = "allocator";
    Result = ID3D12Device_CreateCommandAllocator(Device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 &IID_ID3D12CommandAllocator, (void **)&Allocator);
    if (FAILED(Result))
        goto Done;

    Step = "fence";
    Result = ID3D12Device_CreateFence(Device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&Fence);
    if (FAILED(Result))
        goto Done;

    Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    Step = "vertex buffer";
    ZeroMemory(&HeapProperties, sizeof(HeapProperties));
    HeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
    ZeroMemory(&Desc, sizeof(Desc));
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    Desc.Width = sizeof(Vertices);
    Desc.Height = 1;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.SampleDesc.Count = 1;
    Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Result = ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &Desc,
                                                  D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                                                  &IID_ID3D12Resource, (void **)&VertexBuffer);
    if (FAILED(Result))
        goto Done;

    Result = ID3D12Resource_Map(VertexBuffer, 0, NULL, &Data);
    if (FAILED(Result))
        goto Done;

    CopyMemory(Data, Vertices, sizeof(Vertices));
    ID3D12Resource_Unmap(VertexBuffer, 0, NULL);
    VertexView.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(VertexBuffer);
    VertexView.SizeInBytes = sizeof(Vertices);
    VertexView.StrideInBytes = 4 * sizeof(FLOAT);
    FeatPrint("D3DFEAT_D3D12_BUFFER adapter=%lu cpu=%p gpu=0x%I64x\n", Adapter, Data, VertexView.BufferLocation);

    Step = "buffer release";
    Result = ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &Desc,
                                                  D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                                                  &IID_ID3D12Resource, (void **)&Readback);
    if (FAILED(Result))
        goto Done;

    ID3D12Resource_Release(Readback);
    Readback = NULL;
    FeatPrint("D3DFEAT_D3D12_BUFFER_RELEASE adapter=%lu result=pass\n", Adapter);

    Step = "target";
    ZeroMemory(&HeapProperties, sizeof(HeapProperties));
    HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    ZeroMemory(&Desc, sizeof(Desc));
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    Desc.Width = 64;
    Desc.Height = 64;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    Result = ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &Desc,
                                                  D3D12_RESOURCE_STATE_RENDER_TARGET, NULL,
                                                  &IID_ID3D12Resource, (void **)&Target);
    if (FAILED(Result))
        goto Done;

    Step = "readback";
    ID3D12Device_GetCopyableFootprints(Device, &Desc, 0, 1, 0, &Footprint, NULL, NULL, &Total);
    HeapProperties.Type = D3D12_HEAP_TYPE_READBACK;
    ZeroMemory(&Desc, sizeof(Desc));
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    Desc.Width = Total;
    Desc.Height = 1;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.SampleDesc.Count = 1;
    Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Result = ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &Desc,
                                                  D3D12_RESOURCE_STATE_COPY_DEST, NULL,
                                                  &IID_ID3D12Resource, (void **)&Readback);
    if (FAILED(Result))
        goto Done;

    Step = "descriptor heap";
    ZeroMemory(&HeapDesc, sizeof(HeapDesc));
    HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    HeapDesc.NumDescriptors = 1;
    Result = ID3D12Device_CreateDescriptorHeap(Device, &HeapDesc, &IID_ID3D12DescriptorHeap, (void **)&Heap);
    if (FAILED(Result))
        goto Done;

    View = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(Heap);
    ID3D12Device_CreateRenderTargetView(Device, Target, NULL, View);

    Step = "command list";
    Result = ID3D12Device_CreateCommandList(Device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, Allocator, NULL,
                                            &IID_ID3D12GraphicsCommandList, (void **)&List);
    if (FAILED(Result))
        goto Done;

    ZeroMemory(&Barrier, sizeof(Barrier));
    Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    Barrier.Transition.pResource = Target;
    Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    ZeroMemory(&Source, sizeof(Source));
    Source.pResource = Target;
    Source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    ZeroMemory(&Destination, sizeof(Destination));
    Destination.pResource = Readback;
    Destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    Destination.PlacedFootprint = Footprint;

    Step = "copy check";
    {
        ID3D12Resource *Check = NULL;
        D3D12_HEAP_PROPERTIES CheckHeap;
        D3D12_RESOURCE_DESC CheckDesc;
        PVOID CheckData;

        ZeroMemory(&CheckHeap, sizeof(CheckHeap));
        CheckHeap.Type = D3D12_HEAP_TYPE_READBACK;
        ZeroMemory(&CheckDesc, sizeof(CheckDesc));
        CheckDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        CheckDesc.Width = sizeof(Vertices);
        CheckDesc.Height = 1;
        CheckDesc.DepthOrArraySize = 1;
        CheckDesc.MipLevels = 1;
        CheckDesc.SampleDesc.Count = 1;
        CheckDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Result = ID3D12Device_CreateCommittedResource(Device, &CheckHeap, D3D12_HEAP_FLAG_NONE, &CheckDesc,
                                                      D3D12_RESOURCE_STATE_COPY_DEST, NULL,
                                                      &IID_ID3D12Resource, (void **)&Check);
        if (FAILED(Result))
            goto Done;

        ID3D12GraphicsCommandList_CopyBufferRegion(List, Check, 0, VertexBuffer, 0, sizeof(Vertices));
        Result = FeatExecute12(Queue, List, Fence, Event, &FenceValue);
        if (SUCCEEDED(Result))
            Result = ID3D12Resource_Map(Check, 0, NULL, &CheckData);
        if (FAILED(Result))
        {
            ID3D12Resource_Release(Check);
            goto Done;
        }

        FeatPrint("D3DFEAT_D3D12_COPY adapter=%lu first=0x%08lx expected=0x%08lx result=%s\n", Adapter,
                  *(const DWORD *)CheckData, *(const DWORD *)Vertices,
                  !memcmp(CheckData, Vertices, sizeof(Vertices)) ? "pass" : "fail");
        ID3D12Resource_Unmap(Check, 0, NULL);
        ID3D12Resource_Release(Check);
        Result = ID3D12CommandAllocator_Reset(Allocator);
        if (SUCCEEDED(Result))
            Result = ID3D12GraphicsCommandList_Reset(List, Allocator, NULL);
        if (FAILED(Result))
            goto Done;
    }

    Step = "clear";
    ID3D12GraphicsCommandList_ClearRenderTargetView(List, View, Blue, 0, NULL);
    ID3D12GraphicsCommandList_ResourceBarrier(List, 1, &Barrier);
    ID3D12GraphicsCommandList_CopyTextureRegion(List, &Destination, 0, 0, 0, &Source, NULL);
    Result = FeatExecute12(Queue, List, Fence, Event, &FenceValue);
    if (FAILED(Result))
        goto Done;

    Step = "map";
    Result = ID3D12Resource_Map(Readback, 0, NULL, &Data);
    if (FAILED(Result))
        goto Done;

    Cleared = *(const DWORD *)((const BYTE *)Data + Footprint.Offset + 32 * Footprint.Footprint.RowPitch + 32 * 4);
    ID3D12Resource_Unmap(Readback, 0, NULL);
    FeatPrint("D3DFEAT_D3D12_CLEAR adapter=%lu pixel=0x%08lx result=%s\n",
              Adapter, Cleared, Cleared == 0xffff0000 ? "pass" : "fail");

    Step = "shaders";
    Result = D3DCompile(FeatVertexShader, sizeof(FeatVertexShader) - 1, NULL, NULL, NULL, "main", "vs_5_0", 0, 0,
                        &VertexCode, NULL);
    if (FAILED(Result))
        goto Done;

    Result = D3DCompile(FeatPixelShader, sizeof(FeatPixelShader) - 1, NULL, NULL, NULL, "main", "ps_5_0", 0, 0,
                        &PixelCode, NULL);
    if (FAILED(Result))
        goto Done;

    Step = "root signature";
    ZeroMemory(&SignatureDesc, sizeof(SignatureDesc));
    SignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    Result = D3D12SerializeRootSignature(&SignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &SignatureBlob, NULL);
    if (FAILED(Result))
        goto Done;

    Result = ID3D12Device_CreateRootSignature(Device, 0, ID3D10Blob_GetBufferPointer(SignatureBlob),
                                              ID3D10Blob_GetBufferSize(SignatureBlob),
                                              &IID_ID3D12RootSignature, (void **)&RootSignature);
    if (FAILED(Result))
        goto Done;

    Step = "pipeline state";
    ZeroMemory(&PipelineDesc, sizeof(PipelineDesc));
    PipelineDesc.pRootSignature = RootSignature;
    PipelineDesc.VS.pShaderBytecode = ID3D10Blob_GetBufferPointer(VertexCode);
    PipelineDesc.VS.BytecodeLength = ID3D10Blob_GetBufferSize(VertexCode);
    PipelineDesc.PS.pShaderBytecode = ID3D10Blob_GetBufferPointer(PixelCode);
    PipelineDesc.PS.BytecodeLength = ID3D10Blob_GetBufferSize(PixelCode);
    PipelineDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    PipelineDesc.SampleMask = 0xffffffff;
    PipelineDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    PipelineDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    PipelineDesc.RasterizerState.DepthClipEnable = TRUE;
    PipelineDesc.InputLayout.pInputElementDescs = Layout;
    PipelineDesc.InputLayout.NumElements = RTL_NUMBER_OF(Layout);
    PipelineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    PipelineDesc.NumRenderTargets = 1;
    PipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    PipelineDesc.SampleDesc.Count = 1;
    Result = ID3D12Device_CreateGraphicsPipelineState(Device, &PipelineDesc, &IID_ID3D12PipelineState,
                                                      (void **)&Pipeline);
    if (FAILED(Result))
        goto Done;

    Step = "draw";
    Result = ID3D12CommandAllocator_Reset(Allocator);
    if (FAILED(Result))
        goto Done;

    Result = ID3D12GraphicsCommandList_Reset(List, Allocator, Pipeline);
    if (FAILED(Result))
        goto Done;

    Viewport.TopLeftX = 0.0f;
    Viewport.TopLeftY = 0.0f;
    Viewport.Width = 64.0f;
    Viewport.Height = 64.0f;
    Viewport.MinDepth = 0.0f;
    Viewport.MaxDepth = 1.0f;
    Scissor.left = 0;
    Scissor.top = 0;
    Scissor.right = 64;
    Scissor.bottom = 64;
    Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    ID3D12GraphicsCommandList_ResourceBarrier(List, 1, &Barrier);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(List, RootSignature);
    ID3D12GraphicsCommandList_RSSetViewports(List, 1, &Viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(List, 1, &Scissor);
    ID3D12GraphicsCommandList_OMSetRenderTargets(List, 1, &View, FALSE, NULL);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(List, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_IASetVertexBuffers(List, 0, 1, &VertexView);
    ID3D12GraphicsCommandList_DrawInstanced(List, 3, 1, 0, 0);
    Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    ID3D12GraphicsCommandList_ResourceBarrier(List, 1, &Barrier);
    ID3D12GraphicsCommandList_CopyTextureRegion(List, &Destination, 0, 0, 0, &Source, NULL);
    Result = FeatExecute12(Queue, List, Fence, Event, &FenceValue);
    if (FAILED(Result))
        goto Done;

    Step = "map";
    Result = ID3D12Resource_Map(Readback, 0, NULL, &Data);
    if (FAILED(Result))
        goto Done;

    Drawn = *(const DWORD *)((const BYTE *)Data + Footprint.Offset + 32 * Footprint.Footprint.RowPitch + 32 * 4);
    ID3D12Resource_Unmap(Readback, 0, NULL);
    FeatPrint("D3DFEAT_D3D12_DRAW adapter=%lu pixel=0x%08lx result=%s\n",
              Adapter, Drawn, Drawn == 0xff00ff00 ? "pass" : "fail");

    FeatDirect3D12Scene(Adapter, Device, Queue, Allocator, List, Fence, Event, &FenceValue, &VertexView, View,
                        Target, Readback, &Footprint);

    Step = "extras";
    Result = FeatDirect3D12Extras(Adapter, Device, Queue, Allocator, List, Fence, Event, &FenceValue, Pipeline,
                                  RootSignature, &VertexView, View, Target, Readback, &Footprint);

Done:
    if (FAILED(Result))
    {
        FeatPrint("D3DFEAT_D3D12_RENDER adapter=%lu step=\"%s\" hr=0x%08lx removed=0x%08lx result=fail\n",
                  Adapter, Step, Result, ID3D12Device_GetDeviceRemovedReason(Device));
    }
    if (VertexBuffer != NULL) ID3D12Resource_Release(VertexBuffer);
    if (Pipeline != NULL) ID3D12PipelineState_Release(Pipeline);
    if (RootSignature != NULL) ID3D12RootSignature_Release(RootSignature);
    if (SignatureBlob != NULL) ID3D10Blob_Release(SignatureBlob);
    if (PixelCode != NULL) ID3D10Blob_Release(PixelCode);
    if (VertexCode != NULL) ID3D10Blob_Release(VertexCode);
    if (List != NULL) ID3D12GraphicsCommandList_Release(List);
    if (Heap != NULL) ID3D12DescriptorHeap_Release(Heap);
    if (Readback != NULL) ID3D12Resource_Release(Readback);
    if (Target != NULL) ID3D12Resource_Release(Target);
    if (Event != NULL) CloseHandle(Event);
    if (Fence != NULL) ID3D12Fence_Release(Fence);
    if (Allocator != NULL) ID3D12CommandAllocator_Release(Allocator);
    if (Queue != NULL) ID3D12CommandQueue_Release(Queue);
}

static VOID
FeatStreamAppend(
    _Inout_updates_bytes_(Capacity) BYTE *Stream,
    _In_ SIZE_T Capacity,
    _Inout_ SIZE_T *Size,
    _In_ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type,
    _In_reads_bytes_(PayloadSize) const VOID *Payload,
    _In_ SIZE_T PayloadSize,
    _In_ SIZE_T Alignment)
{
    SIZE_T Offset = (*Size + sizeof(Type) + Alignment - 1) & ~(Alignment - 1);
    SIZE_T End = (Offset + PayloadSize + sizeof(void *) - 1) & ~(sizeof(void *) - 1);

    if (End > Capacity)
        return;
    CopyMemory(Stream + *Size, &Type, sizeof(Type));
    CopyMemory(Stream + Offset, Payload, PayloadSize);
    *Size = End;
}

static VOID
FeatDirect3D12Modern(
    _In_ ULONG Adapter,
    _In_ ID3D12Device *Device)
{
    static const FLOAT Red[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
    static const FLOAT Vertices[] =
    {
        -1.0f, -1.0f, 0.0f, 1.0f,
        -1.0f,  3.0f, 0.0f, 1.0f,
         3.0f, -1.0f, 0.0f, 1.0f,
    };
    static const D3D12_INPUT_ELEMENT_DESC Layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    static const struct
    {
        const IID *Iid;
        PCSTR Name;
    } DeviceInterfaces[] =
    {
        { &IID_ID3D12Device1, "Device1" },
        { &IID_ID3D12Device2, "Device2" },
        { &IID_ID3D12Device3, "Device3" },
        { &IID_ID3D12Device4, "Device4" },
        { &IID_ID3D12Device5, "Device5" },
    }, ListInterfaces[] =
    {
        { &IID_ID3D12GraphicsCommandList1, "List1" },
        { &IID_ID3D12GraphicsCommandList2, "List2" },
        { &IID_ID3D12GraphicsCommandList3, "List3" },
        { &IID_ID3D12GraphicsCommandList4, "List4" },
    };
    D3D12_FEATURE_DATA_D3D12_OPTIONS2 Options2;
    D3D12_FEATURE_DATA_D3D12_OPTIONS3 Options3;
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 Options5;
    ID3D12Device4 *Device4 = NULL;
    ID3D12CommandQueue *Queue = NULL;
    ID3D12CommandAllocator *Allocator = NULL;
    ID3D12GraphicsCommandList4 *List = NULL;
    ID3D12Fence *Fence = NULL, *Second = NULL;
    ID3D12Resource *Target = NULL, *Readback = NULL, *VertexBuffer = NULL, *Values = NULL;
    ID3D12DescriptorHeap *Heap = NULL;
    ID3D12RootSignature *RootSignature = NULL;
    ID3D12PipelineState *Pipeline = NULL;
    ID3DBlob *VertexCode = NULL, *PixelCode = NULL, *SignatureBlob = NULL;
    D3D12_PIPELINE_STATE_STREAM_DESC StreamDesc;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT Footprint;
    D3D12_TEXTURE_COPY_LOCATION Source, Destination;
    D3D12_ROOT_SIGNATURE_DESC SignatureDesc;
    D3D12_COMMAND_QUEUE_DESC QueueDesc;
    D3D12_DESCRIPTOR_HEAP_DESC HeapDesc;
    D3D12_HEAP_PROPERTIES HeapProperties;
    D3D12_VERTEX_BUFFER_VIEW VertexView;
    D3D12_CPU_DESCRIPTOR_HANDLE View;
    D3D12_RESOURCE_BARRIER Barrier;
    D3D12_RESOURCE_DESC Desc;
    D3D12_VIEWPORT Viewport;
    D3D12_RECT Scissor;
    D3D12_RENDER_PASS_RENDER_TARGET_DESC Pass;
    struct D3D12_RT_FORMAT_ARRAY Formats;
    D3D12_INPUT_LAYOUT_DESC InputLayout;
    D3D12_PRIMITIVE_TOPOLOGY_TYPE Topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    D3D12_RASTERIZER_DESC Rasterizer;
    D3D12_SHADER_BYTECODE Code;
    D3D12_WRITEBUFFERIMMEDIATE_PARAMETER Immediate[2];
    ID3D12Fence *WaitFences[2];
    UINT64 WaitValues[2], Total = 0, FenceValue = 0;
    DWORD Cleared = 0xdeadbeef, Drawn = 0xdeadbeef, Written[2] = { 0xdeadbeef, 0xdeadbeef };
    DWORD Waited = WAIT_FAILED;
    BOOL WriteImmediate;
    BYTE Stream[512];
    SIZE_T StreamSize = 0;
    HANDLE Event = NULL;
    PCSTR Step = "interfaces";
    IUnknown *Unknown;
    HRESULT Result;
    ULONG Index;
    PVOID Data;

    for (Index = 0; Index < RTL_NUMBER_OF(DeviceInterfaces); ++Index)
    {
        Result = ID3D12Device_QueryInterface(Device, DeviceInterfaces[Index].Iid, (void **)&Unknown);
        FeatPrint("D3DFEAT_D3D12_INTERFACE adapter=%lu name=%s hr=0x%08lx\n",
                  Adapter, DeviceInterfaces[Index].Name, Result);
        if (SUCCEEDED(Result))
            IUnknown_Release(Unknown);
    }

    ZeroMemory(&Options2, sizeof(Options2));
    ZeroMemory(&Options3, sizeof(Options3));
    ZeroMemory(&Options5, sizeof(Options5));
    ID3D12Device_CheckFeatureSupport(Device, D3D12_FEATURE_D3D12_OPTIONS2, &Options2, sizeof(Options2));
    ID3D12Device_CheckFeatureSupport(Device, D3D12_FEATURE_D3D12_OPTIONS3, &Options3, sizeof(Options3));
    ID3D12Device_CheckFeatureSupport(Device, D3D12_FEATURE_D3D12_OPTIONS5, &Options5, sizeof(Options5));
    FeatPrint("D3DFEAT_D3D12_OPTIONS2_5 adapter=%lu depth_bounds=%d sample_positions_tier=%d write_immediate=0x%x "
              "view_instancing_tier=%d render_pass_tier=%d raytracing_tier=%d\n",
              Adapter, Options2.DepthBoundsTestSupported, Options2.ProgrammableSamplePositionsTier,
              Options3.WriteBufferImmediateSupportFlags, Options3.ViewInstancingTier, Options5.RenderPassesTier,
              Options5.RaytracingTier);
    WriteImmediate = !!(Options3.WriteBufferImmediateSupportFlags & D3D12_COMMAND_LIST_SUPPORT_FLAG_DIRECT);

    Result = ID3D12Device_QueryInterface(Device, &IID_ID3D12Device4, (void **)&Device4);
    if (FAILED(Result))
        goto Done;

    ZeroMemory(&QueueDesc, sizeof(QueueDesc));
    QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Step = "queue";
    Result = ID3D12Device_CreateCommandQueue(Device, &QueueDesc, &IID_ID3D12CommandQueue, (void **)&Queue);
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateCommandAllocator(Device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                     &IID_ID3D12CommandAllocator, (void **)&Allocator);
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateFence(Device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&Fence);
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateFence(Device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&Second);
    if (FAILED(Result))
        goto Done;
    Event = CreateEventW(NULL, FALSE, FALSE, NULL);

    Step = "buffers";
    ZeroMemory(&HeapProperties, sizeof(HeapProperties));
    HeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
    ZeroMemory(&Desc, sizeof(Desc));
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    Desc.Width = sizeof(Vertices);
    Desc.Height = 1;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.SampleDesc.Count = 1;
    Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Result = ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &Desc,
                                                  D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                                                  &IID_ID3D12Resource, (void **)&VertexBuffer);
    if (SUCCEEDED(Result))
        Result = ID3D12Resource_Map(VertexBuffer, 0, NULL, &Data);
    if (FAILED(Result))
        goto Done;
    CopyMemory(Data, Vertices, sizeof(Vertices));
    ID3D12Resource_Unmap(VertexBuffer, 0, NULL);
    VertexView.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(VertexBuffer);
    VertexView.SizeInBytes = sizeof(Vertices);
    VertexView.StrideInBytes = 4 * sizeof(FLOAT);

    HeapProperties.Type = D3D12_HEAP_TYPE_READBACK;
    Desc.Width = 256;
    Result = ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &Desc,
                                                  D3D12_RESOURCE_STATE_COPY_DEST, NULL,
                                                  &IID_ID3D12Resource, (void **)&Values);
    if (FAILED(Result))
        goto Done;

    Step = "target";
    HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    ZeroMemory(&Desc, sizeof(Desc));
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    Desc.Width = 64;
    Desc.Height = 64;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    Result = ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &Desc,
                                                  D3D12_RESOURCE_STATE_RENDER_TARGET, NULL,
                                                  &IID_ID3D12Resource, (void **)&Target);
    if (FAILED(Result))
        goto Done;
    ID3D12Device_GetCopyableFootprints(Device, &Desc, 0, 1, 0, &Footprint, NULL, NULL, &Total);
    HeapProperties.Type = D3D12_HEAP_TYPE_READBACK;
    ZeroMemory(&Desc, sizeof(Desc));
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    Desc.Width = Total;
    Desc.Height = 1;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.SampleDesc.Count = 1;
    Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Result = ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &Desc,
                                                  D3D12_RESOURCE_STATE_COPY_DEST, NULL,
                                                  &IID_ID3D12Resource, (void **)&Readback);
    if (FAILED(Result))
        goto Done;

    ZeroMemory(&HeapDesc, sizeof(HeapDesc));
    HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    HeapDesc.NumDescriptors = 1;
    Result = ID3D12Device_CreateDescriptorHeap(Device, &HeapDesc, &IID_ID3D12DescriptorHeap, (void **)&Heap);
    if (FAILED(Result))
        goto Done;
    View = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(Heap);
    ID3D12Device_CreateRenderTargetView(Device, Target, NULL, View);

    Step = "stream pipeline";
    Result = D3DCompile(FeatVertexShader, sizeof(FeatVertexShader) - 1, NULL, NULL, NULL, "main", "vs_5_0", 0, 0,
                        &VertexCode, NULL);
    if (SUCCEEDED(Result))
        Result = D3DCompile(FeatPixelShader, sizeof(FeatPixelShader) - 1, NULL, NULL, NULL, "main", "ps_5_0", 0,
                            0, &PixelCode, NULL);
    if (FAILED(Result))
        goto Done;
    ZeroMemory(&SignatureDesc, sizeof(SignatureDesc));
    SignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    Result = D3D12SerializeRootSignature(&SignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &SignatureBlob, NULL);
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateRootSignature(Device, 0, ID3D10Blob_GetBufferPointer(SignatureBlob),
                                                  ID3D10Blob_GetBufferSize(SignatureBlob),
                                                  &IID_ID3D12RootSignature, (void **)&RootSignature);
    if (FAILED(Result))
        goto Done;

    ZeroMemory(Stream, sizeof(Stream));
    FeatStreamAppend(Stream, sizeof(Stream), &StreamSize, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE,
                     &RootSignature, sizeof(RootSignature), sizeof(void *));
    Code.pShaderBytecode = ID3D10Blob_GetBufferPointer(VertexCode);
    Code.BytecodeLength = ID3D10Blob_GetBufferSize(VertexCode);
    FeatStreamAppend(Stream, sizeof(Stream), &StreamSize, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS,
                     &Code, sizeof(Code), sizeof(void *));
    Code.pShaderBytecode = ID3D10Blob_GetBufferPointer(PixelCode);
    Code.BytecodeLength = ID3D10Blob_GetBufferSize(PixelCode);
    FeatStreamAppend(Stream, sizeof(Stream), &StreamSize, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS,
                     &Code, sizeof(Code), sizeof(void *));
    InputLayout.pInputElementDescs = Layout;
    InputLayout.NumElements = RTL_NUMBER_OF(Layout);
    FeatStreamAppend(Stream, sizeof(Stream), &StreamSize, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT,
                     &InputLayout, sizeof(InputLayout), sizeof(void *));
    FeatStreamAppend(Stream, sizeof(Stream), &StreamSize, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY,
                     &Topology, sizeof(Topology), sizeof(UINT));
    ZeroMemory(&Rasterizer, sizeof(Rasterizer));
    Rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
    Rasterizer.CullMode = D3D12_CULL_MODE_NONE;
    Rasterizer.DepthClipEnable = TRUE;
    FeatStreamAppend(Stream, sizeof(Stream), &StreamSize, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER,
                     &Rasterizer, sizeof(Rasterizer), sizeof(UINT));
    ZeroMemory(&Formats, sizeof(Formats));
    Formats.NumRenderTargets = 1;
    Formats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    FeatStreamAppend(Stream, sizeof(Stream), &StreamSize, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS,
                     &Formats, sizeof(Formats), sizeof(UINT));
    StreamDesc.SizeInBytes = StreamSize;
    StreamDesc.pPipelineStateSubobjectStream = Stream;
    Result = ID3D12Device4_CreatePipelineState(Device4, &StreamDesc, &IID_ID3D12PipelineState, (void **)&Pipeline);
    if (FAILED(Result))
        goto Done;

    Step = "list1";
    Result = ID3D12Device4_CreateCommandList1(Device4, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              D3D12_COMMAND_LIST_FLAG_NONE, &IID_ID3D12GraphicsCommandList4,
                                              (void **)&List);
    if (FAILED(Result))
        goto Done;
    for (Index = 0; Index < RTL_NUMBER_OF(ListInterfaces); ++Index)
    {
        HRESULT Query = ID3D12GraphicsCommandList4_QueryInterface(List, ListInterfaces[Index].Iid,
                                                                  (void **)&Unknown);
        FeatPrint("D3DFEAT_D3D12_INTERFACE adapter=%lu name=%s hr=0x%08lx\n",
                  Adapter, ListInterfaces[Index].Name, Query);
        if (SUCCEEDED(Query))
            IUnknown_Release(Unknown);
    }
    Result = ID3D12GraphicsCommandList4_Reset(List, Allocator, Pipeline);
    if (FAILED(Result))
        goto Done;

    Step = "render pass";
    ZeroMemory(&Pass, sizeof(Pass));
    Pass.cpuDescriptor = View;
    Pass.BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
    Pass.BeginningAccess.Clear.ClearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    CopyMemory(Pass.BeginningAccess.Clear.ClearValue.Color, Red, sizeof(Red));
    Pass.EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
    Viewport.TopLeftX = 0.0f;
    Viewport.TopLeftY = 0.0f;
    Viewport.Width = 64.0f;
    Viewport.Height = 64.0f;
    Viewport.MinDepth = 0.0f;
    Viewport.MaxDepth = 1.0f;
    Scissor.left = 0;
    Scissor.top = 0;
    Scissor.right = 32;
    Scissor.bottom = 64;
    ID3D12GraphicsCommandList4_BeginRenderPass(List, 1, &Pass, NULL, D3D12_RENDER_PASS_FLAG_NONE);
    ID3D12GraphicsCommandList4_SetGraphicsRootSignature(List, RootSignature);
    ID3D12GraphicsCommandList4_RSSetViewports(List, 1, &Viewport);
    ID3D12GraphicsCommandList4_RSSetScissorRects(List, 1, &Scissor);
    ID3D12GraphicsCommandList4_IASetPrimitiveTopology(List, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList4_IASetVertexBuffers(List, 0, 1, &VertexView);
    if (Options2.DepthBoundsTestSupported)
        ID3D12GraphicsCommandList4_OMSetDepthBounds(List, 0.0f, 1.0f);
    ID3D12GraphicsCommandList4_DrawInstanced(List, 3, 1, 0, 0);
    ID3D12GraphicsCommandList4_EndRenderPass(List);
    ZeroMemory(&Barrier, sizeof(Barrier));
    Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    Barrier.Transition.pResource = Target;
    Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    ID3D12GraphicsCommandList4_ResourceBarrier(List, 1, &Barrier);
    ZeroMemory(&Source, sizeof(Source));
    Source.pResource = Target;
    Source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    ZeroMemory(&Destination, sizeof(Destination));
    Destination.pResource = Readback;
    Destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    Destination.PlacedFootprint = Footprint;
    ID3D12GraphicsCommandList4_CopyTextureRegion(List, &Destination, 0, 0, 0, &Source, NULL);
    if (WriteImmediate)
    {
        Immediate[0].Dest = ID3D12Resource_GetGPUVirtualAddress(Values);
        Immediate[0].Value = 0x12345678;
        Immediate[1].Dest = Immediate[0].Dest + 4;
        Immediate[1].Value = 0xcafef00d;
        ID3D12GraphicsCommandList4_WriteBufferImmediate(List, 2, Immediate, NULL);
    }
    Result = FeatExecute12(Queue, (ID3D12GraphicsCommandList *)List, Fence, Event, &FenceValue);
    if (FAILED(Result))
        goto Done;

    Step = "multiple fences";
    Result = ID3D12CommandQueue_Signal(Queue, Second, 7);
    if (FAILED(Result))
        goto Done;
    WaitFences[0] = Fence;
    WaitFences[1] = Second;
    WaitValues[0] = FenceValue;
    WaitValues[1] = 7;
    Result = ID3D12Device4_SetEventOnMultipleFenceCompletion(Device4, WaitFences, WaitValues, 2,
                                                             D3D12_MULTIPLE_FENCE_WAIT_FLAG_ALL, Event);
    if (FAILED(Result))
        goto Done;
    Waited = WaitForSingleObject(Event, 5000);

    Step = "map";
    Result = ID3D12Resource_Map(Readback, 0, NULL, &Data);
    if (FAILED(Result))
        goto Done;
    Drawn = *(const DWORD *)((const BYTE *)Data + Footprint.Offset + 32 * Footprint.Footprint.RowPitch + 16 * 4);
    Cleared = *(const DWORD *)((const BYTE *)Data + Footprint.Offset + 32 * Footprint.Footprint.RowPitch + 48 * 4);
    ID3D12Resource_Unmap(Readback, 0, NULL);
    if (WriteImmediate && SUCCEEDED(ID3D12Resource_Map(Values, 0, NULL, &Data)))
    {
        Written[0] = ((const DWORD *)Data)[0];
        Written[1] = ((const DWORD *)Data)[1];
        ID3D12Resource_Unmap(Values, 0, NULL);
    }

Done:
    FeatPrint("D3DFEAT_D3D12_MODERN adapter=%lu step=\"%s\" hr=0x%08lx drawn=0x%08lx cleared=0x%08lx "
              "multi_wait=%lu immediate=0x%08lx,0x%08lx result=%s\n",
              Adapter, Step, Result, Drawn, Cleared, Waited, Written[0], Written[1],
              (SUCCEEDED(Result) && Drawn == 0xff00ff00 && Cleared == 0xff0000ff && Waited == WAIT_OBJECT_0
               && (!WriteImmediate || (Written[0] == 0x12345678 && Written[1] == 0xcafef00d))) ? "pass" : "fail");
    if (Pipeline != NULL) ID3D12PipelineState_Release(Pipeline);
    if (RootSignature != NULL) ID3D12RootSignature_Release(RootSignature);
    if (SignatureBlob != NULL) ID3D10Blob_Release(SignatureBlob);
    if (PixelCode != NULL) ID3D10Blob_Release(PixelCode);
    if (VertexCode != NULL) ID3D10Blob_Release(VertexCode);
    if (List != NULL) ID3D12GraphicsCommandList4_Release(List);
    if (Heap != NULL) ID3D12DescriptorHeap_Release(Heap);
    if (Readback != NULL) ID3D12Resource_Release(Readback);
    if (Target != NULL) ID3D12Resource_Release(Target);
    if (Values != NULL) ID3D12Resource_Release(Values);
    if (VertexBuffer != NULL) ID3D12Resource_Release(VertexBuffer);
    if (Event != NULL) CloseHandle(Event);
    if (Second != NULL) ID3D12Fence_Release(Second);
    if (Fence != NULL) ID3D12Fence_Release(Fence);
    if (Allocator != NULL) ID3D12CommandAllocator_Release(Allocator);
    if (Queue != NULL) ID3D12CommandQueue_Release(Queue);
    if (Device4 != NULL) ID3D12Device4_Release(Device4);
}

static VOID
FeatDirect3D12StreamOutput(
    _In_ ULONG Adapter,
    _In_ ID3D12Device *Device)
{
    static const FLOAT Vertices[] =
    {
        -1.0f, -1.0f, 0.0f, 1.0f,
        -1.0f,  3.0f, 0.0f, 1.0f,
         3.0f, -1.0f, 0.0f, 1.0f,
    };
    static const D3D12_INPUT_ELEMENT_DESC Layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    static const D3D12_SO_DECLARATION_ENTRY Declaration[] =
    {
        { 0, "SV_POSITION", 0, 0, 4, 0 },
    };
    static const UINT Stride = 4 * sizeof(FLOAT);
    ID3D12CommandQueue *Queue = NULL;
    ID3D12CommandAllocator *Allocator = NULL;
    ID3D12GraphicsCommandList *List = NULL;
    ID3D12Fence *Fence = NULL;
    ID3D12Resource *VertexBuffer = NULL, *Zero = NULL, *Output = NULL, *Readback = NULL;
    ID3D12RootSignature *RootSignature = NULL;
    ID3D12PipelineState *Pipeline = NULL;
    ID3DBlob *VertexCode = NULL, *SignatureBlob = NULL;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC PipelineDesc;
    D3D12_ROOT_SIGNATURE_DESC SignatureDesc;
    D3D12_COMMAND_QUEUE_DESC QueueDesc;
    D3D12_HEAP_PROPERTIES HeapProperties;
    D3D12_VERTEX_BUFFER_VIEW VertexView;
    D3D12_STREAM_OUTPUT_BUFFER_VIEW OutputView;
    D3D12_RESOURCE_BARRIER Barrier;
    D3D12_RESOURCE_DESC Desc;
    UINT64 FenceValue = 0, Filled = 0;
    BOOL Match = FALSE;
    HANDLE Event = NULL;
    PCSTR Step = "setup";
    HRESULT Result;
    PVOID Data;

    ZeroMemory(&QueueDesc, sizeof(QueueDesc));
    QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Result = ID3D12Device_CreateCommandQueue(Device, &QueueDesc, &IID_ID3D12CommandQueue, (void **)&Queue);
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateCommandAllocator(Device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                     &IID_ID3D12CommandAllocator, (void **)&Allocator);
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateFence(Device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&Fence);
    if (FAILED(Result))
        goto Done;
    Event = CreateEventW(NULL, FALSE, FALSE, NULL);

    Step = "buffers";
    ZeroMemory(&HeapProperties, sizeof(HeapProperties));
    HeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
    ZeroMemory(&Desc, sizeof(Desc));
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    Desc.Width = 512;
    Desc.Height = 1;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.SampleDesc.Count = 1;
    Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Result = ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &Desc,
                                                  D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                                                  &IID_ID3D12Resource, (void **)&VertexBuffer);
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &Desc,
                                                      D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                                                      &IID_ID3D12Resource, (void **)&Zero);
    if (SUCCEEDED(Result))
        Result = ID3D12Resource_Map(VertexBuffer, 0, NULL, &Data);
    if (FAILED(Result))
        goto Done;
    CopyMemory(Data, Vertices, sizeof(Vertices));
    ID3D12Resource_Unmap(VertexBuffer, 0, NULL);
    if (FAILED(Result = ID3D12Resource_Map(Zero, 0, NULL, &Data)))
        goto Done;
    ZeroMemory(Data, 512);
    ID3D12Resource_Unmap(Zero, 0, NULL);
    HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    Result = ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &Desc,
                                                  D3D12_RESOURCE_STATE_COPY_DEST, NULL,
                                                  &IID_ID3D12Resource, (void **)&Output);
    if (FAILED(Result))
        goto Done;
    HeapProperties.Type = D3D12_HEAP_TYPE_READBACK;
    Result = ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &Desc,
                                                  D3D12_RESOURCE_STATE_COPY_DEST, NULL,
                                                  &IID_ID3D12Resource, (void **)&Readback);
    if (FAILED(Result))
        goto Done;

    Step = "pipeline";
    Result = D3DCompile(FeatVertexShader, sizeof(FeatVertexShader) - 1, NULL, NULL, NULL, "main", "vs_5_0", 0, 0,
                        &VertexCode, NULL);
    if (FAILED(Result))
        goto Done;
    ZeroMemory(&SignatureDesc, sizeof(SignatureDesc));
    SignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                          D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT;
    Result = D3D12SerializeRootSignature(&SignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &SignatureBlob, NULL);
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateRootSignature(Device, 0, ID3D10Blob_GetBufferPointer(SignatureBlob),
                                                  ID3D10Blob_GetBufferSize(SignatureBlob),
                                                  &IID_ID3D12RootSignature, (void **)&RootSignature);
    if (FAILED(Result))
        goto Done;
    ZeroMemory(&PipelineDesc, sizeof(PipelineDesc));
    PipelineDesc.pRootSignature = RootSignature;
    PipelineDesc.VS.pShaderBytecode = ID3D10Blob_GetBufferPointer(VertexCode);
    PipelineDesc.VS.BytecodeLength = ID3D10Blob_GetBufferSize(VertexCode);
    PipelineDesc.StreamOutput.pSODeclaration = Declaration;
    PipelineDesc.StreamOutput.NumEntries = RTL_NUMBER_OF(Declaration);
    PipelineDesc.StreamOutput.pBufferStrides = &Stride;
    PipelineDesc.StreamOutput.NumStrides = 1;
    PipelineDesc.StreamOutput.RasterizedStream = D3D12_SO_NO_RASTERIZED_STREAM;
    PipelineDesc.SampleMask = 0xffffffff;
    PipelineDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    PipelineDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    PipelineDesc.RasterizerState.DepthClipEnable = TRUE;
    PipelineDesc.InputLayout.pInputElementDescs = Layout;
    PipelineDesc.InputLayout.NumElements = RTL_NUMBER_OF(Layout);
    PipelineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    PipelineDesc.SampleDesc.Count = 1;
    Result = ID3D12Device_CreateGraphicsPipelineState(Device, &PipelineDesc, &IID_ID3D12PipelineState,
                                                      (void **)&Pipeline);
    if (FAILED(Result))
        goto Done;

    Step = "draw";
    Result = ID3D12Device_CreateCommandList(Device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, Allocator, Pipeline,
                                            &IID_ID3D12GraphicsCommandList, (void **)&List);
    if (FAILED(Result))
        goto Done;
    ID3D12GraphicsCommandList_CopyBufferRegion(List, Output, 0, Zero, 0, 512);
    ZeroMemory(&Barrier, sizeof(Barrier));
    Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    Barrier.Transition.pResource = Output;
    Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_STREAM_OUT;
    ID3D12GraphicsCommandList_ResourceBarrier(List, 1, &Barrier);
    VertexView.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(VertexBuffer);
    VertexView.SizeInBytes = sizeof(Vertices);
    VertexView.StrideInBytes = Stride;
    OutputView.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(Output);
    OutputView.SizeInBytes = 256;
    OutputView.BufferFilledSizeLocation = OutputView.BufferLocation + 256;
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(List, RootSignature);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(List, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_IASetVertexBuffers(List, 0, 1, &VertexView);
    ID3D12GraphicsCommandList_SOSetTargets(List, 0, 1, &OutputView);
    ID3D12GraphicsCommandList_DrawInstanced(List, 3, 1, 0, 0);
    Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_STREAM_OUT;
    Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    ID3D12GraphicsCommandList_ResourceBarrier(List, 1, &Barrier);
    ID3D12GraphicsCommandList_CopyBufferRegion(List, Readback, 0, Output, 0, 512);
    Result = FeatExecute12(Queue, List, Fence, Event, &FenceValue);
    if (FAILED(Result))
        goto Done;

    Step = "map";
    Result = ID3D12Resource_Map(Readback, 0, NULL, &Data);
    if (FAILED(Result))
        goto Done;
    Match = !memcmp(Data, Vertices, sizeof(Vertices));
    CopyMemory(&Filled, (const BYTE *)Data + 256, sizeof(Filled));
    FeatPrint("D3DFEAT_D3D12_SO_DATA adapter=%lu v0=%08lx,%08lx v2=%08lx,%08lx\n", Adapter,
              ((const DWORD *)Data)[0], ((const DWORD *)Data)[1], ((const DWORD *)Data)[8], ((const DWORD *)Data)[9]);
    ID3D12Resource_Unmap(Readback, 0, NULL);

Done:
    FeatPrint("D3DFEAT_D3D12_STREAM_OUTPUT adapter=%lu step=\"%s\" hr=0x%08lx match=%d filled=%I64u result=%s\n",
              Adapter, Step, Result, Match, Filled,
              (SUCCEEDED(Result) && Match && Filled == sizeof(Vertices)) ? "pass" : "fail");
    if (Pipeline != NULL) ID3D12PipelineState_Release(Pipeline);
    if (RootSignature != NULL) ID3D12RootSignature_Release(RootSignature);
    if (SignatureBlob != NULL) ID3D10Blob_Release(SignatureBlob);
    if (VertexCode != NULL) ID3D10Blob_Release(VertexCode);
    if (List != NULL) ID3D12GraphicsCommandList_Release(List);
    if (Readback != NULL) ID3D12Resource_Release(Readback);
    if (Output != NULL) ID3D12Resource_Release(Output);
    if (Zero != NULL) ID3D12Resource_Release(Zero);
    if (VertexBuffer != NULL) ID3D12Resource_Release(VertexBuffer);
    if (Event != NULL) CloseHandle(Event);
    if (Fence != NULL) ID3D12Fence_Release(Fence);
    if (Allocator != NULL) ID3D12CommandAllocator_Release(Allocator);
    if (Queue != NULL) ID3D12CommandQueue_Release(Queue);
}

static VOID
FeatDirect3D12Subresource(
    _In_ ULONG Adapter,
    _In_ ID3D12Device *Device)
{
    static const D3D12_BOX Patch = { 8, 4, 0, 24, 12, 1 };
    static const D3D12_BOX Empty = { 5, 5, 0, 5, 6, 1 };
    static const D3D12_BOX Bytes = { 16, 0, 0, 48, 1, 1 };
    ID3D12CommandQueue *Queue = NULL;
    ID3D12CommandAllocator *Allocator = NULL;
    ID3D12GraphicsCommandList *List = NULL;
    ID3D12Fence *Fence = NULL;
    ID3D12Resource *Texture = NULL, *Readback = NULL, *Buffer = NULL;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT Footprint;
    D3D12_TEXTURE_COPY_LOCATION Destination, Source;
    D3D12_COMMAND_QUEUE_DESC QueueDesc;
    D3D12_HEAP_PROPERTIES HeapProperties;
    D3D12_RESOURCE_DESC Desc;
    DWORD *Expected = NULL, *Read = NULL, Small[16 * 8], Words[16 * 8];
    BYTE Fill[32];
    UINT64 FenceValue = 0;
    HRESULT WriteResult = E_FAIL, PatchResult = E_FAIL, EmptyResult = E_FAIL, ReadResult = E_FAIL;
    HRESULT BoxResult = E_FAIL, BufferResult = E_FAIL;
    BOOL ReadMatch = FALSE, BoxMatch = FALSE, GpuMatch = FALSE, BufferMatch = FALSE;
    HANDLE Event = NULL;
    PCSTR Step = "setup";
    HRESULT Result;
    PVOID Data;
    UINT X, Y;

    Expected = HeapAlloc(GetProcessHeap(), 0, 64 * 64 * sizeof(DWORD));
    Read = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 64 * 64 * sizeof(DWORD));
    Result = Expected && Read ? S_OK : E_OUTOFMEMORY;
    ZeroMemory(&QueueDesc, sizeof(QueueDesc));
    QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateCommandQueue(Device, &QueueDesc, &IID_ID3D12CommandQueue, (void **)&Queue);
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateCommandAllocator(Device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                     &IID_ID3D12CommandAllocator, (void **)&Allocator);
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateCommandList(Device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, Allocator, NULL,
                                                &IID_ID3D12GraphicsCommandList, (void **)&List);
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateFence(Device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&Fence);
    if (FAILED(Result))
        goto Done;
    Event = CreateEventW(NULL, FALSE, FALSE, NULL);

    Step = "texture";
    ZeroMemory(&HeapProperties, sizeof(HeapProperties));
    HeapProperties.Type = D3D12_HEAP_TYPE_CUSTOM;
    HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
    HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
    ZeroMemory(&Desc, sizeof(Desc));
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    Desc.Width = 64;
    Desc.Height = 64;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Result = ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &Desc,
                                                  D3D12_RESOURCE_STATE_COMMON, NULL, &IID_ID3D12Resource,
                                                  (void **)&Texture);
    if (FAILED(Result))
        goto Done;

    Step = "write";
    for (Y = 0; Y < 64; ++Y)
        for (X = 0; X < 64; ++X)
            Expected[Y * 64 + X] = 0x80000000u | (Y << 16) | (X << 8) | ((X ^ Y) & 0xff);
    for (X = 0; X < 16 * 8; ++X)
        Words[X] = 0x5a000000u | X;
    Result = ID3D12Resource_Map(Texture, 0, NULL, NULL);
    if (FAILED(Result))
        goto Done;
    WriteResult = ID3D12Resource_WriteToSubresource(Texture, 0, NULL, Expected, 64 * 4, 64 * 64 * 4);
    PatchResult = ID3D12Resource_WriteToSubresource(Texture, 0, &Patch, Words, 16 * 4, 16 * 8 * 4);
    EmptyResult = ID3D12Resource_WriteToSubresource(Texture, 0, &Empty, Words, 16 * 4, 16 * 8 * 4);
    for (Y = 0; Y < 8; ++Y)
        CopyMemory(&Expected[(Patch.top + Y) * 64 + Patch.left], &Words[Y * 16], 16 * 4);
    ReadResult = ID3D12Resource_ReadFromSubresource(Texture, Read, 64 * 4, 64 * 64 * 4, 0, NULL);
    BoxResult = ID3D12Resource_ReadFromSubresource(Texture, Small, 16 * 4, 16 * 8 * 4, 0, &Patch);
    ID3D12Resource_Unmap(Texture, 0, NULL);
    ReadMatch = SUCCEEDED(ReadResult) && !memcmp(Read, Expected, 64 * 64 * 4);
    BoxMatch = SUCCEEDED(BoxResult) && !memcmp(Small, Words, sizeof(Words));

    Step = "gpu";
    ID3D12Device_GetCopyableFootprints(Device, &Desc, 0, 1, 0, &Footprint, NULL, NULL, NULL);
    HeapProperties.Type = D3D12_HEAP_TYPE_READBACK;
    HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    ZeroMemory(&Desc, sizeof(Desc));
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    Desc.Width = Footprint.Footprint.RowPitch * 64;
    Desc.Height = 1;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.SampleDesc.Count = 1;
    Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Result = ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &Desc,
                                                  D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource,
                                                  (void **)&Readback);
    if (FAILED(Result))
        goto Done;
    ZeroMemory(&Destination, sizeof(Destination));
    Destination.pResource = Readback;
    Destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    Destination.PlacedFootprint = Footprint;
    ZeroMemory(&Source, sizeof(Source));
    Source.pResource = Texture;
    Source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    ID3D12GraphicsCommandList_CopyTextureRegion(List, &Destination, 0, 0, 0, &Source, NULL);
    Result = FeatExecute12(Queue, List, Fence, Event, &FenceValue);
    if (SUCCEEDED(Result))
        Result = ID3D12Resource_Map(Readback, 0, NULL, &Data);
    if (FAILED(Result))
        goto Done;
    GpuMatch = TRUE;
    for (Y = 0; Y < 64 && GpuMatch; ++Y)
        GpuMatch = !memcmp((const BYTE *)Data + Footprint.Offset + Y * Footprint.Footprint.RowPitch,
                           &Expected[Y * 64], 64 * 4);
    FeatPrint("D3DFEAT_D3D12_SUBRESOURCE_DATA adapter=%lu gpu=%08lx,%08lx cpu=%08lx,%08lx\n", Adapter,
              ((const DWORD *)Data)[0], ((const DWORD *)((const BYTE *)Data + 5 * Footprint.Footprint.RowPitch))[9],
              Read[0], Read[5 * 64 + 9]);
    ID3D12Resource_Unmap(Readback, 0, NULL);

    Step = "buffer";
    HeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
    Desc.Width = 256;
    Result = ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &Desc,
                                                  D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource,
                                                  (void **)&Buffer);
    if (FAILED(Result))
        goto Done;
    for (X = 0; X < sizeof(Fill); ++X)
        Fill[X] = (BYTE)(0xc0 + X);
    BufferResult = ID3D12Resource_WriteToSubresource(Buffer, 0, &Bytes, Fill, sizeof(Fill), sizeof(Fill));
    BufferMatch = BufferResult == E_INVALIDARG;

Done:
    FeatPrint("D3DFEAT_D3D12_SUBRESOURCE adapter=%lu step=\"%s\" hr=0x%08lx write=0x%08lx patch=0x%08lx "
              "empty=0x%08lx read=0x%08lx box=0x%08lx buffer=0x%08lx read_match=%d box_match=%d gpu_match=%d "
              "buffer_match=%d result=%s\n",
              Adapter, Step, Result, WriteResult, PatchResult, EmptyResult, ReadResult, BoxResult, BufferResult,
              ReadMatch, BoxMatch, GpuMatch, BufferMatch,
              (SUCCEEDED(Result) && WriteResult == S_OK && PatchResult == S_OK && EmptyResult == S_OK &&
               ReadMatch && BoxMatch && GpuMatch && BufferMatch) ? "pass" : "fail");
    if (Buffer != NULL) ID3D12Resource_Release(Buffer);
    if (Readback != NULL) ID3D12Resource_Release(Readback);
    if (Texture != NULL) ID3D12Resource_Release(Texture);
    if (List != NULL) ID3D12GraphicsCommandList_Release(List);
    if (Event != NULL) CloseHandle(Event);
    if (Fence != NULL) ID3D12Fence_Release(Fence);
    if (Allocator != NULL) ID3D12CommandAllocator_Release(Allocator);
    if (Queue != NULL) ID3D12CommandQueue_Release(Queue);
    HeapFree(GetProcessHeap(), 0, Read);
    HeapFree(GetProcessHeap(), 0, Expected);
}

static VOID
FeatDirect3D12Shared(
    _In_ ULONG Adapter,
    _In_ IDXGIAdapter1 *DxgiAdapter,
    _In_ ID3D12Device *Device)
{
    static const FLOAT Green[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
    ID3D12CommandQueue *Queue = NULL;
    ID3D12CommandAllocator *Allocator = NULL;
    ID3D12GraphicsCommandList *List = NULL;
    ID3D12Fence *Fence = NULL;
    ID3D12DescriptorHeap *Views = NULL;
    ID3D12Resource *Texture = NULL, *Opened = NULL, *Readback = NULL;
    ID3D11Device *Device11 = NULL;
    ID3D11Device1 *Device11_1 = NULL;
    ID3D11DeviceContext *Context11 = NULL;
    ID3D11Texture2D *Texture11 = NULL, *Staging11 = NULL;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT Footprint;
    D3D12_TEXTURE_COPY_LOCATION Destination, Source;
    D3D12_DESCRIPTOR_HEAP_DESC HeapDesc;
    D3D12_COMMAND_QUEUE_DESC QueueDesc;
    D3D12_HEAP_PROPERTIES HeapProperties;
    D3D12_RESOURCE_BARRIER Barrier;
    D3D12_RESOURCE_DESC Desc;
    D3D12_CPU_DESCRIPTOR_HANDLE View;
    D3D11_TEXTURE2D_DESC Desc11;
    HRESULT ShareResult = E_FAIL, OpenResult = E_FAIL, Open11Result = E_FAIL;
    DWORD Pixel12 = 0, Pixel11 = 0;
    UINT64 FenceValue = 0;
    HANDLE Event = NULL, Shared = NULL;
    PCSTR Step = "setup";
    HRESULT Result;
    PVOID Data;

    ZeroMemory(&QueueDesc, sizeof(QueueDesc));
    QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Result = ID3D12Device_CreateCommandQueue(Device, &QueueDesc, &IID_ID3D12CommandQueue, (void **)&Queue);
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateCommandAllocator(Device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                     &IID_ID3D12CommandAllocator, (void **)&Allocator);
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateCommandList(Device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, Allocator, NULL,
                                                &IID_ID3D12GraphicsCommandList, (void **)&List);
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateFence(Device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&Fence);
    ZeroMemory(&HeapDesc, sizeof(HeapDesc));
    HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    HeapDesc.NumDescriptors = 1;
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateDescriptorHeap(Device, &HeapDesc, &IID_ID3D12DescriptorHeap, (void **)&Views);
    if (FAILED(Result))
        goto Done;
    Event = CreateEventW(NULL, FALSE, FALSE, NULL);

    Step = "texture";
    ZeroMemory(&HeapProperties, sizeof(HeapProperties));
    HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    ZeroMemory(&Desc, sizeof(Desc));
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    Desc.Width = 64;
    Desc.Height = 64;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    Result = ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_SHARED, &Desc,
                                                  D3D12_RESOURCE_STATE_COMMON, NULL, &IID_ID3D12Resource,
                                                  (void **)&Texture);
    if (FAILED(Result))
        goto Done;
    View = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(Views);
    ID3D12Device_CreateRenderTargetView(Device, Texture, NULL, View);
    ZeroMemory(&Barrier, sizeof(Barrier));
    Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    Barrier.Transition.pResource = Texture;
    Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    ID3D12GraphicsCommandList_ResourceBarrier(List, 1, &Barrier);
    ID3D12GraphicsCommandList_ClearRenderTargetView(List, View, Green, 0, NULL);
    Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    ID3D12GraphicsCommandList_ResourceBarrier(List, 1, &Barrier);
    Result = FeatExecute12(Queue, List, Fence, Event, &FenceValue);
    if (FAILED(Result))
        goto Done;

    Step = "share";
    ShareResult = ID3D12Device_CreateSharedHandle(Device, (ID3D12DeviceChild *)Texture, NULL, GENERIC_ALL, NULL, &Shared);
    if (FAILED(ShareResult))
        goto Done;

    Step = "open12";
    OpenResult = ID3D12Device_OpenSharedHandle(Device, Shared, &IID_ID3D12Resource, (void **)&Opened);
    if (FAILED(OpenResult))
        goto Done;
    ID3D12Device_GetCopyableFootprints(Device, &Desc, 0, 1, 0, &Footprint, NULL, NULL, NULL);
    HeapProperties.Type = D3D12_HEAP_TYPE_READBACK;
    ZeroMemory(&Desc, sizeof(Desc));
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    Desc.Width = Footprint.Footprint.RowPitch * 64;
    Desc.Height = 1;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.SampleDesc.Count = 1;
    Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Result = ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &Desc,
                                                  D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource,
                                                  (void **)&Readback);
    if (SUCCEEDED(Result))
        Result = ID3D12CommandAllocator_Reset(Allocator);
    if (SUCCEEDED(Result))
        Result = ID3D12GraphicsCommandList_Reset(List, Allocator, NULL);
    if (FAILED(Result))
        goto Done;
    ZeroMemory(&Destination, sizeof(Destination));
    Destination.pResource = Readback;
    Destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    Destination.PlacedFootprint = Footprint;
    ZeroMemory(&Source, sizeof(Source));
    Source.pResource = Opened;
    Source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    ID3D12GraphicsCommandList_CopyTextureRegion(List, &Destination, 0, 0, 0, &Source, NULL);
    Result = FeatExecute12(Queue, List, Fence, Event, &FenceValue);
    if (SUCCEEDED(Result))
        Result = ID3D12Resource_Map(Readback, 0, NULL, &Data);
    if (FAILED(Result))
        goto Done;
    Pixel12 = *(const DWORD *)((const BYTE *)Data + Footprint.Offset + 10 * Footprint.Footprint.RowPitch + 10 * 4);
    ID3D12Resource_Unmap(Readback, 0, NULL);

    Step = "open11";
    Open11Result = D3D11CreateDevice((IDXGIAdapter *)DxgiAdapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, NULL, 0,
                                     D3D11_SDK_VERSION, &Device11, NULL, &Context11);
    if (SUCCEEDED(Open11Result))
        Open11Result = ID3D11Device_QueryInterface(Device11, &IID_ID3D11Device1, (void **)&Device11_1);
    if (SUCCEEDED(Open11Result))
        Open11Result = ID3D11Device1_OpenSharedResource1(Device11_1, Shared, &IID_ID3D11Texture2D, (void **)&Texture11);
    if (FAILED(Open11Result))
        goto Done;
    ZeroMemory(&Desc11, sizeof(Desc11));
    Desc11.Width = 64;
    Desc11.Height = 64;
    Desc11.MipLevels = 1;
    Desc11.ArraySize = 1;
    Desc11.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    Desc11.SampleDesc.Count = 1;
    Desc11.Usage = D3D11_USAGE_STAGING;
    Desc11.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (SUCCEEDED(ID3D11Device_CreateTexture2D(Device11, &Desc11, NULL, &Staging11)))
        Pixel11 = FeatReadPixel(Context11, Texture11, Staging11, 10, 10);

Done:
    FeatPrint("D3DFEAT_D3D12_SHARED_NT adapter=%lu step=\"%s\" hr=0x%08lx share=0x%08lx open12=0x%08lx "
              "open11=0x%08lx pixel12=0x%08lx pixel11=0x%08lx result=%s\n",
              Adapter, Step, Result, ShareResult, OpenResult, Open11Result, Pixel12, Pixel11,
              (SUCCEEDED(ShareResult) && SUCCEEDED(OpenResult) && SUCCEEDED(Open11Result) &&
               Pixel12 == 0xff00ff00 && Pixel11 == 0xff00ff00) ? "pass" : "fail");
    if (Staging11 != NULL) ID3D11Texture2D_Release(Staging11);
    if (Texture11 != NULL) ID3D11Texture2D_Release(Texture11);
    if (Context11 != NULL) ID3D11DeviceContext_Release(Context11);
    if (Device11_1 != NULL) ID3D11Device1_Release(Device11_1);
    if (Device11 != NULL) ID3D11Device_Release(Device11);
    if (Shared != NULL) CloseHandle(Shared);
    if (Readback != NULL) ID3D12Resource_Release(Readback);
    if (Opened != NULL) ID3D12Resource_Release(Opened);
    if (Texture != NULL) ID3D12Resource_Release(Texture);
    if (Views != NULL) ID3D12DescriptorHeap_Release(Views);
    if (List != NULL) ID3D12GraphicsCommandList_Release(List);
    if (Event != NULL) CloseHandle(Event);
    if (Fence != NULL) ID3D12Fence_Release(Fence);
    if (Allocator != NULL) ID3D12CommandAllocator_Release(Allocator);
    if (Queue != NULL) ID3D12CommandQueue_Release(Queue);
}

static VOID
FeatDirect3D11On12(
    _In_ ULONG Adapter,
    _In_ ID3D12Device *Device)
{
    static const FLOAT Blue[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    ID3D12CommandQueue *Queue = NULL;
    ID3D12CommandAllocator *Allocator = NULL;
    ID3D12GraphicsCommandList *List = NULL;
    ID3D12Fence *Fence = NULL;
    ID3D12Resource *Texture = NULL, *Readback = NULL;
    ID3D12Device *Device12 = NULL;
    ID3D11Device *Device11 = NULL;
    ID3D11DeviceContext *Context11 = NULL;
    ID3D11On12Device1 *On12 = NULL;
    ID3D11Texture2D *Texture11 = NULL;
    ID3D11RenderTargetView *View11 = NULL;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT Footprint;
    D3D12_TEXTURE_COPY_LOCATION Destination, Source;
    D3D12_COMMAND_QUEUE_DESC QueueDesc;
    D3D12_HEAP_PROPERTIES HeapProperties;
    D3D12_RESOURCE_DESC Desc;
    D3D11_RESOURCE_FLAGS Flags11;
    D3D_FEATURE_LEVEL Level = 0;
    HRESULT CreateResult = E_FAIL, WrapResult = E_FAIL;
    DWORD Pixel = 0;
    UINT64 FenceValue = 0;
    HANDLE Event = NULL;
    PCSTR Step = "setup";
    HRESULT Result;
    PVOID Data;

    ZeroMemory(&QueueDesc, sizeof(QueueDesc));
    QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Result = ID3D12Device_CreateCommandQueue(Device, &QueueDesc, &IID_ID3D12CommandQueue, (void **)&Queue);
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateCommandAllocator(Device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                     &IID_ID3D12CommandAllocator, (void **)&Allocator);
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateCommandList(Device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, Allocator, NULL,
                                                &IID_ID3D12GraphicsCommandList, (void **)&List);
    if (SUCCEEDED(Result))
        Result = ID3D12GraphicsCommandList_Close(List);
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateFence(Device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&Fence);
    if (FAILED(Result))
        goto Done;
    Event = CreateEventW(NULL, FALSE, FALSE, NULL);

    Step = "texture";
    ZeroMemory(&HeapProperties, sizeof(HeapProperties));
    HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    ZeroMemory(&Desc, sizeof(Desc));
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    Desc.Width = 64;
    Desc.Height = 64;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    Result = ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_SHARED, &Desc,
                                                  D3D12_RESOURCE_STATE_COMMON, NULL, &IID_ID3D12Resource,
                                                  (void **)&Texture);
    if (FAILED(Result))
        goto Done;
    ID3D12Device_GetCopyableFootprints(Device, &Desc, 0, 1, 0, &Footprint, NULL, NULL, NULL);

    Step = "create";
    CreateResult = D3D11On12CreateDevice((IUnknown *)Device, 0, NULL, 0, (IUnknown *const *)&Queue, 1, 0,
                                         &Device11, &Context11, &Level);
    if (SUCCEEDED(CreateResult))
        CreateResult = ID3D11Device_QueryInterface(Device11, &IID_ID3D11On12Device1, (void **)&On12);
    if (SUCCEEDED(CreateResult))
        CreateResult = ID3D11On12Device1_GetD3D12Device(On12, &IID_ID3D12Device, &Device12);
    if (SUCCEEDED(CreateResult) && Device12 != Device)
        CreateResult = E_UNEXPECTED;
    if (FAILED(CreateResult))
        goto Done;

    Step = "wrap";
    ZeroMemory(&Flags11, sizeof(Flags11));
    Flags11.BindFlags = D3D11_BIND_RENDER_TARGET;
    WrapResult = ID3D11On12Device1_CreateWrappedResource(On12, (IUnknown *)Texture, &Flags11,
                                                         D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON,
                                                         &IID_ID3D11Texture2D, (void **)&Texture11);
    if (SUCCEEDED(WrapResult))
        WrapResult = ID3D11Device_CreateRenderTargetView(Device11, (ID3D11Resource *)Texture11, NULL, &View11);
    if (FAILED(WrapResult))
        goto Done;
    ID3D11On12Device1_AcquireWrappedResources(On12, (ID3D11Resource *const *)&Texture11, 1);
    ID3D11DeviceContext_ClearRenderTargetView(Context11, View11, Blue);
    ID3D11On12Device1_ReleaseWrappedResources(On12, (ID3D11Resource *const *)&Texture11, 1);

    Step = "readback";
    HeapProperties.Type = D3D12_HEAP_TYPE_READBACK;
    ZeroMemory(&Desc, sizeof(Desc));
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    Desc.Width = Footprint.Footprint.RowPitch * 64;
    Desc.Height = 1;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.SampleDesc.Count = 1;
    Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Result = ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &Desc,
                                                  D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource,
                                                  (void **)&Readback);
    if (SUCCEEDED(Result))
        Result = ID3D12CommandAllocator_Reset(Allocator);
    if (SUCCEEDED(Result))
        Result = ID3D12GraphicsCommandList_Reset(List, Allocator, NULL);
    if (FAILED(Result))
        goto Done;
    ZeroMemory(&Destination, sizeof(Destination));
    Destination.pResource = Readback;
    Destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    Destination.PlacedFootprint = Footprint;
    ZeroMemory(&Source, sizeof(Source));
    Source.pResource = Texture;
    Source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    ID3D12GraphicsCommandList_CopyTextureRegion(List, &Destination, 0, 0, 0, &Source, NULL);
    Result = FeatExecute12(Queue, List, Fence, Event, &FenceValue);
    if (SUCCEEDED(Result))
        Result = ID3D12Resource_Map(Readback, 0, NULL, &Data);
    if (FAILED(Result))
        goto Done;
    Pixel = *(const DWORD *)((const BYTE *)Data + Footprint.Offset + 10 * Footprint.Footprint.RowPitch + 10 * 4);
    ID3D12Resource_Unmap(Readback, 0, NULL);

Done:
    FeatPrint("D3DFEAT_D3D11ON12 adapter=%lu step=\"%s\" hr=0x%08lx create=0x%08lx level=0x%04x wrap=0x%08lx "
              "pixel=0x%08lx result=%s\n",
              Adapter, Step, Result, CreateResult, Level, WrapResult, Pixel,
              (SUCCEEDED(CreateResult) && SUCCEEDED(WrapResult) && SUCCEEDED(Result) && Pixel == 0xff0000ff)
              ? "pass" : "fail");
    if (View11 != NULL) ID3D11RenderTargetView_Release(View11);
    if (Texture11 != NULL) ID3D11Texture2D_Release(Texture11);
    if (On12 != NULL) ID3D11On12Device1_Release(On12);
    if (Device12 != NULL) ID3D12Device_Release(Device12);
    if (Context11 != NULL) ID3D11DeviceContext_Release(Context11);
    if (Device11 != NULL) ID3D11Device_Release(Device11);
    if (Readback != NULL) ID3D12Resource_Release(Readback);
    if (Texture != NULL) ID3D12Resource_Release(Texture);
    if (List != NULL) ID3D12GraphicsCommandList_Release(List);
    if (Event != NULL) CloseHandle(Event);
    if (Fence != NULL) ID3D12Fence_Release(Fence);
    if (Allocator != NULL) ID3D12CommandAllocator_Release(Allocator);
    if (Queue != NULL) ID3D12CommandQueue_Release(Queue);
}

static VOID
FeatDirect3D12ArrayCopy(
    _In_ ULONG Adapter,
    _In_ ID3D12Device *Device,
    _In_ BOOL Depth)
{
    static const FLOAT Color[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
    ID3D12CommandQueue *Queue = NULL;
    ID3D12CommandAllocator *Allocator = NULL;
    ID3D12GraphicsCommandList *List = NULL;
    ID3D12Fence *Fence = NULL;
    ID3D12DescriptorHeap *Views = NULL;
    ID3D12Resource *Texture = NULL, *Readback = NULL;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT Footprint;
    D3D12_TEXTURE_COPY_LOCATION Destination, Source;
    D3D12_DESCRIPTOR_HEAP_DESC HeapDesc;
    D3D12_COMMAND_QUEUE_DESC QueueDesc;
    D3D12_HEAP_PROPERTIES HeapProperties;
    D3D12_RESOURCE_BARRIER Barrier;
    D3D12_RESOURCE_DESC Desc, BufferDesc;
    D3D12_CPU_DESCRIPTOR_HANDLE View;
    D3D12_CLEAR_VALUE Clear;
    UINT64 FenceValue = 0;
    HANDLE Event = NULL;
    HRESULT Result;
    PVOID Data;
    UINT Slice;

    ZeroMemory(&QueueDesc, sizeof(QueueDesc));
    QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Result = ID3D12Device_CreateCommandQueue(Device, &QueueDesc, &IID_ID3D12CommandQueue, (void **)&Queue);
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateCommandAllocator(Device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                     &IID_ID3D12CommandAllocator, (void **)&Allocator);
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateCommandList(Device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, Allocator, NULL,
                                                &IID_ID3D12GraphicsCommandList, (void **)&List);
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateFence(Device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&Fence);
    ZeroMemory(&HeapDesc, sizeof(HeapDesc));
    HeapDesc.Type = Depth ? D3D12_DESCRIPTOR_HEAP_TYPE_DSV : D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    HeapDesc.NumDescriptors = 1;
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateDescriptorHeap(Device, &HeapDesc, &IID_ID3D12DescriptorHeap, (void **)&Views);
    Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    ZeroMemory(&HeapProperties, sizeof(HeapProperties));
    HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    ZeroMemory(&Desc, sizeof(Desc));
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    Desc.Width = 32;
    Desc.Height = 32;
    Desc.DepthOrArraySize = 6;
    Desc.MipLevels = 1;
    Desc.Format = Depth ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.Flags = Depth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ZeroMemory(&Clear, sizeof(Clear));
    Clear.Format = Desc.Format;
    Clear.DepthStencil.Depth = 0.5f;
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &Desc,
                                                      Depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE
                                                            : D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                      &Clear, &IID_ID3D12Resource, (void **)&Texture);
    FeatPrint("D3DFEAT_ARRAYCOPY depth=%d step=create hr=0x%08lx\n", Depth, Result);
    if (FAILED(Result))
        goto Done;
    View = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(Views);
    if (Depth)
    {
        ID3D12Device_CreateDepthStencilView(Device, Texture, NULL, View);
        ID3D12GraphicsCommandList_ClearDepthStencilView(List, View, D3D12_CLEAR_FLAG_DEPTH, 0.25f, 0, 0, NULL);
    }
    else
    {
        ID3D12Device_CreateRenderTargetView(Device, Texture, NULL, View);
        ID3D12GraphicsCommandList_ClearRenderTargetView(List, View, Color, 0, NULL);
    }
    ZeroMemory(&Barrier, sizeof(Barrier));
    Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    Barrier.Transition.pResource = Texture;
    Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    Barrier.Transition.StateBefore = Depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET;
    Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    ID3D12GraphicsCommandList_ResourceBarrier(List, 1, &Barrier);
    Result = FeatExecute12(Queue, List, Fence, Event, &FenceValue);
    FeatPrint("D3DFEAT_ARRAYCOPY depth=%d step=clear hr=0x%08lx removed=0x%08lx\n", Depth, Result,
              ID3D12Device_GetDeviceRemovedReason(Device));
    if (FAILED(Result))
        goto Done;

    ID3D12Device_GetCopyableFootprints(Device, &Desc, 0, 1, 0, &Footprint, NULL, NULL, NULL);
    HeapProperties.Type = D3D12_HEAP_TYPE_READBACK;
    ZeroMemory(&BufferDesc, sizeof(BufferDesc));
    BufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    BufferDesc.Width = Footprint.Footprint.RowPitch * 32;
    BufferDesc.Height = 1;
    BufferDesc.DepthOrArraySize = 1;
    BufferDesc.MipLevels = 1;
    BufferDesc.SampleDesc.Count = 1;
    BufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Result = ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &BufferDesc,
                                                  D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource,
                                                  (void **)&Readback);
    for (Slice = 0; SUCCEEDED(Result) && Slice < 6; ++Slice)
    {
        ID3D12Device_GetCopyableFootprints(Device, &Desc, Slice, 1, 0, &Footprint, NULL, NULL, NULL);
        Footprint.Offset = 0;
        Result = ID3D12CommandAllocator_Reset(Allocator);
        if (SUCCEEDED(Result))
            Result = ID3D12GraphicsCommandList_Reset(List, Allocator, NULL);
        if (FAILED(Result))
            break;
        ZeroMemory(&Destination, sizeof(Destination));
        Destination.pResource = Readback;
        Destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        Destination.PlacedFootprint = Footprint;
        ZeroMemory(&Source, sizeof(Source));
        Source.pResource = Texture;
        Source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        Source.SubresourceIndex = Slice;
        ID3D12GraphicsCommandList_CopyTextureRegion(List, &Destination, 0, 0, 0, &Source, NULL);
        Result = FeatExecute12(Queue, List, Fence, Event, &FenceValue);
        if (SUCCEEDED(Result) && SUCCEEDED(ID3D12Resource_Map(Readback, 0, NULL, &Data)))
        {
            FeatPrint("D3DFEAT_ARRAYCOPY depth=%d step=slice%u hr=0x%08lx value=0x%08lx format=%u pitch=%u\n",
                      Depth, Slice, Result, *(const DWORD *)Data, Footprint.Footprint.Format,
                      Footprint.Footprint.RowPitch);
            ID3D12Resource_Unmap(Readback, 0, NULL);
        }
        else
        {
            FeatPrint("D3DFEAT_ARRAYCOPY depth=%d step=slice%u hr=0x%08lx removed=0x%08lx\n", Depth, Slice,
                      Result, ID3D12Device_GetDeviceRemovedReason(Device));
        }
    }

Done:
    if (Readback != NULL) ID3D12Resource_Release(Readback);
    if (Texture != NULL) ID3D12Resource_Release(Texture);
    if (Views != NULL) ID3D12DescriptorHeap_Release(Views);
    if (List != NULL) ID3D12GraphicsCommandList_Release(List);
    if (Event != NULL) CloseHandle(Event);
    if (Fence != NULL) ID3D12Fence_Release(Fence);
    if (Allocator != NULL) ID3D12CommandAllocator_Release(Allocator);
    if (Queue != NULL) ID3D12CommandQueue_Release(Queue);
}

static VOID
FeatDirect3D12ClearTypeless(
    _In_ ULONG AdapterIndex,
    _In_ IDXGIAdapter1 *Adapter,
    _In_ ULONG Variant)
{
    static const FLOAT Color[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
    static const struct
    {
        DXGI_FORMAT Resource;
        DXGI_FORMAT View;
        BOOL Clear;
    } Variants[] =
    {
        { DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, TRUE },
        { DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM, TRUE },
        { DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, FALSE },
        { DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM, FALSE },
        { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM, TRUE },
    };
    ID3D12Device *Device = NULL;
    ID3D12CommandQueue *Queue = NULL;
    ID3D12CommandAllocator *Allocator = NULL;
    ID3D12GraphicsCommandList *List = NULL;
    ID3D12Fence *Fence = NULL;
    ID3D12DescriptorHeap *Views = NULL;
    ID3D12Resource *Texture = NULL, *Readback = NULL;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT Footprint;
    D3D12_TEXTURE_COPY_LOCATION Destination, Source;
    D3D12_DESCRIPTOR_HEAP_DESC HeapDesc;
    D3D12_COMMAND_QUEUE_DESC QueueDesc;
    D3D12_HEAP_PROPERTIES HeapProperties;
    D3D12_RENDER_TARGET_VIEW_DESC ViewDesc;
    D3D12_RESOURCE_BARRIER Barrier;
    D3D12_RESOURCE_DESC Desc, BufferDesc;
    D3D12_CPU_DESCRIPTOR_HANDLE View;
    D3D12_CLEAR_VALUE Clear;
    UINT64 FenceValue = 0;
    HANDLE Event = NULL;
    HRESULT Result;
    PVOID Data;
    BOOL Single = Variant >= RTL_NUMBER_OF(Variants);

    if (Single)
        Variant -= RTL_NUMBER_OF(Variants);
    if (Variant >= RTL_NUMBER_OF(Variants))
        return;
    Result = D3D12CreateDevice((IUnknown *)Adapter, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&Device);
    if (FAILED(Result))
        return;
    ZeroMemory(&QueueDesc, sizeof(QueueDesc));
    QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Result = ID3D12Device_CreateCommandQueue(Device, &QueueDesc, &IID_ID3D12CommandQueue, (void **)&Queue);
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateCommandAllocator(Device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                     &IID_ID3D12CommandAllocator, (void **)&Allocator);
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateCommandList(Device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, Allocator, NULL,
                                                &IID_ID3D12GraphicsCommandList, (void **)&List);
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateFence(Device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&Fence);
    ZeroMemory(&HeapDesc, sizeof(HeapDesc));
    HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    HeapDesc.NumDescriptors = 1;
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateDescriptorHeap(Device, &HeapDesc, &IID_ID3D12DescriptorHeap, (void **)&Views);
    Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    ZeroMemory(&HeapProperties, sizeof(HeapProperties));
    HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    ZeroMemory(&Desc, sizeof(Desc));
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    Desc.Width = 32;
    Desc.Height = 32;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.Format = Variants[Variant].Resource;
    Desc.SampleDesc.Count = 1;
    Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ZeroMemory(&Clear, sizeof(Clear));
    Clear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Clear.Color[0] = 1.0f;
    Clear.Color[3] = 1.0f;
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &Desc,
                                                      D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                      Variants[Variant].Clear ? &Clear : NULL,
                                                      &IID_ID3D12Resource, (void **)&Texture);
    FeatPrint("D3DFEAT_TYPELESS variant=%lu step=create hr=0x%08lx\n", Variant, Result);
    if (FAILED(Result))
        goto Done;
    View = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(Views);
    ZeroMemory(&ViewDesc, sizeof(ViewDesc));
    ViewDesc.Format = Variants[Variant].View;
    ViewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    ID3D12Device_CreateRenderTargetView(Device, Texture, &ViewDesc, View);
    ID3D12GraphicsCommandList_ClearRenderTargetView(List, View, Color, 0, NULL);
    ZeroMemory(&Barrier, sizeof(Barrier));
    Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    Barrier.Transition.pResource = Texture;
    Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    ID3D12GraphicsCommandList_ResourceBarrier(List, 1, &Barrier);
    if (!Single)
    {
        Result = FeatExecute12(Queue, List, Fence, Event, &FenceValue);
        FeatPrint("D3DFEAT_TYPELESS variant=%lu step=clear hr=0x%08lx removed=0x%08lx\n", Variant, Result,
                  ID3D12Device_GetDeviceRemovedReason(Device));
        if (FAILED(Result))
            goto Done;
    }

    ID3D12Device_GetCopyableFootprints(Device, &Desc, 0, 1, 0, &Footprint, NULL, NULL, NULL);
    HeapProperties.Type = D3D12_HEAP_TYPE_READBACK;
    ZeroMemory(&BufferDesc, sizeof(BufferDesc));
    BufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    BufferDesc.Width = Footprint.Footprint.RowPitch * 32;
    BufferDesc.Height = 1;
    BufferDesc.DepthOrArraySize = 1;
    BufferDesc.MipLevels = 1;
    BufferDesc.SampleDesc.Count = 1;
    BufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Result = ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &BufferDesc,
                                                  D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource,
                                                  (void **)&Readback);
    if (SUCCEEDED(Result) && !Single)
        Result = ID3D12CommandAllocator_Reset(Allocator);
    if (SUCCEEDED(Result) && !Single)
        Result = ID3D12GraphicsCommandList_Reset(List, Allocator, NULL);
    if (FAILED(Result))
        goto Done;
    ZeroMemory(&Destination, sizeof(Destination));
    Destination.pResource = Readback;
    Destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    Destination.PlacedFootprint = Footprint;
    ZeroMemory(&Source, sizeof(Source));
    Source.pResource = Texture;
    Source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    ID3D12GraphicsCommandList_CopyTextureRegion(List, &Destination, 0, 0, 0, &Source, NULL);
    Result = FeatExecute12(Queue, List, Fence, Event, &FenceValue);
    if (SUCCEEDED(Result) && SUCCEEDED(ID3D12Resource_Map(Readback, 0, NULL, &Data)))
    {
        FeatPrint("D3DFEAT_TYPELESS variant=%lu single=%d step=copy hr=0x%08lx value=0x%08lx format=%u\n", Variant,
                  Single, Result, *(const DWORD *)Data, Footprint.Footprint.Format);
        ID3D12Resource_Unmap(Readback, 0, NULL);
    }
    else
    {
        FeatPrint("D3DFEAT_TYPELESS variant=%lu single=%d step=copy hr=0x%08lx removed=0x%08lx\n", Variant,
                  Single, Result, ID3D12Device_GetDeviceRemovedReason(Device));
    }

Done:
    if (Readback != NULL) ID3D12Resource_Release(Readback);
    if (Texture != NULL) ID3D12Resource_Release(Texture);
    if (Views != NULL) ID3D12DescriptorHeap_Release(Views);
    if (List != NULL) ID3D12GraphicsCommandList_Release(List);
    if (Event != NULL) CloseHandle(Event);
    if (Fence != NULL) ID3D12Fence_Release(Fence);
    if (Allocator != NULL) ID3D12CommandAllocator_Release(Allocator);
    if (Queue != NULL) ID3D12CommandQueue_Release(Queue);
    ID3D12Device_Release(Device);
}

static VOID
FeatDirect3D12Placed(
    _In_ IDXGIAdapter1 *Adapter,
    _In_ ULONG Variant)
{
    ID3D12Device *Device = NULL;
    ID3D12Heap *Heap = NULL;
    ID3D12Resource *First = NULL, *Second = NULL;
    D3D12_HEAP_DESC HeapDesc;
    D3D12_RESOURCE_DESC Desc;
    HRESULT Result;

    Result = D3D12CreateDevice((IUnknown *)Adapter, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&Device);
    if (FAILED(Result))
        return;
    ZeroMemory(&HeapDesc, sizeof(HeapDesc));
    HeapDesc.SizeInBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT * 2;
    HeapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    HeapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    Result = ID3D12Device_CreateHeap(Device, &HeapDesc, &IID_ID3D12Heap, (void **)&Heap);
    FeatPrint("D3DFEAT_PLACED variant=%lu step=heap hr=0x%08lx\n", Variant, Result);
    if (FAILED(Result))
        goto Done;
    ZeroMemory(&Desc, sizeof(Desc));
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    Desc.Width = 32;
    Desc.Height = 1;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.SampleDesc.Count = 1;
    Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (Variant >= 2)
    {
        D3D12_CLEAR_VALUE Clear;
        ID3D12Resource *Temp = NULL;

        Result = ID3D12Device_CreatePlacedResource(Device, Heap, 0, &Desc, D3D12_RESOURCE_STATE_COMMON, NULL,
                                                   &IID_ID3D12Resource, (void **)&Temp);
        FeatPrint("D3DFEAT_PLACED variant=%lu step=temp hr=0x%08lx va=%I64x\n", Variant, Result,
                  SUCCEEDED(Result) ? ID3D12Resource_GetGPUVirtualAddress(Temp) : 0);
        if (Temp != NULL)
            ID3D12Resource_Release(Temp);
        if (Variant == 3)
        {
            ZeroMemory(&Clear, sizeof(Clear));
            Clear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            Result = ID3D12Device_CreatePlacedResource(Device, Heap, 0, &Desc, D3D12_RESOURCE_STATE_COMMON, &Clear,
                                                       &IID_ID3D12Resource, (void **)&Temp);
            FeatPrint("D3DFEAT_PLACED variant=%lu step=clear hr=0x%08lx\n", Variant, Result);
            Result = ID3D12Device_CreatePlacedResource(Device, Heap, HeapDesc.SizeInBytes, &Desc,
                                                       D3D12_RESOURCE_STATE_COMMON, NULL, &IID_ID3D12Resource,
                                                       (void **)&Temp);
            FeatPrint("D3DFEAT_PLACED variant=%lu step=large hr=0x%08lx\n", Variant, Result);
        }
    }
    if (Variant >= 1)
    {
        Result = ID3D12Device_CreatePlacedResource(Device, Heap, 0, &Desc, D3D12_RESOURCE_STATE_COMMON, NULL,
                                                   &IID_ID3D12Resource, (void **)&First);
        FeatPrint("D3DFEAT_PLACED variant=%lu step=first hr=0x%08lx\n", Variant, Result);
        ID3D12Heap_Release(Heap);
        if (FAILED(Result))
        {
            Heap = NULL;
            goto Done;
        }
    }
    Result = ID3D12Device_CreatePlacedResource(Device, Heap, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT, &Desc,
                                               D3D12_RESOURCE_STATE_COMMON, NULL, &IID_ID3D12Resource,
                                               (void **)&Second);
    FeatPrint("D3DFEAT_PLACED variant=%lu step=second hr=0x%08lx\n", Variant, Result);
    if (Variant >= 1)
        Heap = NULL;

Done:
    if (Second != NULL) ID3D12Resource_Release(Second);
    if (First != NULL) ID3D12Resource_Release(First);
    if (Heap != NULL) ID3D12Heap_Release(Heap);
    ID3D12Device_Release(Device);
}

static LRESULT CALLBACK
FeatWindowProc(
    _In_ HWND Window,
    _In_ UINT Message,
    _In_ WPARAM WParam,
    _In_ LPARAM LParam)
{
    return DefWindowProcW(Window, Message, WParam, LParam);
}

static VOID
FeatPumpMessages(VOID)
{
    MSG Message;

    while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&Message);
        DispatchMessageW(&Message);
    }
}

static VOID
FeatDirect3D12Present(
    _In_ ULONG Adapter,
    _In_ ID3D12Device *Device)
{
    static const FLOAT Magenta[4] = { 1.0f, 0.0f, 1.0f, 1.0f };
    ID3D12CommandQueue *Queue = NULL;
    ID3D12CommandAllocator *Allocator = NULL;
    ID3D12GraphicsCommandList *List = NULL;
    ID3D12Fence *Fence = NULL;
    ID3D12DescriptorHeap *Heap = NULL;
    ID3D12Resource *Buffers[2] = { NULL, NULL };
    IDXGIFactory2 *Factory = NULL;
    IDXGISwapChain1 *SwapChain = NULL;
    IDXGISwapChain3 *SwapChain3 = NULL;
    D3D12_COMMAND_QUEUE_DESC QueueDesc;
    D3D12_DESCRIPTOR_HEAP_DESC HeapDesc;
    D3D12_CPU_DESCRIPTOR_HANDLE Views[2];
    D3D12_RESOURCE_BARRIER Barrier;
    DXGI_SWAP_CHAIN_DESC1 Desc;
    WNDCLASSW Class;
    RECT Rect = { 0, 0, 320, 240 };
    HANDLE Event = NULL;
    HWND Window = NULL;
    UINT64 FenceValue = 0;
    UINT Frame, Presented = 0, Index, Increment, Waits = 0, MaxLatency = 0;
    HANDLE Latency = NULL;
    BOOL FullscreenState = FALSE, WindowedState = TRUE;
    HRESULT FullscreenResult = E_FAIL, WindowedResult = E_FAIL;
    PCSTR Step = "window";
    HRESULT Result = E_FAIL;
    DWORD End;

    ZeroMemory(&Class, sizeof(Class));
    Class.lpfnWndProc = FeatWindowProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"D3DFeatPresent";
    Class.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    RegisterClassW(&Class);
    AdjustWindowRect(&Rect, WS_OVERLAPPEDWINDOW, FALSE);
    Window = CreateWindowExW(0, Class.lpszClassName, L"Direct3D 12", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             200, 150, Rect.right - Rect.left, Rect.bottom - Rect.top, NULL, NULL,
                             Class.hInstance, NULL);
    if (Window == NULL)
    {
        Result = HRESULT_FROM_WIN32(GetLastError());
        goto Done;
    }
    FeatPumpMessages();

    Step = "queue";
    ZeroMemory(&QueueDesc, sizeof(QueueDesc));
    QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Result = ID3D12Device_CreateCommandQueue(Device, &QueueDesc, &IID_ID3D12CommandQueue, (void **)&Queue);
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateCommandAllocator(Device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                     &IID_ID3D12CommandAllocator, (void **)&Allocator);
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateFence(Device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&Fence);
    if (SUCCEEDED(Result))
        Result = ID3D12Device_CreateCommandList(Device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, Allocator, NULL,
                                                &IID_ID3D12GraphicsCommandList, (void **)&List);
    if (SUCCEEDED(Result))
        Result = ID3D12GraphicsCommandList_Close(List);
    if (FAILED(Result))
        goto Done;
    Event = CreateEventW(NULL, FALSE, FALSE, NULL);

    Step = "swap chain";
    Result = CreateDXGIFactory1(&IID_IDXGIFactory2, (void **)&Factory);
    if (FAILED(Result))
        goto Done;
    ZeroMemory(&Desc, sizeof(Desc));
    Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    Desc.BufferCount = 2;
    Desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    Desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    Result = IDXGIFactory2_CreateSwapChainForHwnd(Factory, (IUnknown *)Queue, Window, &Desc, NULL, NULL, &SwapChain);
    if (SUCCEEDED(Result))
        Result = IDXGISwapChain1_QueryInterface(SwapChain, &IID_IDXGISwapChain3, (void **)&SwapChain3);
    if (FAILED(Result))
        goto Done;
    Latency = IDXGISwapChain3_GetFrameLatencyWaitableObject(SwapChain3);
    IDXGISwapChain3_GetMaximumFrameLatency(SwapChain3, &MaxLatency);

    Step = "views";
    ZeroMemory(&HeapDesc, sizeof(HeapDesc));
    HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    HeapDesc.NumDescriptors = 2;
    Result = ID3D12Device_CreateDescriptorHeap(Device, &HeapDesc, &IID_ID3D12DescriptorHeap, (void **)&Heap);
    if (FAILED(Result))
        goto Done;
    Increment = ID3D12Device_GetDescriptorHandleIncrementSize(Device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    Views[0] = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(Heap);
    Views[1].ptr = Views[0].ptr + Increment;
    for (Index = 0; Index < 2; Index++)
    {
        Result = IDXGISwapChain3_GetBuffer(SwapChain3, Index, &IID_ID3D12Resource, (void **)&Buffers[Index]);
        if (FAILED(Result))
            goto Done;
        ID3D12Device_CreateRenderTargetView(Device, Buffers[Index], NULL, Views[Index]);
    }

    Step = "present";
    for (Frame = 0; Frame < 90; Frame++)
    {
        if (Latency != NULL && WaitForSingleObjectEx(Latency, 1000, TRUE) == WAIT_OBJECT_0)
            Waits++;
        Index = IDXGISwapChain3_GetCurrentBackBufferIndex(SwapChain3);
        Result = ID3D12CommandAllocator_Reset(Allocator);
        if (SUCCEEDED(Result))
            Result = ID3D12GraphicsCommandList_Reset(List, Allocator, NULL);
        if (FAILED(Result))
            break;
        ZeroMemory(&Barrier, sizeof(Barrier));
        Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        Barrier.Transition.pResource = Buffers[Index];
        Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ID3D12GraphicsCommandList_ResourceBarrier(List, 1, &Barrier);
        ID3D12GraphicsCommandList_ClearRenderTargetView(List, Views[Index], Magenta, 0, NULL);
        Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        ID3D12GraphicsCommandList_ResourceBarrier(List, 1, &Barrier);
        Result = FeatExecute12(Queue, List, Fence, Event, &FenceValue);
        if (FAILED(Result))
            break;
        Result = IDXGISwapChain3_Present(SwapChain3, 1, 0);
        if (FAILED(Result))
            break;
        Presented++;
        FeatPumpMessages();
    }
    if (SUCCEEDED(Result))
    {
        Step = "fullscreen";
        FullscreenResult = IDXGISwapChain3_SetFullscreenState(SwapChain3, TRUE, NULL);
        IDXGISwapChain3_GetFullscreenState(SwapChain3, &FullscreenState, NULL);
        FeatPumpMessages();
        WindowedResult = IDXGISwapChain3_SetFullscreenState(SwapChain3, FALSE, NULL);
        IDXGISwapChain3_GetFullscreenState(SwapChain3, &WindowedState, NULL);
        FeatPumpMessages();
    }

Done:
    FeatPrint("D3DFEAT_D3D12_PRESENT adapter=%lu step=\"%s\" hr=0x%08lx frames=%u waits=%u latency=%u "
              "fullscreen=0x%08lx,%d windowed=0x%08lx,%d result=%s\n", Adapter, Step, Result, Presented, Waits,
              MaxLatency, FullscreenResult, FullscreenState, WindowedResult, WindowedState,
              SUCCEEDED(Result) && Presented == 90 && Waits == 90 && MaxLatency == 1 && SUCCEEDED(FullscreenResult)
              && FullscreenState && SUCCEEDED(WindowedResult) && !WindowedState ? "pass" : "fail");
    if (Latency != NULL) CloseHandle(Latency);
    End = GetTickCount() + 4000;
    while (Window != NULL && (LONG)(End - GetTickCount()) > 0)
    {
        FeatPumpMessages();
        Sleep(50);
    }
    for (Index = 0; Index < 2; Index++)
        if (Buffers[Index] != NULL) ID3D12Resource_Release(Buffers[Index]);
    if (Heap != NULL) ID3D12DescriptorHeap_Release(Heap);
    if (SwapChain3 != NULL) IDXGISwapChain3_Release(SwapChain3);
    if (SwapChain != NULL) IDXGISwapChain1_Release(SwapChain);
    if (Factory != NULL) IDXGIFactory2_Release(Factory);
    if (Event != NULL) CloseHandle(Event);
    if (List != NULL) ID3D12GraphicsCommandList_Release(List);
    if (Fence != NULL) ID3D12Fence_Release(Fence);
    if (Allocator != NULL) ID3D12CommandAllocator_Release(Allocator);
    if (Queue != NULL) ID3D12CommandQueue_Release(Queue);
    if (Window != NULL) DestroyWindow(Window);
}

static VOID
FeatDirect3D12(
    _In_ ULONG AdapterIndex,
    _In_ IDXGIAdapter1 *Adapter)
{
    static const D3D_FEATURE_LEVEL Levels[] =
    {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_12_1,
    };
    D3D12_FEATURE_DATA_FEATURE_LEVELS FeatureLevels;
    D3D12_FEATURE_DATA_D3D12_OPTIONS Options;
    D3D12_FEATURE_DATA_ARCHITECTURE Architecture;
    D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT Address;
    ID3D12Device *Device = NULL;
    HRESULT Result;

    Result = D3D12CreateDevice((IUnknown *)Adapter, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&Device);
    FeatPrint("D3DFEAT_D3D12_CREATE adapter=%lu min_level=0x%04x hr=0x%08lx\n",
              AdapterIndex, D3D_FEATURE_LEVEL_11_0, Result);
    if (FAILED(Result))
        return;

    ZeroMemory(&FeatureLevels, sizeof(FeatureLevels));
    FeatureLevels.NumFeatureLevels = RTL_NUMBER_OF(Levels);
    FeatureLevels.pFeatureLevelsRequested = Levels;
    Result = ID3D12Device_CheckFeatureSupport(Device, D3D12_FEATURE_FEATURE_LEVELS, &FeatureLevels, sizeof(FeatureLevels));
    FeatPrint("D3DFEAT_D3D12_LEVELS adapter=%lu hr=0x%08lx max_level=0x%04x\n",
              AdapterIndex, Result, FeatureLevels.MaxSupportedFeatureLevel);

    ZeroMemory(&Options, sizeof(Options));
    Result = ID3D12Device_CheckFeatureSupport(Device, D3D12_FEATURE_D3D12_OPTIONS, &Options, sizeof(Options));
    FeatPrint("D3DFEAT_D3D12_OPTIONS adapter=%lu hr=0x%08lx doubles=%d logic_op=%d min_precision=0x%x tiled_tier=%d "
              "binding_tier=%d ps_stencil_ref=%d typed_uav_load=%d rovs=%d conservative_tier=%d va_bits=%u "
              "standard_swizzle=%d cross_node_tier=%d cross_adapter_row_major=%d vp_rt_index=%d heap_tier=%d\n",
              AdapterIndex, Result, Options.DoublePrecisionFloatShaderOps, Options.OutputMergerLogicOp,
              Options.MinPrecisionSupport, Options.TiledResourcesTier, Options.ResourceBindingTier,
              Options.PSSpecifiedStencilRefSupported, Options.TypedUAVLoadAdditionalFormats, Options.ROVsSupported,
              Options.ConservativeRasterizationTier, Options.MaxGPUVirtualAddressBitsPerResource,
              Options.StandardSwizzle64KBSupported, Options.CrossNodeSharingTier,
              Options.CrossAdapterRowMajorTextureSupported,
              Options.VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation,
              Options.ResourceHeapTier);

    ZeroMemory(&Architecture, sizeof(Architecture));
    Result = ID3D12Device_CheckFeatureSupport(Device, D3D12_FEATURE_ARCHITECTURE, &Architecture, sizeof(Architecture));
    ZeroMemory(&Address, sizeof(Address));
    ID3D12Device_CheckFeatureSupport(Device, D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT, &Address, sizeof(Address));
    FeatPrint("D3DFEAT_D3D12_ARCHITECTURE adapter=%lu hr=0x%08lx tbr=%d uma=%d cache_coherent_uma=%d "
              "va_bits_resource=%u va_bits_process=%u\n",
              AdapterIndex, Result, Architecture.TileBasedRenderer, Architecture.UMA,
              Architecture.CacheCoherentUMA, Address.MaxGPUVirtualAddressBitsPerResource,
              Address.MaxGPUVirtualAddressBitsPerProcess);
    FeatDirect3D12Render(AdapterIndex, Device);
    FeatDirect3D12Modern(AdapterIndex, Device);
    FeatDirect3D12StreamOutput(AdapterIndex, Device);
    FeatDirect3D12Subresource(AdapterIndex, Device);
    FeatDirect3D12Shared(AdapterIndex, Adapter, Device);
    FeatDirect3D11On12(AdapterIndex, Device);
    FeatDirect3D12ArrayCopy(AdapterIndex, Device, FALSE);
    FeatDirect3D12ArrayCopy(AdapterIndex, Device, TRUE);
    FeatDirect3D12Present(AdapterIndex, Device);
    ID3D12Device_Release(Device);
}

static HRESULT APIENTRY
FeatQueryAdapterInfo(
    _In_ HANDLE Adapter,
    _In_ CONST D3DDDICB_QUERYADAPTERINFO *Info)
{
    D3DKMT_QUERYADAPTERINFO Query;
    NTSTATUS Status;

    if (Info == NULL || Adapter == NULL)
        return E_INVALIDARG;

    ZeroMemory(&Query, sizeof(Query));
    Query.hAdapter = (D3DKMT_HANDLE)(ULONG_PTR)Adapter;
    Query.Type = KMTQAITYPE_UMDRIVERPRIVATE;
    Query.pPrivateDriverData = Info->pPrivateDriverData;
    Query.PrivateDriverDataSize = Info->PrivateDriverDataSize;
    Status = D3DKMTQueryAdapterInfo(&Query);
    return Status >= 0 ? S_OK : HRESULT_FROM_NT(Status);
}

static HRESULT APIENTRY
FeatQueryAdapterInfo2(
    _In_ HANDLE Adapter,
    _In_ CONST D3DDDICB_QUERYADAPTERINFO2 *Info)
{
    return RosUmdQueryAdapterInfo2(Adapter, Info, D3DKMTQueryAdapterInfo);
}

static VOID
FeatDriver12(
    _In_ ULONG AdapterIndex,
    _In_ LUID Luid)
{
    static ROS_UMD_ADAPTER_CALLBACKS Callbacks;
    D3D12DDI_3DPIPELINESUPPORT1_DATA_0081 Pipeline1;
    D3D12DDI_3DPIPELINELEVEL Pipeline;
    D3D12DDI_GPUVA_CAPS_0004 Address;
    D3DKMT_OPENADAPTERFROMLUID Open;
    D3DKMT_UMDFILENAMEINFO Name;
    D3DKMT_QUERYADAPTERINFO Query;
    D3DKMT_CLOSEADAPTER Close;
    D3D12DDI_ADAPTERFUNCS Functions;
    D3D12DDIARG_OPENADAPTER Arguments;
    D3D12DDIARG_GETCAPS Caps;
    PFND3D12DDI_OPENADAPTER OpenAdapter;
    CHAR NameUtf8[MAX_PATH * 3];
    UINT64 Versions[64];
    UINT32 Count, Index, Node = 0;
    DWORD LoadFlags = 0;
    HMODULE Driver;
    HRESULT Result;
    NTSTATUS Status;

    ZeroMemory(&Open, sizeof(Open));
    Open.AdapterLuid = Luid;
    Status = D3DKMTOpenAdapterFromLuid(&Open);
    if (Status < 0)
    {
        FeatPrint("D3DFEAT_UMD12 adapter=%lu open_status=0x%08lx\n", AdapterIndex, Status);
        return;
    }

    ZeroMemory(&Name, sizeof(Name));
    Name.Version = KMTUMDVERSION_DX12;
    ZeroMemory(&Query, sizeof(Query));
    Query.hAdapter = Open.hAdapter;
    Query.Type = KMTQAITYPE_UMDRIVERNAME;
    Query.pPrivateDriverData = &Name;
    Query.PrivateDriverDataSize = sizeof(Name);
    Status = D3DKMTQueryAdapterInfo(&Query);
    Name.UmdFileName[MAX_PATH - 1] = UNICODE_NULL;
    NameUtf8[0] = '\0';
    WideCharToMultiByte(CP_UTF8, 0, Name.UmdFileName, -1, NameUtf8, sizeof(NameUtf8), NULL, NULL);
    FeatPrint("D3DFEAT_UMD12 adapter=%lu name_status=0x%08lx name=\"%s\"\n", AdapterIndex, Status, NameUtf8);
    if (Status < 0 || Name.UmdFileName[0] == UNICODE_NULL)
        goto CloseKernelAdapter;

    if ((Name.UmdFileName[0] == L'\\' && Name.UmdFileName[1] == L'\\') ||
        (Name.UmdFileName[1] == L':' && Name.UmdFileName[2] == L'\\'))
    {
        LoadFlags = LOAD_WITH_ALTERED_SEARCH_PATH;
    }

    Driver = LoadLibraryExW(Name.UmdFileName, NULL, LoadFlags);
    if (Driver == NULL)
    {
        FeatPrint("D3DFEAT_UMD12 adapter=%lu load_error=%lu\n", AdapterIndex, GetLastError());
        goto CloseKernelAdapter;
    }

    OpenAdapter = (PFND3D12DDI_OPENADAPTER)GetProcAddress(Driver, "OpenAdapter12");
    if (OpenAdapter == NULL)
    {
        FeatPrint("D3DFEAT_UMD12 adapter=%lu entry=missing\n", AdapterIndex);
        goto UnloadDriver;
    }

    Callbacks.pfnQueryAdapterInfoCb = FeatQueryAdapterInfo;
    Callbacks.pfnQueryAdapterInfoCb2 = FeatQueryAdapterInfo2;
    ZeroMemory(&Functions, sizeof(Functions));
    ZeroMemory(&Arguments, sizeof(Arguments));
    Arguments.hRTAdapter.handle = (HANDLE)(ULONG_PTR)Open.hAdapter;
    Arguments.pAdapterCallbacks = (CONST D3DDDI_ADAPTERCALLBACKS *)&Callbacks;
    Arguments.pAdapterFuncs = &Functions;
    Result = OpenAdapter(&Arguments);
    FeatPrint("D3DFEAT_UMD12 adapter=%lu open_hr=0x%08lx create=%p versions=%p caps=%p tables=%p fill=%p\n",
              AdapterIndex, Result, Functions.pfnCreateDevice, Functions.pfnGetSupportedVersions,
              Functions.pfnGetCaps, Functions.pfnGetOptionalDDITables, Functions.pfnFillDDITable);
    if (FAILED(Result))
        goto UnloadDriver;

    if (Functions.pfnGetSupportedVersions != NULL)
    {
        Count = RTL_NUMBER_OF(Versions);
        Result = Functions.pfnGetSupportedVersions(Arguments.hAdapter, &Count, Versions);
        FeatPrint("D3DFEAT_UMD12_VERSIONS adapter=%lu hr=0x%08lx count=%u\n", AdapterIndex, Result, Count);
        for (Index = 0; SUCCEEDED(Result) && Index < Count && Index < RTL_NUMBER_OF(Versions); ++Index)
        {
            FeatPrint("D3DFEAT_UMD12_VERSION adapter=%lu index=%u interface=0x%08x build=%u\n",
                      AdapterIndex, Index, (UINT)(Versions[Index] >> 32), (UINT)((Versions[Index] >> 16) & 0xffff));
        }
    }

    if (Functions.pfnGetCaps != NULL)
    {
        Pipeline = 0;
        ZeroMemory(&Caps, sizeof(Caps));
        Caps.Type = D3D12DDICAPS_TYPE_3DPIPELINESUPPORT;
        Caps.pData = &Pipeline;
        Caps.DataSize = sizeof(Pipeline);
        Result = Functions.pfnGetCaps(Arguments.hAdapter, &Caps);
        FeatPrint("D3DFEAT_UMD12_PIPELINE adapter=%lu hr=0x%08lx level=%d\n", AdapterIndex, Result, Pipeline);

        ZeroMemory(&Pipeline1, sizeof(Pipeline1));
        Pipeline1.HighestRuntimeSupportedFeatureLevel = D3D12DDI_3DPIPELINELEVEL_12_2;
        Caps.Type = D3D12DDICAPS_TYPE_0081_3DPIPELINESUPPORT1;
        Caps.pData = &Pipeline1;
        Caps.DataSize = sizeof(Pipeline1);
        Result = Functions.pfnGetCaps(Arguments.hAdapter, &Caps);
        FeatPrint("D3DFEAT_UMD12_PIPELINE1 adapter=%lu hr=0x%08lx level=%d\n",
                  AdapterIndex, Result, Pipeline1.MaximumDriverSupportedFeatureLevel);

        ZeroMemory(&Address, sizeof(Address));
        Caps.Type = D3D12DDICAPS_TYPE_GPUVA_CAPS;
        Caps.pInfo = &Node;
        Caps.pData = &Address;
        Caps.DataSize = sizeof(Address);
        Result = Functions.pfnGetCaps(Arguments.hAdapter, &Caps);
        FeatPrint("D3DFEAT_UMD12_GPUVA adapter=%lu hr=0x%08lx bits=%u\n",
                  AdapterIndex, Result, Address.MaxGPUVirtualAddressBitsPerResource);
    }

    if (Functions.pfnGetOptionalDDITables != NULL)
    {
        D3D12DDI_TABLE_REQUEST Tables[64];

        Count = RTL_NUMBER_OF(Tables);
        ZeroMemory(Tables, sizeof(Tables));
        Result = Functions.pfnGetOptionalDDITables(Arguments.hAdapter, &Count, Tables);
        FeatPrint("D3DFEAT_UMD12_TABLES adapter=%lu hr=0x%08lx count=%u\n", AdapterIndex, Result, Count);
        for (Index = 0; SUCCEEDED(Result) && Index < Count && Index < RTL_NUMBER_OF(Tables); ++Index)
        {
            FeatPrint("D3DFEAT_UMD12_TABLE adapter=%lu index=%u type=%d count=%u\n",
                      AdapterIndex, Index, Tables[Index].tableType, Tables[Index].numTables);
        }
    }

    if (Functions.pfnCloseAdapter != NULL)
        Functions.pfnCloseAdapter(Arguments.hAdapter);

UnloadDriver:
    FreeLibrary(Driver);
CloseKernelAdapter:
    Close.hAdapter = Open.hAdapter;
    D3DKMTCloseAdapter(&Close);
}

static LONG FeatThrowCount;

static LONG CALLBACK
FeatExceptionFilter(
    _In_ PEXCEPTION_POINTERS Pointers)
{
    PEXCEPTION_RECORD Record = Pointers->ExceptionRecord;

    if (Record->ExceptionCode == 0xE06D7363 && Record->NumberParameters >= 3)
    {
        CONST DWORD *Object = (CONST DWORD *)Record->ExceptionInformation[1];

        FeatPrint("D3DFEAT_CXX_THROW address=%p object=%p value=0x%08lx 0x%08lx 0x%08lx 0x%08lx\n",
                  Record->ExceptionAddress, Object,
                  Object ? Object[0] : 0, Object ? Object[1] : 0, Object ? Object[2] : 0, Object ? Object[3] : 0);
        if (Object != NULL && Object[0] == 0x80070057 && InterlockedIncrement(&FeatThrowCount) <= 1)
        {
            PVOID Frames[24];
            USHORT Count = RtlCaptureStackBackTrace(0, RTL_NUMBER_OF(Frames), Frames, NULL);
            USHORT Index;

            for (Index = 0; Index < Count; ++Index)
            {
                HMODULE Module = NULL;
                CHAR Name[MAX_PATH];

                Name[0] = '\0';
                if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                       (LPCSTR)Frames[Index], &Module) && Module != NULL)
                {
                    GetModuleFileNameA(Module, Name, sizeof(Name));
                }
                FeatPrint("D3DFEAT_CXX_FRAME %u %s+0x%Ix\n", Index, Name,
                          (ULONG_PTR)Frames[Index] - (ULONG_PTR)Module);
            }
        }
    }
    else if ((Record->ExceptionCode & 0xC0000000) == 0xC0000000)
    {
        FeatPrint("D3DFEAT_EXCEPTION code=0x%08lx address=%p\n", Record->ExceptionCode, Record->ExceptionAddress);
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

int
main(void)
{
    IDXGIFactory1 *Factory = NULL;
    IDXGIAdapter1 *Adapter;
    HRESULT Result;
    ULONG Index;

    AddVectoredExceptionHandler(1, FeatExceptionFilter);
    Result = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&Factory);
    FeatPrint("D3DFEAT_BEGIN factory_hr=0x%08lx\n", Result);
    if (FAILED(Result))
        return 1;

    for (Index = 0; IDXGIFactory1_EnumAdapters1(Factory, Index, &Adapter) == S_OK; ++Index)
    {
        DXGI_ADAPTER_DESC1 Description;
        CHAR Name[RTL_NUMBER_OF(Description.Description) * 3];

        ZeroMemory(&Description, sizeof(Description));
        IDXGIAdapter1_GetDesc1(Adapter, &Description);
        Name[0] = '\0';
        WideCharToMultiByte(CP_UTF8, 0, Description.Description, -1, Name, sizeof(Name), NULL, NULL);
        FeatPrint("D3DFEAT_ADAPTER index=%lu vendor=0x%04x device=0x%04x flags=0x%x luid=%08lx:%08lx name=\"%s\"\n",
                  Index, Description.VendorId, Description.DeviceId, Description.Flags,
                  Description.AdapterLuid.HighPart, Description.AdapterLuid.LowPart, Name);
        if (GetEnvironmentVariableA("D3DFEAT_PLACED", Name, sizeof(Name)))
        {
            FeatDirect3D12Placed(Adapter, (ULONG)atoi(Name));
            IDXGIAdapter1_Release(Adapter);
            continue;
        }
        if (GetEnvironmentVariableA("D3DFEAT_TYPELESS", NULL, 0))
        {
            ULONG Variant;

            for (Variant = 0; Variant < 10; ++Variant)
                FeatDirect3D12ClearTypeless(Index, Adapter, Variant);
            IDXGIAdapter1_Release(Adapter);
            continue;
        }
        if (!GetEnvironmentVariableA("D3DFEAT_ONLY12", NULL, 0))
            FeatDirect3D11(Index, Adapter);
        FeatDirect3D12(Index, Adapter);
        FeatDriver12(Index, Description.AdapterLuid);
        IDXGIAdapter1_Release(Adapter);
    }

    IDXGIFactory1_Release(Factory);
    FeatPrint("D3DFEAT_END adapters=%lu\n", Index);
    return 0;
}
