/*
 * PROJECT: ReactOS Direct3D 11 runtime
 * LICENSE: GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE: WDDM 2.0 argument adapters, included after the native runtime object types
 */

struct alignas(16) NativeWddm20DeviceHeader
{
    NativeDevice *device;
};

static NativeDevice *NativeWddm20Device(D3D10DDI_HDEVICE handle)
{
    return (static_cast<NativeWddm20DeviceHeader *>(handle.pDrvPrivate) - 1)->device;
}

template<typename Table> static Table &NativeDriverFunctions(NativeDevice *device);
template<> D3DWDDM2_0DDI_DEVICEFUNCS &NativeDriverFunctions<D3DWDDM2_0DDI_DEVICEFUNCS>(NativeDevice *device)
{
    return device->wddm20_functions;
}
template<> D3D11_1DDI_DEVICEFUNCS &NativeDriverFunctions<D3D11_1DDI_DEVICEFUNCS>(NativeDevice *device)
{
    return device->ddi11_1_functions;
}

template<typename Table, typename Pfn> struct NativeLiveCall;
template<typename Table, typename R, typename... A> struct NativeLiveCall<Table, R (APIENTRY *)(D3D10DDI_HDEVICE, A...)>
{
    template<R (APIENTRY *Table::*Member)(D3D10DDI_HDEVICE, A...)>
    static R APIENTRY Call(D3D10DDI_HDEVICE h, A... args)
    {
        return (NativeDriverFunctions<Table>(NativeWddm20Device(h)).*Member)(h, args...);
    }
};

#define NATIVE_LIVE(Table, Name) \
    (NativeLiveCall<Table, decltype(Table::pfn##Name)>::template Call<&Table::pfn##Name>)

#define NATIVE_WDDM20_CONSTANT_BUFFERS(Name) \
static void APIENTRY NativeWddm20##Name(D3D10DDI_HDEVICE h, UINT start, UINT count, const D3D10DDI_HRESOURCE *resources) \
{ \
    NativeWddm20Device(h)->wddm20_functions.pfn##Name(h, start, count, resources, NULL, NULL); \
}
NATIVE_WDDM20_CONSTANT_BUFFERS(VsSetConstantBuffers)
NATIVE_WDDM20_CONSTANT_BUFFERS(PsSetConstantBuffers)
NATIVE_WDDM20_CONSTANT_BUFFERS(GsSetConstantBuffers)
NATIVE_WDDM20_CONSTANT_BUFFERS(HsSetConstantBuffers)
NATIVE_WDDM20_CONSTANT_BUFFERS(DsSetConstantBuffers)
NATIVE_WDDM20_CONSTANT_BUFFERS(CsSetConstantBuffers)
#undef NATIVE_WDDM20_CONSTANT_BUFFERS

#define NATIVE_WDDM20_UPDATE(Name) \
static void APIENTRY NativeWddm20##Name(D3D10DDI_HDEVICE h, D3D10DDI_HRESOURCE resource, UINT subresource, \
        const D3D10_DDI_BOX *box, const void *data, UINT row_pitch, UINT depth_pitch) \
{ \
    NativeWddm20Device(h)->wddm20_functions.pfn##Name(h, resource, subresource, box, data, row_pitch, depth_pitch, 0); \
}
NATIVE_WDDM20_UPDATE(DefaultConstantBufferUpdateSubresourceUP)
NATIVE_WDDM20_UPDATE(ResourceUpdateSubresourceUP)
#undef NATIVE_WDDM20_UPDATE

#define NATIVE_WDDM20_COPY(Name) \
static void APIENTRY NativeWddm20##Name(D3D10DDI_HDEVICE h, D3D10DDI_HRESOURCE dst, UINT dst_subresource, \
        UINT x, UINT y, UINT z, D3D10DDI_HRESOURCE src, UINT src_subresource, const D3D10_DDI_BOX *box) \
{ \
    NativeWddm20Device(h)->wddm20_functions.pfn##Name(h, dst, dst_subresource, x, y, z, src, src_subresource, box, 0); \
}
NATIVE_WDDM20_COPY(ResourceCopyRegion)
NATIVE_WDDM20_COPY(ResourceConvertRegion)
#undef NATIVE_WDDM20_COPY

static void APIENTRY NativeWddm20Flush(D3D10DDI_HDEVICE h)
{
    /* ALL contexts, unconditional flush; do not turn an API Flush into a hint. */
    NativeWddm20Device(h)->wddm20_functions.pfnFlush(h, 0, 0);
}

static void APIENTRY NativeWddm20CheckMultisampleQualityLevels(D3D10DDI_HDEVICE h,
        DXGI_FORMAT format, UINT samples, UINT *quality)
{
    NativeWddm20Device(h)->wddm20_functions.pfnCheckMultisampleQualityLevels(h, format, samples, 0, quality);
}

static D3D11_1_DDI_BLEND_DESC NativeWddm20Convert(const D3D10_1_DDI_BLEND_DESC &old)
{
    D3D11_1_DDI_BLEND_DESC value = {};
    value.AlphaToCoverageEnable = old.AlphaToCoverageEnable;
    value.IndependentBlendEnable = old.IndependentBlendEnable;
    for (UINT i = 0; i < ARRAYSIZE(value.RenderTarget); ++i)
    {
        value.RenderTarget[i].BlendEnable = old.RenderTarget[i].BlendEnable;
        value.RenderTarget[i].SrcBlend = old.RenderTarget[i].SrcBlend;
        value.RenderTarget[i].DestBlend = old.RenderTarget[i].DestBlend;
        value.RenderTarget[i].BlendOp = old.RenderTarget[i].BlendOp;
        value.RenderTarget[i].SrcBlendAlpha = old.RenderTarget[i].SrcBlendAlpha;
        value.RenderTarget[i].DestBlendAlpha = old.RenderTarget[i].DestBlendAlpha;
        value.RenderTarget[i].BlendOpAlpha = old.RenderTarget[i].BlendOpAlpha;
        value.RenderTarget[i].RenderTargetWriteMask = old.RenderTarget[i].RenderTargetWriteMask;
        value.RenderTarget[i].LogicOp = D3D11_1_DDI_LOGIC_OP_COPY;
    }
    return value;
}

static D3DWDDM2_0DDI_RASTERIZER_DESC NativeWddm20Convert(const D3D10_DDI_RASTERIZER_DESC &old)
{
    D3DWDDM2_0DDI_RASTERIZER_DESC value = {};
    static_assert(offsetof(D3DWDDM2_0DDI_RASTERIZER_DESC, ForcedSampleCount) == sizeof(old), "Rasterizer prefix");
    memcpy(&value, &old, sizeof(old));
    return value;
}

static D3DWDDM2_0DDIARG_CREATEQUERY NativeWddm20Convert(const D3D10DDIARG_CREATEQUERY &old)
{
    D3DWDDM2_0DDIARG_CREATEQUERY value = {old.Query, old.MiscFlags, 1 /* 3D context */};
    return value;
}

static D3DWDDM2_0DDIARG_CREATERENDERTARGETVIEW NativeWddm20Convert(const D3D10DDIARG_CREATERENDERTARGETVIEW &old)
{
    D3DWDDM2_0DDIARG_CREATERENDERTARGETVIEW value = {};
    value.hDrvResource = old.hDrvResource;
    value.Format = old.Format;
    value.ResourceDimension = old.ResourceDimension;
    switch (old.ResourceDimension)
    {
        case D3D11DDIRESOURCE_BUFFEREX:
        case D3D10DDIRESOURCE_BUFFER: value.Buffer = old.Buffer; break;
        case D3D10DDIRESOURCE_TEXTURE1D: value.Tex1D = old.Tex1D; break;
        case D3D10DDIRESOURCE_TEXTURE2D:
            value.Tex2D.MipSlice = old.Tex2D.MipSlice;
            value.Tex2D.FirstArraySlice = old.Tex2D.FirstArraySlice;
            value.Tex2D.ArraySize = old.Tex2D.ArraySize;
            break;
        case D3D10DDIRESOURCE_TEXTURE3D: value.Tex3D = old.Tex3D; break;
        case D3D10DDIRESOURCE_TEXTURECUBE: value.TexCube = old.TexCube; break;
    }
    return value;
}

static D3DWDDM2_0DDIARG_CREATESHADERRESOURCEVIEW NativeWddm20Convert(const D3D11DDIARG_CREATESHADERRESOURCEVIEW &old)
{
    D3DWDDM2_0DDIARG_CREATESHADERRESOURCEVIEW value = {};
    value.hDrvResource = old.hDrvResource;
    value.Format = old.Format;
    value.ResourceDimension = old.ResourceDimension;
    switch (old.ResourceDimension)
    {
        case D3D10DDIRESOURCE_BUFFER: value.Buffer = old.Buffer; break;
        case D3D11DDIRESOURCE_BUFFEREX: value.BufferEx = old.BufferEx; break;
        case D3D10DDIRESOURCE_TEXTURE1D: value.Tex1D = old.Tex1D; break;
        case D3D10DDIRESOURCE_TEXTURE2D:
            value.Tex2D.MostDetailedMip = old.Tex2D.MostDetailedMip;
            value.Tex2D.FirstArraySlice = old.Tex2D.FirstArraySlice;
            value.Tex2D.MipLevels = old.Tex2D.MipLevels;
            value.Tex2D.ArraySize = old.Tex2D.ArraySize;
            break;
        case D3D10DDIRESOURCE_TEXTURE3D: value.Tex3D = old.Tex3D; break;
        case D3D10DDIRESOURCE_TEXTURECUBE: value.TexCube = old.TexCube; break;
    }
    return value;
}

static D3DWDDM2_0DDIARG_CREATEUNORDEREDACCESSVIEW NativeWddm20Convert(const D3D11DDIARG_CREATEUNORDEREDACCESSVIEW &old)
{
    D3DWDDM2_0DDIARG_CREATEUNORDEREDACCESSVIEW value = {};
    value.hDrvResource = old.hDrvResource;
    value.Format = old.Format;
    value.ResourceDimension = old.ResourceDimension;
    switch (old.ResourceDimension)
    {
        case D3D10DDIRESOURCE_BUFFER: value.Buffer = old.Buffer; break;
        case D3D10DDIRESOURCE_TEXTURE1D: value.Tex1D = old.Tex1D; break;
        case D3D10DDIRESOURCE_TEXTURE2D:
            value.Tex2D.MipSlice = old.Tex2D.MipSlice;
            value.Tex2D.FirstArraySlice = old.Tex2D.FirstArraySlice;
            value.Tex2D.ArraySize = old.Tex2D.ArraySize;
            break;
        case D3D10DDIRESOURCE_TEXTURE3D: value.Tex3D = old.Tex3D; break;
        default: break;
    }
    return value;
}

template<class Args> static void NativeWddm20Extend(NativeDevice *, Args &) {}
static void NativeWddm20Extend(NativeDevice *device, D3DWDDM2_0DDIARG_CREATERENDERTARGETVIEW &value)
{
    if (value.ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D) value.Tex2D.PlaneSlice = device->plane_slice;
}
static void NativeWddm20Extend(NativeDevice *device, D3DWDDM2_0DDIARG_CREATESHADERRESOURCEVIEW &value)
{
    if (value.ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D) value.Tex2D.PlaneSlice = device->plane_slice;
}
static void NativeWddm20Extend(NativeDevice *device, D3DWDDM2_0DDIARG_CREATEUNORDEREDACCESSVIEW &value)
{
    if (value.ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D) value.Tex2D.PlaneSlice = device->plane_slice;
}
static void NativeWddm20Extend(NativeDevice *device, D3DWDDM2_0DDIARG_CREATEQUERY &value)
{
    value.ContextType = device->query_context;
}

#define NATIVE_WDDM20_OBJECT(Name, Old, New, DriverHandle, RuntimeHandle) \
static SIZE_T APIENTRY NativeWddm20CalcPrivate##Name##Size(D3D10DDI_HDEVICE h, const Old *old) \
{ \
    New value = NativeWddm20Convert(*old); \
    NativeWddm20Extend(NativeWddm20Device(h), value); \
    return NativeWddm20Device(h)->wddm20_functions.pfnCalcPrivate##Name##Size(h, &value); \
} \
static void APIENTRY NativeWddm20Create##Name(D3D10DDI_HDEVICE h, const Old *old, DriverHandle object, RuntimeHandle runtime) \
{ \
    New value = NativeWddm20Convert(*old); \
    NativeWddm20Extend(NativeWddm20Device(h), value); \
    NativeWddm20Device(h)->wddm20_functions.pfnCreate##Name(h, &value, object, runtime); \
}
NATIVE_WDDM20_OBJECT(BlendState, D3D10_1_DDI_BLEND_DESC, D3D11_1_DDI_BLEND_DESC, D3D10DDI_HBLENDSTATE, D3D10DDI_HRTBLENDSTATE)
NATIVE_WDDM20_OBJECT(RasterizerState, D3D10_DDI_RASTERIZER_DESC, D3DWDDM2_0DDI_RASTERIZER_DESC, D3D10DDI_HRASTERIZERSTATE, D3D10DDI_HRTRASTERIZERSTATE)
NATIVE_WDDM20_OBJECT(Query, D3D10DDIARG_CREATEQUERY, D3DWDDM2_0DDIARG_CREATEQUERY, D3D10DDI_HQUERY, D3D10DDI_HRTQUERY)
NATIVE_WDDM20_OBJECT(RenderTargetView, D3D10DDIARG_CREATERENDERTARGETVIEW, D3DWDDM2_0DDIARG_CREATERENDERTARGETVIEW, D3D10DDI_HRENDERTARGETVIEW, D3D10DDI_HRTRENDERTARGETVIEW)
NATIVE_WDDM20_OBJECT(ShaderResourceView, D3D11DDIARG_CREATESHADERRESOURCEVIEW, D3DWDDM2_0DDIARG_CREATESHADERRESOURCEVIEW, D3D10DDI_HSHADERRESOURCEVIEW, D3D10DDI_HRTSHADERRESOURCEVIEW)
NATIVE_WDDM20_OBJECT(UnorderedAccessView, D3D11DDIARG_CREATEUNORDEREDACCESSVIEW, D3DWDDM2_0DDIARG_CREATEUNORDEREDACCESSVIEW, D3D11DDI_HUNORDEREDACCESSVIEW, D3D11DDI_HRTUNORDEREDACCESSVIEW)
#undef NATIVE_WDDM20_OBJECT

static HRESULT APIENTRY NativeWddm20CreateContext(D3D10DDI_HRTCORELAYER layer, D3DWDDM2_0DDICB_CREATECONTEXT *args)
{
    NativeDevice *device = static_cast<NativeDevice *>(layer.handle);
    if (!args || !device->kernel_callbacks.pfnCreateContextCb) return E_INVALIDARG;
    D3DDDICB_CREATECONTEXT legacy = {};
    legacy.NodeOrdinal = args->NodeOrdinal;
    legacy.EngineAffinity = args->EngineAffinity;
    legacy.Flags = args->Flags;
    legacy.pPrivateDriverData = args->pPrivateDriverData;
    legacy.PrivateDriverDataSize = args->PrivateDriverDataSize;
    HRESULT hr = device->kernel_callbacks.pfnCreateContextCb(device->runtime_device, &legacy);
    if (SUCCEEDED(hr))
    {
        args->hContext = legacy.hContext;
        args->pCommandBuffer = legacy.pCommandBuffer;
        args->CommandBufferSize = legacy.CommandBufferSize;
        args->pAllocationList = legacy.pAllocationList;
        args->AllocationListSize = legacy.AllocationListSize;
        args->pPatchLocationList = legacy.pPatchLocationList;
        args->PatchLocationListSize = legacy.PatchLocationListSize;
        args->CommandBuffer = legacy.CommandBuffer;
    }
    return hr;
}

static HRESULT APIENTRY NativeWddm20CreateContextVirtual(D3D10DDI_HRTCORELAYER layer, D3DWDDM2_0DDICB_CREATECONTEXTVIRTUAL *args)
{
    NativeDevice *device = static_cast<NativeDevice *>(layer.handle);
    if (!args || !device->kernel_callbacks.pfnCreateContextVirtualCb) return E_INVALIDARG;
    D3DDDICB_CREATECONTEXTVIRTUAL legacy = {};
    legacy.NodeOrdinal = args->NodeOrdinal;
    legacy.EngineAffinity = args->EngineAffinity;
    legacy.Flags = args->Flags;
    legacy.pPrivateDriverData = args->pPrivateDriverData;
    legacy.PrivateDriverDataSize = args->PrivateDriverDataSize;
    HRESULT hr = device->kernel_callbacks.pfnCreateContextVirtualCb(device->runtime_device, &legacy);
    if (SUCCEEDED(hr)) args->hContext = legacy.hContext;
    return hr;
}

static void APIENTRY NativeWddm20NoDeferredWork(D3D10DDI_HRTCORELAYER)
{
    /* The native runtime already destroys staging resources synchronously and
     * has no deferred garbage collection or amortized work queue. */
}

static UINT NativeWddm20RefreshCount(UINT count, UINT base, UINT capacity)
{
    if (base >= capacity) return 0;
    return count == ~0u ? capacity - base : min(count, capacity - base);
}

#define NATIVE_WDDM20_REFRESH_RANGE(Name, Stage, Method, Member, Capacity) \
static void APIENTRY NativeWddm20State##Name(D3D10DDI_HRTCORELAYER layer, UINT base, UINT count) \
{ \
    NativeContext *context = static_cast<NativeDevice *>(layer.handle)->context; \
    count = NativeWddm20RefreshCount(count, base, Capacity); \
    if (context && count) NativeStateBypass(context)->Method(Stage, base, count, &context->Member[Stage][base]); \
}
NATIVE_WDDM20_REFRESH_RANGE(VsConstBuf, 0, SetConstantBuffers, constant_buffers, 14)
NATIVE_WDDM20_REFRESH_RANGE(PsConstBuf, 1, SetConstantBuffers, constant_buffers, 14)
NATIVE_WDDM20_REFRESH_RANGE(GsConstBuf, 2, SetConstantBuffers, constant_buffers, 14)
NATIVE_WDDM20_REFRESH_RANGE(VsSrv, 0, SetShaderResources, shader_resources, 128)
NATIVE_WDDM20_REFRESH_RANGE(PsSrv, 1, SetShaderResources, shader_resources, 128)
NATIVE_WDDM20_REFRESH_RANGE(GsSrv, 2, SetShaderResources, shader_resources, 128)
NATIVE_WDDM20_REFRESH_RANGE(VsSampler, 0, SetSamplers, samplers, 16)
NATIVE_WDDM20_REFRESH_RANGE(PsSampler, 1, SetSamplers, samplers, 16)
NATIVE_WDDM20_REFRESH_RANGE(GsSampler, 2, SetSamplers, samplers, 16)
NATIVE_WDDM20_REFRESH_RANGE(HsConstBuf, 3, SetConstantBuffers, constant_buffers, 14)
NATIVE_WDDM20_REFRESH_RANGE(HsSrv, 3, SetShaderResources, shader_resources, 128)
NATIVE_WDDM20_REFRESH_RANGE(HsSampler, 3, SetSamplers, samplers, 16)
NATIVE_WDDM20_REFRESH_RANGE(DsConstBuf, 4, SetConstantBuffers, constant_buffers, 14)
NATIVE_WDDM20_REFRESH_RANGE(DsSrv, 4, SetShaderResources, shader_resources, 128)
NATIVE_WDDM20_REFRESH_RANGE(DsSampler, 4, SetSamplers, samplers, 16)
NATIVE_WDDM20_REFRESH_RANGE(CsConstBuf, 5, SetConstantBuffers, constant_buffers, 14)
NATIVE_WDDM20_REFRESH_RANGE(CsSrv, 5, SetShaderResources, shader_resources, 128)
NATIVE_WDDM20_REFRESH_RANGE(CsSampler, 5, SetSamplers, samplers, 16)
#undef NATIVE_WDDM20_REFRESH_RANGE

#define NATIVE_WDDM20_REFRESH(Name, Expression) \
static void APIENTRY NativeWddm20State##Name(D3D10DDI_HRTCORELAYER layer) \
{ \
    NativeContext *context = static_cast<NativeDevice *>(layer.handle)->context; \
    if (context) NativeStateBypass(context)->Expression; \
}
NATIVE_WDDM20_REFRESH(VsShader, VSSetShader(context->vertex_shader, NULL, 0))
NATIVE_WDDM20_REFRESH(PsShader, PSSetShader(context->pixel_shader, NULL, 0))
NATIVE_WDDM20_REFRESH(GsShader, GSSetShader(context->geometry_shader, NULL, 0))
NATIVE_WDDM20_REFRESH(IaInputLayout, IASetInputLayout(context->input_layout))
NATIVE_WDDM20_REFRESH(IaIndexBuf, IASetIndexBuffer(context->index_buffer, context->index_format, context->index_offset))
NATIVE_WDDM20_REFRESH(IaPrimitiveTopology, IASetPrimitiveTopology(context->topology))
NATIVE_WDDM20_REFRESH(OmRenderTargets, RefreshOutputBindings())
NATIVE_WDDM20_REFRESH(OmBlendState, OMSetBlendState(context->blend_state, context->blend_factor, context->sample_mask))
NATIVE_WDDM20_REFRESH(OmDepthState, OMSetDepthStencilState(context->depth_stencil_state, context->stencil_ref))
NATIVE_WDDM20_REFRESH(RsRastState, RSSetState(context->rasterizer_state))
NATIVE_WDDM20_REFRESH(RsViewports, RSSetViewports(context->viewport_count, context->viewports))
NATIVE_WDDM20_REFRESH(RsScissor, RSSetScissorRects(context->scissor_count, context->scissors))
NATIVE_WDDM20_REFRESH(HsShader, HSSetShader(context->hull_shader, NULL, 0))
NATIVE_WDDM20_REFRESH(DsShader, DSSetShader(context->domain_shader, NULL, 0))
NATIVE_WDDM20_REFRESH(CsShader, CSSetShader(context->compute_shader, NULL, 0))
#undef NATIVE_WDDM20_REFRESH

static void APIENTRY NativeWddm20StateIaVertexBuf(D3D10DDI_HRTCORELAYER layer, UINT base, UINT count)
{
    NativeContext *context = static_cast<NativeDevice *>(layer.handle)->context;
    count = NativeWddm20RefreshCount(count, base, 32);
    if (context && count) NativeStateBypass(context)->IASetVertexBuffers(base, count, &context->vertex_buffers[base],
            &context->vertex_strides[base], &context->vertex_offsets[base]);
}

static void APIENTRY NativeWddm20StateCsUav(D3D10DDI_HRTCORELAYER layer, UINT base, UINT count)
{
    NativeContext *context = static_cast<NativeDevice *>(layer.handle)->context;
    count = NativeWddm20RefreshCount(count, base, 8);
    if (context && count) NativeStateBypass(context)->CSSetUnorderedAccessViews(base, count, context->compute_uavs + base, NULL);
}

static void APIENTRY NativeWddm20StateSoTargets(D3D10DDI_HRTCORELAYER layer)
{
    NativeContext *context = static_cast<NativeDevice *>(layer.handle)->context;
    if (context) context->RefreshStreamOutput();
}

static void APIENTRY NativeWddm20StateTextFilterSize(D3D10DDI_HRTCORELAYER layer)
{
    auto *device = static_cast<NativeDevice *>(layer.handle);
    if (device->context && device->wddm20_functions.pfnSetTextFilterSize)
        device->wddm20_functions.pfnSetTextFilterSize(device->driver_device, 0, 0);
}

template<class Callbacks> static void NativeInitializeCoreLayerCallbacks(Callbacks &callbacks)
{
    callbacks.pfnSetErrorCb = NativeSetError;
    callbacks.pfnDisableDeferredStagingResourceDestruction = NativeWddm20NoDeferredWork;
    callbacks.pfnPerformAmortizedProcessingCb = NativeWddm20NoDeferredWork;
#define REFRESH(Name) callbacks.pfnState##Name##Cb = NativeWddm20State##Name
    REFRESH(VsConstBuf); REFRESH(PsConstBuf); REFRESH(GsConstBuf);
    REFRESH(VsSrv); REFRESH(PsSrv); REFRESH(GsSrv);
    REFRESH(VsSampler); REFRESH(PsSampler); REFRESH(GsSampler);
    REFRESH(VsShader); REFRESH(PsShader); REFRESH(GsShader);
    REFRESH(IaInputLayout); REFRESH(IaVertexBuf); REFRESH(IaIndexBuf); REFRESH(IaPrimitiveTopology);
    REFRESH(OmRenderTargets); REFRESH(OmBlendState); REFRESH(OmDepthState);
    REFRESH(RsRastState); REFRESH(RsViewports); REFRESH(RsScissor);
    REFRESH(HsShader); REFRESH(HsConstBuf); REFRESH(HsSrv); REFRESH(HsSampler);
    REFRESH(DsShader); REFRESH(DsConstBuf); REFRESH(DsSrv); REFRESH(DsSampler);
    REFRESH(CsShader); REFRESH(CsConstBuf); REFRESH(CsSrv); REFRESH(CsSampler); REFRESH(CsUav);
    REFRESH(SoTargets); REFRESH(TextFilterSize);
#undef REFRESH
}

static void NativeWddm20InitializeCallbacks(NativeDevice *device)
{
    auto &callbacks = device->wddm20_callbacks;
    NativeInitializeCoreLayerCallbacks(callbacks);
    callbacks.pfnCreateContextCb = NativeWddm20CreateContext;
    callbacks.pfnCreateContextVirtualCb = NativeWddm20CreateContextVirtual;
}


static BOOL APIENTRY NativeDdi11_1Flush(D3D10DDI_HDEVICE h, UINT, UINT flags)
{
    return NativeWddm20Device(h)->ddi11_1_functions.pfnFlush(h, flags);
}

static void APIENTRY NativeDdi11_1CheckMultisampleQualityLevels(D3D10DDI_HDEVICE h, DXGI_FORMAT format, UINT samples,
        UINT flags, UINT *quality)
{
    if (flags) { *quality = 0; return; }
    NativeWddm20Device(h)->ddi11_1_functions.pfnCheckMultisampleQualityLevels(h, format, samples, quality);
}

static D3D11_1_DDI_RASTERIZER_DESC NativeDdi11_1Convert(const D3DWDDM2_0DDI_RASTERIZER_DESC &wide)
{
    D3D11_1_DDI_RASTERIZER_DESC value;
    static_assert(offsetof(D3DWDDM2_0DDI_RASTERIZER_DESC, ConservativeRasterizationMode) == sizeof(value), "Rasterizer prefix");
    memcpy(&value, &wide, sizeof(value));
    return value;
}

static SIZE_T APIENTRY NativeDdi11_1CalcPrivateRasterizerStateSize(D3D10DDI_HDEVICE h, const D3DWDDM2_0DDI_RASTERIZER_DESC *desc)
{
    D3D11_1_DDI_RASTERIZER_DESC value = NativeDdi11_1Convert(*desc);
    return NativeWddm20Device(h)->ddi11_1_functions.pfnCalcPrivateRasterizerStateSize(h, &value);
}

static void APIENTRY NativeDdi11_1CreateRasterizerState(D3D10DDI_HDEVICE h, const D3DWDDM2_0DDI_RASTERIZER_DESC *desc,
        D3D10DDI_HRASTERIZERSTATE state, D3D10DDI_HRTRASTERIZERSTATE runtime)
{
    D3D11_1_DDI_RASTERIZER_DESC value = NativeDdi11_1Convert(*desc);
    NativeWddm20Device(h)->ddi11_1_functions.pfnCreateRasterizerState(h, &value, state, runtime);
}

static void NativeDdi11_1BridgeFunctions(NativeDevice *device)
{
    const auto &d = device->ddi11_1_functions;
    auto &m = device->wddm20_functions;
#define SAME(Name) m.pfn##Name = d.pfn##Name
#define LIVE(Name) m.pfn##Name = d.pfn##Name ? NATIVE_LIVE(D3D11_1DDI_DEVICEFUNCS, Name) : NULL
    LIVE(DefaultConstantBufferUpdateSubresourceUP); LIVE(VsSetConstantBuffers); LIVE(PsSetShaderResources);
    LIVE(PsSetShader); LIVE(PsSetSamplers); LIVE(VsSetShader); LIVE(DrawIndexed); LIVE(Draw);
    LIVE(DynamicIABufferMapNoOverwrite); LIVE(DynamicIABufferUnmap); LIVE(DynamicConstantBufferMapDiscard);
    LIVE(DynamicIABufferMapDiscard); LIVE(DynamicConstantBufferUnmap); LIVE(PsSetConstantBuffers);
    LIVE(IaSetInputLayout); LIVE(IaSetVertexBuffers); LIVE(IaSetIndexBuffer); LIVE(DrawIndexedInstanced);
    LIVE(DrawInstanced); LIVE(DynamicResourceMapDiscard); LIVE(DynamicResourceUnmap); LIVE(GsSetConstantBuffers);
    LIVE(GsSetShader); LIVE(IaSetTopology); LIVE(StagingResourceMap); LIVE(StagingResourceUnmap);
    LIVE(VsSetShaderResources); LIVE(VsSetSamplers); LIVE(GsSetShaderResources); LIVE(GsSetSamplers);
    LIVE(SetRenderTargets); LIVE(ShaderResourceViewReadAfterWriteHazard); LIVE(ResourceReadAfterWriteHazard);
    LIVE(SetBlendState); LIVE(SetDepthStencilState); LIVE(SetRasterizerState); LIVE(QueryEnd); LIVE(QueryBegin);
    LIVE(ResourceCopyRegion); LIVE(ResourceUpdateSubresourceUP); LIVE(SoSetTargets); LIVE(DrawAuto);
    LIVE(SetViewports); LIVE(SetScissorRects); LIVE(ClearRenderTargetView); LIVE(ClearDepthStencilView);
    LIVE(SetPredication); LIVE(QueryGetData); LIVE(GenMips); LIVE(ResourceCopy); LIVE(ResourceResolveSubresource);
    LIVE(ResourceMap); LIVE(ResourceUnmap); LIVE(ResourceIsStagingBusy); SAME(CalcPrivateResourceSize);
    SAME(CalcPrivateOpenedResourceSize); SAME(CreateResource); SAME(OpenResource); SAME(DestroyResource);
    SAME(DestroyShaderResourceView); SAME(DestroyRenderTargetView); SAME(CalcPrivateDepthStencilViewSize);
    SAME(CreateDepthStencilView); SAME(DestroyDepthStencilView); SAME(CalcPrivateElementLayoutSize);
    SAME(CreateElementLayout); SAME(DestroyElementLayout); SAME(CalcPrivateBlendStateSize); SAME(CreateBlendState);
    SAME(DestroyBlendState); SAME(CalcPrivateDepthStencilStateSize); SAME(CreateDepthStencilState);
    SAME(DestroyDepthStencilState); SAME(DestroyRasterizerState); SAME(CalcPrivateShaderSize);
    SAME(CreateVertexShader); SAME(CreateGeometryShader); SAME(CreatePixelShader);
    SAME(CalcPrivateGeometryShaderWithStreamOutput); SAME(CreateGeometryShaderWithStreamOutput); SAME(DestroyShader);
    SAME(CalcPrivateSamplerSize); SAME(CreateSampler); SAME(DestroySampler); SAME(DestroyQuery);
    SAME(CheckFormatSupport); SAME(CheckCounterInfo); SAME(CheckCounter); SAME(DestroyDevice);
    LIVE(SetTextFilterSize); LIVE(ResourceConvert); LIVE(ResourceConvertRegion); LIVE(DrawIndexedInstancedIndirect);
    LIVE(DrawInstancedIndirect); LIVE(CommandListExecute); LIVE(HsSetShaderResources); LIVE(HsSetShader);
    LIVE(HsSetSamplers); LIVE(HsSetConstantBuffers); LIVE(DsSetShaderResources); LIVE(DsSetShader);
    LIVE(DsSetSamplers); LIVE(DsSetConstantBuffers); SAME(CreateHullShader); SAME(CreateDomainShader);
    SAME(CheckDeferredContextHandleSizes); LIVE(CalcDeferredContextHandleSize); SAME(CalcPrivateDeferredContextSize);
    SAME(CreateDeferredContext); LIVE(AbandonCommandList); SAME(CalcPrivateCommandListSize); SAME(CreateCommandList);
    SAME(DestroyCommandList); SAME(CalcPrivateTessellationShaderSize); LIVE(PsSetShaderWithIfaces);
    LIVE(VsSetShaderWithIfaces); LIVE(GsSetShaderWithIfaces); LIVE(HsSetShaderWithIfaces);
    LIVE(DsSetShaderWithIfaces); LIVE(CsSetShaderWithIfaces); SAME(CreateComputeShader); LIVE(CsSetShader);
    LIVE(CsSetShaderResources); LIVE(CsSetSamplers); LIVE(CsSetConstantBuffers); SAME(DestroyUnorderedAccessView);
    LIVE(ClearUnorderedAccessViewUint); LIVE(ClearUnorderedAccessViewFloat); LIVE(CsSetUnorderedAccessViews);
    LIVE(Dispatch); LIVE(DispatchIndirect); LIVE(SetResourceMinLOD); LIVE(CopyStructureCount);
    LIVE(RecycleCommandList); LIVE(RecycleCreateCommandList); LIVE(RecycleCreateDeferredContext);
    LIVE(RecycleDestroyCommandList); LIVE(Discard); LIVE(AssignDebugBinary);
    LIVE(DynamicConstantBufferMapNoOverwrite); SAME(CheckDirectFlipSupport); LIVE(ClearView);
#undef LIVE
#undef SAME
    m.pfnFlush = d.pfnFlush ? NativeDdi11_1Flush : NULL;
    m.pfnCheckMultisampleQualityLevels = d.pfnCheckMultisampleQualityLevels ? NativeDdi11_1CheckMultisampleQualityLevels : NULL;
    m.pfnCalcPrivateRasterizerStateSize = d.pfnCalcPrivateRasterizerStateSize ? NativeDdi11_1CalcPrivateRasterizerStateSize : NULL;
    m.pfnCreateRasterizerState = d.pfnCreateRasterizerState ? NativeDdi11_1CreateRasterizerState : NULL;
}

static void NativeDdi11_1InstallViewFunctions(NativeDevice *device)
{
    const auto &d = device->ddi11_1_functions;
    auto &f = device->functions;
    f.pfnCalcPrivateShaderResourceViewSize = d.pfnCalcPrivateShaderResourceViewSize;
    f.pfnCreateShaderResourceView = d.pfnCreateShaderResourceView;
    f.pfnCalcPrivateRenderTargetViewSize = d.pfnCalcPrivateRenderTargetViewSize;
    f.pfnCreateRenderTargetView = d.pfnCreateRenderTargetView;
    f.pfnCalcPrivateUnorderedAccessViewSize = d.pfnCalcPrivateUnorderedAccessViewSize;
    f.pfnCreateUnorderedAccessView = d.pfnCreateUnorderedAccessView;
    f.pfnCalcPrivateQuerySize = d.pfnCalcPrivateQuerySize;
    f.pfnCreateQuery = d.pfnCreateQuery;
}

static void NativeWddm20InstallFunctions(NativeDevice *device)
{
    /* Copy only identical signatures. Changed slots use typed adapters below;
     * an unsupported API must never call a newer DDI through an older type. */
    device->functions.pfnPsSetShaderResources = device->wddm20_functions.pfnPsSetShaderResources ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, PsSetShaderResources) : NULL;
    device->functions.pfnPsSetShader = device->wddm20_functions.pfnPsSetShader ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, PsSetShader) : NULL;
    device->functions.pfnPsSetSamplers = device->wddm20_functions.pfnPsSetSamplers ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, PsSetSamplers) : NULL;
    device->functions.pfnVsSetShader = device->wddm20_functions.pfnVsSetShader ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, VsSetShader) : NULL;
    device->functions.pfnDrawIndexed = device->wddm20_functions.pfnDrawIndexed ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, DrawIndexed) : NULL;
    device->functions.pfnDraw = device->wddm20_functions.pfnDraw ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, Draw) : NULL;
    device->functions.pfnDynamicIABufferMapNoOverwrite = device->wddm20_functions.pfnDynamicIABufferMapNoOverwrite ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, DynamicIABufferMapNoOverwrite) : NULL;
    device->functions.pfnDynamicIABufferUnmap = device->wddm20_functions.pfnDynamicIABufferUnmap ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, DynamicIABufferUnmap) : NULL;
    device->functions.pfnDynamicConstantBufferMapDiscard = device->wddm20_functions.pfnDynamicConstantBufferMapDiscard ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, DynamicConstantBufferMapDiscard) : NULL;
    device->functions.pfnDynamicIABufferMapDiscard = device->wddm20_functions.pfnDynamicIABufferMapDiscard ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, DynamicIABufferMapDiscard) : NULL;
    device->functions.pfnDynamicConstantBufferUnmap = device->wddm20_functions.pfnDynamicConstantBufferUnmap ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, DynamicConstantBufferUnmap) : NULL;
    device->functions.pfnIaSetInputLayout = device->wddm20_functions.pfnIaSetInputLayout ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, IaSetInputLayout) : NULL;
    device->functions.pfnIaSetVertexBuffers = device->wddm20_functions.pfnIaSetVertexBuffers ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, IaSetVertexBuffers) : NULL;
    device->functions.pfnIaSetIndexBuffer = device->wddm20_functions.pfnIaSetIndexBuffer ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, IaSetIndexBuffer) : NULL;
    device->functions.pfnDrawIndexedInstanced = device->wddm20_functions.pfnDrawIndexedInstanced ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, DrawIndexedInstanced) : NULL;
    device->functions.pfnDrawInstanced = device->wddm20_functions.pfnDrawInstanced ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, DrawInstanced) : NULL;
    device->functions.pfnDynamicResourceMapDiscard = device->wddm20_functions.pfnDynamicResourceMapDiscard ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, DynamicResourceMapDiscard) : NULL;
    device->functions.pfnDynamicResourceUnmap = device->wddm20_functions.pfnDynamicResourceUnmap ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, DynamicResourceUnmap) : NULL;
    device->functions.pfnGsSetShader = device->wddm20_functions.pfnGsSetShader ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, GsSetShader) : NULL;
    device->functions.pfnIaSetTopology = device->wddm20_functions.pfnIaSetTopology ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, IaSetTopology) : NULL;
    device->functions.pfnStagingResourceMap = device->wddm20_functions.pfnStagingResourceMap ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, StagingResourceMap) : NULL;
    device->functions.pfnStagingResourceUnmap = device->wddm20_functions.pfnStagingResourceUnmap ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, StagingResourceUnmap) : NULL;
    device->functions.pfnVsSetShaderResources = device->wddm20_functions.pfnVsSetShaderResources ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, VsSetShaderResources) : NULL;
    device->functions.pfnVsSetSamplers = device->wddm20_functions.pfnVsSetSamplers ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, VsSetSamplers) : NULL;
    device->functions.pfnGsSetShaderResources = device->wddm20_functions.pfnGsSetShaderResources ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, GsSetShaderResources) : NULL;
    device->functions.pfnGsSetSamplers = device->wddm20_functions.pfnGsSetSamplers ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, GsSetSamplers) : NULL;
    device->functions.pfnSetRenderTargets = device->wddm20_functions.pfnSetRenderTargets ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, SetRenderTargets) : NULL;
    device->functions.pfnShaderResourceViewReadAfterWriteHazard = device->wddm20_functions.pfnShaderResourceViewReadAfterWriteHazard ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, ShaderResourceViewReadAfterWriteHazard) : NULL;
    device->functions.pfnResourceReadAfterWriteHazard = device->wddm20_functions.pfnResourceReadAfterWriteHazard ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, ResourceReadAfterWriteHazard) : NULL;
    device->functions.pfnSetBlendState = device->wddm20_functions.pfnSetBlendState ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, SetBlendState) : NULL;
    device->functions.pfnSetDepthStencilState = device->wddm20_functions.pfnSetDepthStencilState ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, SetDepthStencilState) : NULL;
    device->functions.pfnSetRasterizerState = device->wddm20_functions.pfnSetRasterizerState ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, SetRasterizerState) : NULL;
    device->functions.pfnQueryEnd = device->wddm20_functions.pfnQueryEnd ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, QueryEnd) : NULL;
    device->functions.pfnQueryBegin = device->wddm20_functions.pfnQueryBegin ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, QueryBegin) : NULL;
    device->functions.pfnSoSetTargets = device->wddm20_functions.pfnSoSetTargets ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, SoSetTargets) : NULL;
    device->functions.pfnDrawAuto = device->wddm20_functions.pfnDrawAuto ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, DrawAuto) : NULL;
    device->functions.pfnSetViewports = device->wddm20_functions.pfnSetViewports ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, SetViewports) : NULL;
    device->functions.pfnSetScissorRects = device->wddm20_functions.pfnSetScissorRects ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, SetScissorRects) : NULL;
    device->functions.pfnClearRenderTargetView = device->wddm20_functions.pfnClearRenderTargetView ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, ClearRenderTargetView) : NULL;
    device->functions.pfnClearDepthStencilView = device->wddm20_functions.pfnClearDepthStencilView ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, ClearDepthStencilView) : NULL;
    device->functions.pfnSetPredication = device->wddm20_functions.pfnSetPredication ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, SetPredication) : NULL;
    device->functions.pfnQueryGetData = device->wddm20_functions.pfnQueryGetData ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, QueryGetData) : NULL;
    device->functions.pfnGenMips = device->wddm20_functions.pfnGenMips ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, GenMips) : NULL;
    device->functions.pfnResourceCopy = device->wddm20_functions.pfnResourceCopy ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, ResourceCopy) : NULL;
    device->functions.pfnResourceResolveSubresource = device->wddm20_functions.pfnResourceResolveSubresource ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, ResourceResolveSubresource) : NULL;
    device->functions.pfnResourceMap = device->wddm20_functions.pfnResourceMap ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, ResourceMap) : NULL;
    device->functions.pfnResourceUnmap = device->wddm20_functions.pfnResourceUnmap ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, ResourceUnmap) : NULL;
    device->functions.pfnResourceIsStagingBusy = device->wddm20_functions.pfnResourceIsStagingBusy ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, ResourceIsStagingBusy) : NULL;
    device->functions.pfnCalcPrivateResourceSize = device->wddm20_functions.pfnCalcPrivateResourceSize;
    device->functions.pfnCalcPrivateOpenedResourceSize = device->wddm20_functions.pfnCalcPrivateOpenedResourceSize;
    device->functions.pfnCreateResource = device->wddm20_functions.pfnCreateResource;
    device->functions.pfnOpenResource = device->wddm20_functions.pfnOpenResource;
    device->functions.pfnDestroyResource = device->wddm20_functions.pfnDestroyResource;
    device->functions.pfnDestroyShaderResourceView = device->wddm20_functions.pfnDestroyShaderResourceView;
    device->functions.pfnDestroyRenderTargetView = device->wddm20_functions.pfnDestroyRenderTargetView;
    device->functions.pfnCalcPrivateDepthStencilViewSize = device->wddm20_functions.pfnCalcPrivateDepthStencilViewSize;
    device->functions.pfnCreateDepthStencilView = device->wddm20_functions.pfnCreateDepthStencilView;
    device->functions.pfnDestroyDepthStencilView = device->wddm20_functions.pfnDestroyDepthStencilView;
    device->functions.pfnCalcPrivateElementLayoutSize = device->wddm20_functions.pfnCalcPrivateElementLayoutSize;
    device->functions.pfnCreateElementLayout = device->wddm20_functions.pfnCreateElementLayout;
    device->functions.pfnDestroyElementLayout = device->wddm20_functions.pfnDestroyElementLayout;
    device->functions.pfnDestroyBlendState = device->wddm20_functions.pfnDestroyBlendState;
    device->functions.pfnCalcPrivateDepthStencilStateSize = device->wddm20_functions.pfnCalcPrivateDepthStencilStateSize;
    device->functions.pfnCreateDepthStencilState = device->wddm20_functions.pfnCreateDepthStencilState;
    device->functions.pfnDestroyDepthStencilState = device->wddm20_functions.pfnDestroyDepthStencilState;
    device->functions.pfnDestroyRasterizerState = device->wddm20_functions.pfnDestroyRasterizerState;
    device->functions.pfnDestroyShader = device->wddm20_functions.pfnDestroyShader;
    device->functions.pfnCalcPrivateSamplerSize = device->wddm20_functions.pfnCalcPrivateSamplerSize;
    device->functions.pfnCreateSampler = device->wddm20_functions.pfnCreateSampler;
    device->functions.pfnDestroySampler = device->wddm20_functions.pfnDestroySampler;
    device->functions.pfnDestroyQuery = device->wddm20_functions.pfnDestroyQuery;
    device->functions.pfnCheckFormatSupport = device->wddm20_functions.pfnCheckFormatSupport;
    device->functions.pfnCheckCounterInfo = device->wddm20_functions.pfnCheckCounterInfo;
    device->functions.pfnCheckCounter = device->wddm20_functions.pfnCheckCounter;
    device->functions.pfnDestroyDevice = device->wddm20_functions.pfnDestroyDevice;
    device->functions.pfnSetTextFilterSize = device->wddm20_functions.pfnSetTextFilterSize ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, SetTextFilterSize) : NULL;
    device->functions.pfnResourceConvert = device->wddm20_functions.pfnResourceConvert ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, ResourceConvert) : NULL;
    device->functions.pfnDrawIndexedInstancedIndirect = device->wddm20_functions.pfnDrawIndexedInstancedIndirect ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, DrawIndexedInstancedIndirect) : NULL;
    device->functions.pfnDrawInstancedIndirect = device->wddm20_functions.pfnDrawInstancedIndirect ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, DrawInstancedIndirect) : NULL;
    device->functions.pfnCommandListExecute = device->wddm20_functions.pfnCommandListExecute ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, CommandListExecute) : NULL;
    device->functions.pfnHsSetShaderResources = device->wddm20_functions.pfnHsSetShaderResources ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, HsSetShaderResources) : NULL;
    device->functions.pfnHsSetShader = device->wddm20_functions.pfnHsSetShader ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, HsSetShader) : NULL;
    device->functions.pfnHsSetSamplers = device->wddm20_functions.pfnHsSetSamplers ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, HsSetSamplers) : NULL;
    device->functions.pfnDsSetShaderResources = device->wddm20_functions.pfnDsSetShaderResources ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, DsSetShaderResources) : NULL;
    device->functions.pfnDsSetShader = device->wddm20_functions.pfnDsSetShader ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, DsSetShader) : NULL;
    device->functions.pfnDsSetSamplers = device->wddm20_functions.pfnDsSetSamplers ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, DsSetSamplers) : NULL;
    device->functions.pfnCheckDeferredContextHandleSizes = device->wddm20_functions.pfnCheckDeferredContextHandleSizes;
    device->functions.pfnCalcDeferredContextHandleSize = device->wddm20_functions.pfnCalcDeferredContextHandleSize ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, CalcDeferredContextHandleSize) : NULL;
    device->functions.pfnCalcPrivateDeferredContextSize = device->wddm20_functions.pfnCalcPrivateDeferredContextSize;
    device->functions.pfnCreateDeferredContext = device->wddm20_functions.pfnCreateDeferredContext;
    device->functions.pfnAbandonCommandList = device->wddm20_functions.pfnAbandonCommandList ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, AbandonCommandList) : NULL;
    device->functions.pfnCalcPrivateCommandListSize = device->wddm20_functions.pfnCalcPrivateCommandListSize;
    device->functions.pfnCreateCommandList = device->wddm20_functions.pfnCreateCommandList;
    device->functions.pfnDestroyCommandList = device->wddm20_functions.pfnDestroyCommandList;
    device->functions.pfnPsSetShaderWithIfaces = device->wddm20_functions.pfnPsSetShaderWithIfaces ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, PsSetShaderWithIfaces) : NULL;
    device->functions.pfnVsSetShaderWithIfaces = device->wddm20_functions.pfnVsSetShaderWithIfaces ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, VsSetShaderWithIfaces) : NULL;
    device->functions.pfnGsSetShaderWithIfaces = device->wddm20_functions.pfnGsSetShaderWithIfaces ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, GsSetShaderWithIfaces) : NULL;
    device->functions.pfnHsSetShaderWithIfaces = device->wddm20_functions.pfnHsSetShaderWithIfaces ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, HsSetShaderWithIfaces) : NULL;
    device->functions.pfnDsSetShaderWithIfaces = device->wddm20_functions.pfnDsSetShaderWithIfaces ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, DsSetShaderWithIfaces) : NULL;
    device->functions.pfnCsSetShaderWithIfaces = device->wddm20_functions.pfnCsSetShaderWithIfaces ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, CsSetShaderWithIfaces) : NULL;
    device->functions.pfnCreateComputeShader = device->wddm20_functions.pfnCreateComputeShader;
    device->functions.pfnCsSetShader = device->wddm20_functions.pfnCsSetShader ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, CsSetShader) : NULL;
    device->functions.pfnCsSetShaderResources = device->wddm20_functions.pfnCsSetShaderResources ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, CsSetShaderResources) : NULL;
    device->functions.pfnCsSetSamplers = device->wddm20_functions.pfnCsSetSamplers ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, CsSetSamplers) : NULL;
    device->functions.pfnDestroyUnorderedAccessView = device->wddm20_functions.pfnDestroyUnorderedAccessView;
    device->functions.pfnClearUnorderedAccessViewUint = device->wddm20_functions.pfnClearUnorderedAccessViewUint ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, ClearUnorderedAccessViewUint) : NULL;
    device->functions.pfnClearUnorderedAccessViewFloat = device->wddm20_functions.pfnClearUnorderedAccessViewFloat ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, ClearUnorderedAccessViewFloat) : NULL;
    device->functions.pfnCsSetUnorderedAccessViews = device->wddm20_functions.pfnCsSetUnorderedAccessViews ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, CsSetUnorderedAccessViews) : NULL;
    device->functions.pfnDispatch = device->wddm20_functions.pfnDispatch ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, Dispatch) : NULL;
    device->functions.pfnDispatchIndirect = device->wddm20_functions.pfnDispatchIndirect ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, DispatchIndirect) : NULL;
    device->functions.pfnSetResourceMinLOD = device->wddm20_functions.pfnSetResourceMinLOD ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, SetResourceMinLOD) : NULL;
    device->functions.pfnCopyStructureCount = device->wddm20_functions.pfnCopyStructureCount ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, CopyStructureCount) : NULL;
    device->functions.pfnRecycleCommandList = device->wddm20_functions.pfnRecycleCommandList ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, RecycleCommandList) : NULL;
    device->functions.pfnRecycleCreateCommandList = device->wddm20_functions.pfnRecycleCreateCommandList ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, RecycleCreateCommandList) : NULL;
    device->functions.pfnRecycleCreateDeferredContext = device->wddm20_functions.pfnRecycleCreateDeferredContext ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, RecycleCreateDeferredContext) : NULL;
    device->functions.pfnRecycleDestroyCommandList = device->wddm20_functions.pfnRecycleDestroyCommandList ? NATIVE_LIVE(D3DWDDM2_0DDI_DEVICEFUNCS, RecycleDestroyCommandList) : NULL;
    device->functions.pfnVsSetConstantBuffers = device->wddm20_functions.pfnVsSetConstantBuffers ? NativeWddm20VsSetConstantBuffers : NULL;
    device->functions.pfnPsSetConstantBuffers = device->wddm20_functions.pfnPsSetConstantBuffers ? NativeWddm20PsSetConstantBuffers : NULL;
    device->functions.pfnGsSetConstantBuffers = device->wddm20_functions.pfnGsSetConstantBuffers ? NativeWddm20GsSetConstantBuffers : NULL;
    device->functions.pfnHsSetConstantBuffers = device->wddm20_functions.pfnHsSetConstantBuffers ? NativeWddm20HsSetConstantBuffers : NULL;
    device->functions.pfnDsSetConstantBuffers = device->wddm20_functions.pfnDsSetConstantBuffers ? NativeWddm20DsSetConstantBuffers : NULL;
    device->functions.pfnCsSetConstantBuffers = device->wddm20_functions.pfnCsSetConstantBuffers ? NativeWddm20CsSetConstantBuffers : NULL;
    device->functions.pfnDefaultConstantBufferUpdateSubresourceUP = device->wddm20_functions.pfnDefaultConstantBufferUpdateSubresourceUP ? NativeWddm20DefaultConstantBufferUpdateSubresourceUP : NULL;
    device->functions.pfnResourceUpdateSubresourceUP = device->wddm20_functions.pfnResourceUpdateSubresourceUP ? NativeWddm20ResourceUpdateSubresourceUP : NULL;
    device->functions.pfnResourceCopyRegion = device->wddm20_functions.pfnResourceCopyRegion ? NativeWddm20ResourceCopyRegion : NULL;
    device->functions.pfnResourceConvertRegion = device->wddm20_functions.pfnResourceConvertRegion ? NativeWddm20ResourceConvertRegion : NULL;
    device->functions.pfnFlush = device->wddm20_functions.pfnFlush ? NativeWddm20Flush : NULL;
    device->functions.pfnCheckMultisampleQualityLevels = device->wddm20_functions.pfnCheckMultisampleQualityLevels ? NativeWddm20CheckMultisampleQualityLevels : NULL;
    device->functions.pfnCalcPrivateBlendStateSize = device->wddm20_functions.pfnCalcPrivateBlendStateSize ? NativeWddm20CalcPrivateBlendStateSize : NULL;
    device->functions.pfnCreateBlendState = device->wddm20_functions.pfnCreateBlendState ? NativeWddm20CreateBlendState : NULL;
    device->functions.pfnCalcPrivateRasterizerStateSize = device->wddm20_functions.pfnCalcPrivateRasterizerStateSize ? NativeWddm20CalcPrivateRasterizerStateSize : NULL;
    device->functions.pfnCreateRasterizerState = device->wddm20_functions.pfnCreateRasterizerState ? NativeWddm20CreateRasterizerState : NULL;
    device->functions.pfnCalcPrivateQuerySize = device->wddm20_functions.pfnCalcPrivateQuerySize ? NativeWddm20CalcPrivateQuerySize : NULL;
    device->functions.pfnCreateQuery = device->wddm20_functions.pfnCreateQuery ? NativeWddm20CreateQuery : NULL;
    device->functions.pfnCalcPrivateRenderTargetViewSize = device->wddm20_functions.pfnCalcPrivateRenderTargetViewSize ? NativeWddm20CalcPrivateRenderTargetViewSize : NULL;
    device->functions.pfnCreateRenderTargetView = device->wddm20_functions.pfnCreateRenderTargetView ? NativeWddm20CreateRenderTargetView : NULL;
    device->functions.pfnCalcPrivateShaderResourceViewSize = device->wddm20_functions.pfnCalcPrivateShaderResourceViewSize ? NativeWddm20CalcPrivateShaderResourceViewSize : NULL;
    device->functions.pfnCreateShaderResourceView = device->wddm20_functions.pfnCreateShaderResourceView ? NativeWddm20CreateShaderResourceView : NULL;
    device->functions.pfnCalcPrivateUnorderedAccessViewSize = device->wddm20_functions.pfnCalcPrivateUnorderedAccessViewSize ? NativeWddm20CalcPrivateUnorderedAccessViewSize : NULL;
    device->functions.pfnCreateUnorderedAccessView = device->wddm20_functions.pfnCreateUnorderedAccessView ? NativeWddm20CreateUnorderedAccessView : NULL;
    static_assert(offsetof(DXGI1_4_DDI_BASE_FUNCTIONS, pfnBlt1) == sizeof(DXGI1_1_DDI_BASE_FUNCTIONS), "DXGI base prefix");
    memcpy(&device->dxgi_functions, &device->wddm20_dxgi, sizeof(device->dxgi_functions));
}
