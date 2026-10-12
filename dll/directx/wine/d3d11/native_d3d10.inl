/*
 * PROJECT:     LiberNT Direct3D 11 runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Direct3D 10 device and object interfaces on native Direct3D 11 objects
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

static_assert(sizeof(D3D10_SAMPLER_DESC) == sizeof(D3D11_SAMPLER_DESC), "D3D10 sampler layout");
static_assert(sizeof(D3D10_RASTERIZER_DESC) == sizeof(D3D11_RASTERIZER_DESC), "D3D10 rasterizer layout");
static_assert(sizeof(D3D10_DEPTH_STENCIL_DESC) == sizeof(D3D11_DEPTH_STENCIL_DESC), "D3D10 depth-stencil layout");
static_assert(sizeof(D3D10_BLEND_DESC1) == sizeof(D3D11_BLEND_DESC), "D3D10.1 blend layout");
static_assert(sizeof(D3D10_RENDER_TARGET_VIEW_DESC) == sizeof(D3D11_RENDER_TARGET_VIEW_DESC), "D3D10 RTV layout");
static_assert(sizeof(D3D10_SHADER_RESOURCE_VIEW_DESC1) <= sizeof(D3D11_SHADER_RESOURCE_VIEW_DESC), "D3D10.1 SRV layout");
static_assert(sizeof(D3D10_INPUT_ELEMENT_DESC) == sizeof(D3D11_INPUT_ELEMENT_DESC), "D3D10 input element layout");
static_assert(sizeof(D3D10_SUBRESOURCE_DATA) == sizeof(D3D11_SUBRESOURCE_DATA), "D3D10 initial data layout");
static_assert(sizeof(D3D10_BOX) == sizeof(D3D11_BOX), "D3D10 box layout");
static_assert(sizeof(D3D10_QUERY_DESC) == sizeof(D3D11_QUERY_DESC), "D3D10 query layout");
static_assert(sizeof(D3D10_QUERY_DATA_PIPELINE_STATISTICS) <= sizeof(D3D11_QUERY_DATA_PIPELINE_STATISTICS),
        "D3D10 pipeline statistics prefix");

static const UINT native_d3d10_misc_flags = D3D10_RESOURCE_MISC_GENERATE_MIPS | D3D10_RESOURCE_MISC_SHARED
        | D3D10_RESOURCE_MISC_TEXTURECUBE | D3D10_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D10_RESOURCE_MISC_GDI_COMPATIBLE;

static UINT NativeMiscFlagsFrom10(UINT flags)
{
    UINT result = flags & (D3D10_RESOURCE_MISC_GENERATE_MIPS | D3D10_RESOURCE_MISC_SHARED
            | D3D10_RESOURCE_MISC_TEXTURECUBE);
    if (flags & D3D10_RESOURCE_MISC_SHARED_KEYEDMUTEX) result |= D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    if (flags & D3D10_RESOURCE_MISC_GDI_COMPATIBLE) result |= D3D11_RESOURCE_MISC_GDI_COMPATIBLE;
    return result;
}

static UINT NativeMiscFlagsTo10(UINT flags)
{
    UINT result = flags & (D3D11_RESOURCE_MISC_GENERATE_MIPS | D3D11_RESOURCE_MISC_SHARED
            | D3D11_RESOURCE_MISC_TEXTURECUBE);
    if (flags & D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX) result |= D3D10_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    if (flags & D3D11_RESOURCE_MISC_GDI_COMPATIBLE) result |= D3D10_RESOURCE_MISC_GDI_COMPATIBLE;
    return result;
}

template<class T> static T *NativeAs(IUnknown *object, REFIID iid)
{
    T *result = NULL;
    if (object) object->QueryInterface(iid, reinterpret_cast<void **>(&result));
    return result;
}

template<class T> static T *NativeAsReleasing(IUnknown *object, REFIID iid)
{
    T *result = NativeAs<T>(object, iid);
    if (object) object->Release();
    return result;
}

template<class T> static void NativeReleaseAll(T **objects, UINT count)
{
    for (UINT i = 0; i < count; ++i)
        if (objects[i]) objects[i]->Release();
}

template<class T11, class T10> static void NativeFrom10(T10 *const *objects, UINT count, T11 **out, REFIID iid)
{
    for (UINT i = 0; i < count; ++i) out[i] = objects ? NativeAs<T11>(objects[i], iid) : NULL;
}

template<class T10, class T11> static void NativeTo10(T11 **objects, UINT count, T10 **out, REFIID iid)
{
    for (UINT i = 0; i < count; ++i) out[i] = NativeAsReleasing<T10>(objects[i], iid);
}

static void NativeD3D10GetDevice(NativeDevice *device, ID3D10Device **out);

template<class Interface10, class Interface11>
class NativeD3D10Child : public Interface10, public NativeD3D10Facet
{
protected:
    Interface11 *object;
    NativeDevice *device;
public:
    NativeD3D10Child(Interface11 *owner, NativeDevice *d) : object(owner), device(d) {}
    IUnknown *Interface() { return static_cast<Interface10 *>(this); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override { return object->QueryInterface(iid, out); }
    ULONG STDMETHODCALLTYPE AddRef() override { return object->AddRef(); }
    ULONG STDMETHODCALLTYPE Release() override { return object->Release(); }
    void STDMETHODCALLTYPE GetDevice(ID3D10Device **out) override { NativeD3D10GetDevice(device, out); }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT *size, void *data) override
    {
        return object->GetPrivateData(guid, size, data);
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT size, const void *data) override
    {
        return object->SetPrivateData(guid, size, data);
    }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *data) override
    {
        return object->SetPrivateDataInterface(guid, data);
    }
};

template<class Interface10, class Interface11, D3D10_RESOURCE_DIMENSION Dimension>
class NativeD3D10Resource : public NativeD3D10Child<Interface10, Interface11>
{
public:
    NativeD3D10Resource(Interface11 *owner, NativeDevice *d) : NativeD3D10Child<Interface10, Interface11>(owner, d) {}
    void STDMETHODCALLTYPE GetType(D3D10_RESOURCE_DIMENSION *out) override { if (out) *out = Dimension; }
    void STDMETHODCALLTYPE SetEvictionPriority(UINT value) override { this->object->SetEvictionPriority(value); }
    UINT STDMETHODCALLTYPE GetEvictionPriority() override { return this->object->GetEvictionPriority(); }
    HRESULT MapSubresource(UINT subresource, D3D10_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE *mapped)
    {
        return this->device->context->Map(this->object, subresource, static_cast<D3D11_MAP>(type), flags, mapped);
    }
    void UnmapSubresource(UINT subresource) { this->device->context->Unmap(this->object, subresource); }
};

class NativeD3D10Buffer final : public NativeD3D10Resource<ID3D10Buffer, ID3D11Buffer, D3D10_RESOURCE_DIMENSION_BUFFER>
{
public:
    explicit NativeD3D10Buffer(NativeBuffer *owner) : NativeD3D10Resource(owner, owner->device) {}
    HRESULT STDMETHODCALLTYPE Map(D3D10_MAP type, UINT flags, void **data) override
    {
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr = MapSubresource(0, type, flags, &mapped);
        if (data) *data = mapped.pData;
        return hr;
    }
    void STDMETHODCALLTYPE Unmap() override { UnmapSubresource(0); }
    void STDMETHODCALLTYPE GetDesc(D3D10_BUFFER_DESC *out) override
    {
        if (!out) return;
        D3D11_BUFFER_DESC desc;
        object->GetDesc(&desc);
        out->ByteWidth = desc.ByteWidth;
        out->Usage = static_cast<D3D10_USAGE>(desc.Usage);
        out->BindFlags = desc.BindFlags;
        out->CPUAccessFlags = desc.CPUAccessFlags;
        out->MiscFlags = NativeMiscFlagsTo10(desc.MiscFlags);
    }
};

class NativeD3D10Texture1D final
        : public NativeD3D10Resource<ID3D10Texture1D, ID3D11Texture1D, D3D10_RESOURCE_DIMENSION_TEXTURE1D>
{
public:
    explicit NativeD3D10Texture1D(NativeTexture1D *owner) : NativeD3D10Resource(owner, owner->device) {}
    HRESULT STDMETHODCALLTYPE Map(UINT subresource, D3D10_MAP type, UINT flags, void **data) override
    {
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr = MapSubresource(subresource, type, flags, &mapped);
        if (data) *data = mapped.pData;
        return hr;
    }
    void STDMETHODCALLTYPE Unmap(UINT subresource) override { UnmapSubresource(subresource); }
    void STDMETHODCALLTYPE GetDesc(D3D10_TEXTURE1D_DESC *out) override
    {
        if (!out) return;
        D3D11_TEXTURE1D_DESC desc;
        object->GetDesc(&desc);
        out->Width = desc.Width;
        out->MipLevels = desc.MipLevels;
        out->ArraySize = desc.ArraySize;
        out->Format = desc.Format;
        out->Usage = static_cast<D3D10_USAGE>(desc.Usage);
        out->BindFlags = desc.BindFlags;
        out->CPUAccessFlags = desc.CPUAccessFlags;
        out->MiscFlags = NativeMiscFlagsTo10(desc.MiscFlags);
    }
};

class NativeD3D10Texture2D final
        : public NativeD3D10Resource<ID3D10Texture2D, ID3D11Texture2D, D3D10_RESOURCE_DIMENSION_TEXTURE2D>
{
public:
    explicit NativeD3D10Texture2D(NativeTexture2D *owner) : NativeD3D10Resource(owner, owner->device) {}
    HRESULT STDMETHODCALLTYPE Map(UINT subresource, D3D10_MAP type, UINT flags, D3D10_MAPPED_TEXTURE2D *data) override
    {
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr = MapSubresource(subresource, type, flags, &mapped);
        if (data)
        {
            data->pData = mapped.pData;
            data->RowPitch = mapped.RowPitch;
        }
        return hr;
    }
    void STDMETHODCALLTYPE Unmap(UINT subresource) override { UnmapSubresource(subresource); }
    void STDMETHODCALLTYPE GetDesc(D3D10_TEXTURE2D_DESC *out) override
    {
        if (!out) return;
        D3D11_TEXTURE2D_DESC desc;
        object->GetDesc(&desc);
        out->Width = desc.Width;
        out->Height = desc.Height;
        out->MipLevels = desc.MipLevels;
        out->ArraySize = desc.ArraySize;
        out->Format = desc.Format;
        out->SampleDesc = desc.SampleDesc;
        out->Usage = static_cast<D3D10_USAGE>(desc.Usage);
        out->BindFlags = desc.BindFlags;
        out->CPUAccessFlags = desc.CPUAccessFlags;
        out->MiscFlags = NativeMiscFlagsTo10(desc.MiscFlags);
    }
};

class NativeD3D10Texture3D final
        : public NativeD3D10Resource<ID3D10Texture3D, ID3D11Texture3D, D3D10_RESOURCE_DIMENSION_TEXTURE3D>
{
public:
    explicit NativeD3D10Texture3D(NativeTexture3D *owner) : NativeD3D10Resource(owner, owner->device) {}
    HRESULT STDMETHODCALLTYPE Map(UINT subresource, D3D10_MAP type, UINT flags, D3D10_MAPPED_TEXTURE3D *data) override
    {
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr = MapSubresource(subresource, type, flags, &mapped);
        if (data)
        {
            data->pData = mapped.pData;
            data->RowPitch = mapped.RowPitch;
            data->DepthPitch = mapped.DepthPitch;
        }
        return hr;
    }
    void STDMETHODCALLTYPE Unmap(UINT subresource) override { UnmapSubresource(subresource); }
    void STDMETHODCALLTYPE GetDesc(D3D10_TEXTURE3D_DESC *out) override
    {
        if (!out) return;
        D3D11_TEXTURE3D_DESC desc;
        object->GetDesc(&desc);
        out->Width = desc.Width;
        out->Height = desc.Height;
        out->Depth = desc.Depth;
        out->MipLevels = desc.MipLevels;
        out->Format = desc.Format;
        out->Usage = static_cast<D3D10_USAGE>(desc.Usage);
        out->BindFlags = desc.BindFlags;
        out->CPUAccessFlags = desc.CPUAccessFlags;
        out->MiscFlags = NativeMiscFlagsTo10(desc.MiscFlags);
    }
};

template<class Interface10, class Interface11>
class NativeD3D10View : public NativeD3D10Child<Interface10, Interface11>
{
public:
    NativeD3D10View(Interface11 *owner, NativeDevice *d) : NativeD3D10Child<Interface10, Interface11>(owner, d) {}
    void STDMETHODCALLTYPE GetResource(ID3D10Resource **out) override
    {
        if (!out) return;
        ID3D11Resource *resource = NULL;
        this->object->GetResource(&resource);
        *out = NativeAsReleasing<ID3D10Resource>(resource, IID_ID3D10Resource);
    }
};

class NativeD3D10ShaderResourceView final : public NativeD3D10View<ID3D10ShaderResourceView1, ID3D11ShaderResourceView>
{
public:
    explicit NativeD3D10ShaderResourceView(NativeShaderResourceView *owner) : NativeD3D10View(owner, owner->device) {}
    void STDMETHODCALLTYPE GetDesc(D3D10_SHADER_RESOURCE_VIEW_DESC *out) override
    {
        if (!out) return;
        D3D11_SHADER_RESOURCE_VIEW_DESC desc;
        object->GetDesc(&desc);
        memcpy(out, &desc, sizeof(*out));
    }
    void STDMETHODCALLTYPE GetDesc1(D3D10_SHADER_RESOURCE_VIEW_DESC1 *out) override
    {
        if (!out) return;
        D3D11_SHADER_RESOURCE_VIEW_DESC desc;
        object->GetDesc(&desc);
        memcpy(out, &desc, sizeof(*out));
    }
};

class NativeD3D10RenderTargetView final : public NativeD3D10View<ID3D10RenderTargetView, ID3D11RenderTargetView>
{
public:
    explicit NativeD3D10RenderTargetView(NativeRenderTargetView *owner) : NativeD3D10View(owner, owner->device) {}
    void STDMETHODCALLTYPE GetDesc(D3D10_RENDER_TARGET_VIEW_DESC *out) override
    {
        if (!out) return;
        D3D11_RENDER_TARGET_VIEW_DESC desc;
        object->GetDesc(&desc);
        memcpy(out, &desc, sizeof(*out));
    }
};

class NativeD3D10DepthStencilView final : public NativeD3D10View<ID3D10DepthStencilView, ID3D11DepthStencilView>
{
public:
    explicit NativeD3D10DepthStencilView(NativeDepthView *owner) : NativeD3D10View(owner, owner->device) {}
    void STDMETHODCALLTYPE GetDesc(D3D10_DEPTH_STENCIL_VIEW_DESC *out) override
    {
        if (!out) return;
        D3D11_DEPTH_STENCIL_VIEW_DESC desc;
        object->GetDesc(&desc);
        memset(out, 0, sizeof(*out));
        out->Format = desc.Format;
        out->ViewDimension = static_cast<D3D10_DSV_DIMENSION>(desc.ViewDimension);
        memcpy(&out->Texture2DArray, &desc.Texture2DArray, sizeof(out->Texture2DArray));
    }
};

template<class Interface10, class Interface11>
class NativeD3D10Plain final : public NativeD3D10Child<Interface10, Interface11>
{
public:
    NativeD3D10Plain(Interface11 *owner, NativeDevice *d) : NativeD3D10Child<Interface10, Interface11>(owner, d) {}
};

template<class Interface10, class Desc10, class Interface11, class Desc11>
class NativeD3D10State final : public NativeD3D10Child<Interface10, Interface11>
{
public:
    NativeD3D10State(Interface11 *owner, NativeDevice *d) : NativeD3D10Child<Interface10, Interface11>(owner, d) {}
    void STDMETHODCALLTYPE GetDesc(Desc10 *out) override
    {
        if (!out) return;
        Desc11 desc;
        this->object->GetDesc(&desc);
        memcpy(out, &desc, sizeof(*out));
    }
};

class NativeD3D10BlendState final : public NativeD3D10Child<ID3D10BlendState1, ID3D11BlendState>
{
public:
    explicit NativeD3D10BlendState(NativeBlend *owner) : NativeD3D10Child(owner, owner->device) {}
    void STDMETHODCALLTYPE GetDesc(D3D10_BLEND_DESC *out) override
    {
        if (!out) return;
        D3D11_BLEND_DESC desc;
        object->GetDesc(&desc);
        out->AlphaToCoverageEnable = desc.AlphaToCoverageEnable;
        out->SrcBlend = static_cast<D3D10_BLEND>(desc.RenderTarget[0].SrcBlend);
        out->DestBlend = static_cast<D3D10_BLEND>(desc.RenderTarget[0].DestBlend);
        out->BlendOp = static_cast<D3D10_BLEND_OP>(desc.RenderTarget[0].BlendOp);
        out->SrcBlendAlpha = static_cast<D3D10_BLEND>(desc.RenderTarget[0].SrcBlendAlpha);
        out->DestBlendAlpha = static_cast<D3D10_BLEND>(desc.RenderTarget[0].DestBlendAlpha);
        out->BlendOpAlpha = static_cast<D3D10_BLEND_OP>(desc.RenderTarget[0].BlendOpAlpha);
        for (UINT i = 0; i < D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
        {
            UINT target = desc.IndependentBlendEnable ? i : 0;
            out->BlendEnable[i] = desc.RenderTarget[target].BlendEnable;
            out->RenderTargetWriteMask[i] = desc.RenderTarget[target].RenderTargetWriteMask;
        }
    }
    void STDMETHODCALLTYPE GetDesc1(D3D10_BLEND_DESC1 *out) override
    {
        if (!out) return;
        D3D11_BLEND_DESC desc;
        object->GetDesc(&desc);
        memcpy(out, &desc, sizeof(*out));
    }
};

class NativeD3D10Query final : public NativeD3D10Child<ID3D10Predicate, ID3D11Query>
{
    bool PipelineStatistics()
    {
        D3D11_QUERY_DESC desc;
        object->GetDesc(&desc);
        return desc.Query == D3D11_QUERY_PIPELINE_STATISTICS;
    }
public:
    explicit NativeD3D10Query(NativeQuery *owner) : NativeD3D10Child(owner, owner->device) {}
    void STDMETHODCALLTYPE Begin() override { device->context->Begin(object); }
    void STDMETHODCALLTYPE End() override { device->context->End(object); }
    HRESULT STDMETHODCALLTYPE GetData(void *data, UINT size, UINT flags) override
    {
        if (!PipelineStatistics()) return device->context->GetData(object, data, size, flags);
        if (data && size != sizeof(D3D10_QUERY_DATA_PIPELINE_STATISTICS)) return E_INVALIDARG;
        D3D11_QUERY_DATA_PIPELINE_STATISTICS statistics = {};
        HRESULT hr = device->context->GetData(object, data ? &statistics : NULL, data ? sizeof(statistics) : 0, flags);
        if (hr == S_OK && data) memcpy(data, &statistics, sizeof(D3D10_QUERY_DATA_PIPELINE_STATISTICS));
        return hr;
    }
    UINT STDMETHODCALLTYPE GetDataSize() override
    {
        return PipelineStatistics() ? sizeof(D3D10_QUERY_DATA_PIPELINE_STATISTICS) : object->GetDataSize();
    }
    void STDMETHODCALLTYPE GetDesc(D3D10_QUERY_DESC *out) override
    {
        if (!out) return;
        D3D11_QUERY_DESC desc;
        object->GetDesc(&desc);
        out->Query = static_cast<D3D10_QUERY>(desc.Query);
        out->MiscFlags = desc.MiscFlags;
    }
};

using NativeD3D10InputLayout = NativeD3D10Plain<ID3D10InputLayout, ID3D11InputLayout>;
using NativeD3D10VertexShader = NativeD3D10Plain<ID3D10VertexShader, ID3D11VertexShader>;
using NativeD3D10GeometryShader = NativeD3D10Plain<ID3D10GeometryShader, ID3D11GeometryShader>;
using NativeD3D10PixelShader = NativeD3D10Plain<ID3D10PixelShader, ID3D11PixelShader>;
using NativeD3D10SamplerState = NativeD3D10State<ID3D10SamplerState, D3D10_SAMPLER_DESC,
        ID3D11SamplerState, D3D11_SAMPLER_DESC>;
using NativeD3D10RasterizerState = NativeD3D10State<ID3D10RasterizerState, D3D10_RASTERIZER_DESC,
        ID3D11RasterizerState, D3D11_RASTERIZER_DESC>;
using NativeD3D10DepthStencilState = NativeD3D10State<ID3D10DepthStencilState, D3D10_DEPTH_STENCIL_DESC,
        ID3D11DepthStencilState, D3D11_DEPTH_STENCIL_DESC>;

static bool NativeD3D10IsChild(REFIID iid, REFIID own)
{
    return IsEqualGUID(iid, own) || IsEqualGUID(iid, IID_ID3D10DeviceChild);
}

static bool NativeD3D10IsResource(REFIID iid, REFIID own)
{
    return NativeD3D10IsChild(iid, own) || IsEqualGUID(iid, IID_ID3D10Resource);
}

static bool NativeD3D10IsView(REFIID iid, REFIID own)
{
    return NativeD3D10IsChild(iid, own) || IsEqualGUID(iid, IID_ID3D10View);
}

IUnknown *NativeBuffer::D3D10Facet(REFIID iid)
{
    if (!NativeD3D10IsResource(iid, IID_ID3D10Buffer) || (desc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED))
        return NULL;
    return D3D10FacetOf<NativeD3D10Buffer>(this);
}

IUnknown *NativeTexture1D::D3D10Facet(REFIID iid)
{
    if (!NativeD3D10IsResource(iid, IID_ID3D10Texture1D) || (desc.MiscFlags & D3D11_RESOURCE_MISC_RESOURCE_CLAMP))
        return NULL;
    return D3D10FacetOf<NativeD3D10Texture1D>(this);
}

IUnknown *NativeTexture2D::D3D10Facet(REFIID iid)
{
    if (!NativeD3D10IsResource(iid, IID_ID3D10Texture2D) || (desc.MiscFlags & D3D11_RESOURCE_MISC_RESOURCE_CLAMP))
        return NULL;
    return D3D10FacetOf<NativeD3D10Texture2D>(this);
}

IUnknown *NativeTexture3D::D3D10Facet(REFIID iid)
{
    if (!NativeD3D10IsResource(iid, IID_ID3D10Texture3D) || (desc.MiscFlags & D3D11_RESOURCE_MISC_RESOURCE_CLAMP))
        return NULL;
    return D3D10FacetOf<NativeD3D10Texture3D>(this);
}

IUnknown *NativeShaderResourceView::D3D10Facet(REFIID iid)
{
    if (!NativeD3D10IsView(iid, IID_ID3D10ShaderResourceView) && !IsEqualGUID(iid, IID_ID3D10ShaderResourceView1))
        return NULL;
    return D3D10FacetOf<NativeD3D10ShaderResourceView>(this);
}

IUnknown *NativeRenderTargetView::D3D10Facet(REFIID iid)
{
    if (!NativeD3D10IsView(iid, IID_ID3D10RenderTargetView)) return NULL;
    return D3D10FacetOf<NativeD3D10RenderTargetView>(this);
}

IUnknown *NativeDepthView::D3D10Facet(REFIID iid)
{
    if (!NativeD3D10IsView(iid, IID_ID3D10DepthStencilView)) return NULL;
    return D3D10FacetOf<NativeD3D10DepthStencilView>(this);
}

IUnknown *NativeInputLayout::D3D10Facet(REFIID iid)
{
    if (!NativeD3D10IsChild(iid, IID_ID3D10InputLayout)) return NULL;
    return D3D10FacetOf<NativeD3D10InputLayout>(static_cast<ID3D11InputLayout *>(this), device);
}

IUnknown *NativeBlend::D3D10Facet(REFIID iid)
{
    if (!NativeD3D10IsChild(iid, IID_ID3D10BlendState) && !IsEqualGUID(iid, IID_ID3D10BlendState1)) return NULL;
    return D3D10FacetOf<NativeD3D10BlendState>(this);
}

IUnknown *NativeRasterizer::D3D10Facet(REFIID iid)
{
    if (!NativeD3D10IsChild(iid, IID_ID3D10RasterizerState)) return NULL;
    return D3D10FacetOf<NativeD3D10RasterizerState>(static_cast<ID3D11RasterizerState *>(this), device);
}

IUnknown *NativeQuery::D3D10Facet(REFIID iid)
{
    if (!NativeD3D10IsChild(iid, IID_ID3D10Query) && !IsEqualGUID(iid, IID_ID3D10Asynchronous)
            && !(is_predicate && IsEqualGUID(iid, IID_ID3D10Predicate))) return NULL;
    return D3D10FacetOf<NativeD3D10Query>(this);
}

template<class Shader> IUnknown *NativeD3D10ShaderFacet(Shader *, REFIID)
{
    return NULL;
}

IUnknown *NativeD3D10StateFacet(NativeSampler *state, REFIID iid)
{
    if (!NativeD3D10IsChild(iid, IID_ID3D10SamplerState)) return NULL;
    return state->D3D10FacetOf<NativeD3D10SamplerState>(static_cast<ID3D11SamplerState *>(state), state->device);
}

IUnknown *NativeD3D10StateFacet(NativeDepthStencil *state, REFIID iid)
{
    if (!NativeD3D10IsChild(iid, IID_ID3D10DepthStencilState)) return NULL;
    return state->D3D10FacetOf<NativeD3D10DepthStencilState>(static_cast<ID3D11DepthStencilState *>(state),
            state->device);
}

IUnknown *NativeD3D10ShaderFacet(NativeVertexShader *shader, REFIID iid)
{
    if (!NativeD3D10IsChild(iid, IID_ID3D10VertexShader)) return NULL;
    return shader->D3D10FacetOf<NativeD3D10VertexShader>(static_cast<ID3D11VertexShader *>(shader), shader->device);
}

IUnknown *NativeD3D10ShaderFacet(NativeGeometryShader *shader, REFIID iid)
{
    if (!NativeD3D10IsChild(iid, IID_ID3D10GeometryShader)) return NULL;
    return shader->D3D10FacetOf<NativeD3D10GeometryShader>(static_cast<ID3D11GeometryShader *>(shader),
            shader->device);
}

IUnknown *NativeD3D10ShaderFacet(NativePixelShader *shader, REFIID iid)
{
    if (!NativeD3D10IsChild(iid, IID_ID3D10PixelShader)) return NULL;
    return shader->D3D10FacetOf<NativeD3D10PixelShader>(static_cast<ID3D11PixelShader *>(shader), shader->device);
}

template<class Interface, const GUID *iid, class Desc, class Handle>
IUnknown *NativeState<Interface, iid, Desc, Handle>::D3D10Facet(REFIID requested)
{
    return NativeD3D10StateFacet(this, requested);
}

template<class Interface, const GUID *iid>
IUnknown *NativeShader<Interface, iid>::D3D10Facet(REFIID requested)
{
    return NativeD3D10ShaderFacet(this, requested);
}

class NativeD3D10Device final : public ID3D10Device1, public ID3D10Multithread
{
    NativeDevice *device;
    UINT text_filter_width = 0;
    UINT text_filter_height = 0;
    ID3D11DeviceContext4 *Context() { return device->context; }
    ID3D11Device5 *Device() { return device; }
public:
    explicit NativeD3D10Device(NativeDevice *d) : device(d) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override { return device->QueryInterface(iid, out); }
    ULONG STDMETHODCALLTYPE AddRef() override { return device->AddRef(); }
    ULONG STDMETHODCALLTYPE Release() override { return device->Release(); }

    void STDMETHODCALLTYPE Enter() override { static_cast<ID3D11Multithread *>(device)->Enter(); }
    void STDMETHODCALLTYPE Leave() override { static_cast<ID3D11Multithread *>(device)->Leave(); }
    BOOL STDMETHODCALLTYPE SetMultithreadProtected(BOOL protect) override
    {
        return static_cast<ID3D11Multithread *>(device)->SetMultithreadProtected(protect);
    }
    BOOL STDMETHODCALLTYPE GetMultithreadProtected() override
    {
        return static_cast<ID3D11Multithread *>(device)->GetMultithreadProtected();
    }

    void STDMETHODCALLTYPE VSSetConstantBuffers(UINT start, UINT count, ID3D10Buffer *const *buffers) override
    {
        ID3D11Buffer *items[D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
        if (count > ARRAYSIZE(items)) return;
        NativeFrom10(buffers, count, items, IID_ID3D11Buffer);
        Context()->VSSetConstantBuffers(start, count, buffers ? items : NULL);
        NativeReleaseAll(items, count);
    }
    void STDMETHODCALLTYPE PSSetConstantBuffers(UINT start, UINT count, ID3D10Buffer *const *buffers) override
    {
        ID3D11Buffer *items[D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
        if (count > ARRAYSIZE(items)) return;
        NativeFrom10(buffers, count, items, IID_ID3D11Buffer);
        Context()->PSSetConstantBuffers(start, count, buffers ? items : NULL);
        NativeReleaseAll(items, count);
    }
    void STDMETHODCALLTYPE GSSetConstantBuffers(UINT start, UINT count, ID3D10Buffer *const *buffers) override
    {
        ID3D11Buffer *items[D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
        if (count > ARRAYSIZE(items)) return;
        NativeFrom10(buffers, count, items, IID_ID3D11Buffer);
        Context()->GSSetConstantBuffers(start, count, buffers ? items : NULL);
        NativeReleaseAll(items, count);
    }
    void STDMETHODCALLTYPE VSSetShaderResources(UINT start, UINT count, ID3D10ShaderResourceView *const *views) override
    {
        ID3D11ShaderResourceView *items[D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
        if (count > ARRAYSIZE(items)) return;
        NativeFrom10(views, count, items, IID_ID3D11ShaderResourceView);
        Context()->VSSetShaderResources(start, count, views ? items : NULL);
        NativeReleaseAll(items, count);
    }
    void STDMETHODCALLTYPE PSSetShaderResources(UINT start, UINT count, ID3D10ShaderResourceView *const *views) override
    {
        ID3D11ShaderResourceView *items[D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
        if (count > ARRAYSIZE(items)) return;
        NativeFrom10(views, count, items, IID_ID3D11ShaderResourceView);
        Context()->PSSetShaderResources(start, count, views ? items : NULL);
        NativeReleaseAll(items, count);
    }
    void STDMETHODCALLTYPE GSSetShaderResources(UINT start, UINT count, ID3D10ShaderResourceView *const *views) override
    {
        ID3D11ShaderResourceView *items[D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
        if (count > ARRAYSIZE(items)) return;
        NativeFrom10(views, count, items, IID_ID3D11ShaderResourceView);
        Context()->GSSetShaderResources(start, count, views ? items : NULL);
        NativeReleaseAll(items, count);
    }
    void STDMETHODCALLTYPE VSSetSamplers(UINT start, UINT count, ID3D10SamplerState *const *samplers) override
    {
        ID3D11SamplerState *items[D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
        if (count > ARRAYSIZE(items)) return;
        NativeFrom10(samplers, count, items, IID_ID3D11SamplerState);
        Context()->VSSetSamplers(start, count, samplers ? items : NULL);
        NativeReleaseAll(items, count);
    }
    void STDMETHODCALLTYPE PSSetSamplers(UINT start, UINT count, ID3D10SamplerState *const *samplers) override
    {
        ID3D11SamplerState *items[D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
        if (count > ARRAYSIZE(items)) return;
        NativeFrom10(samplers, count, items, IID_ID3D11SamplerState);
        Context()->PSSetSamplers(start, count, samplers ? items : NULL);
        NativeReleaseAll(items, count);
    }
    void STDMETHODCALLTYPE GSSetSamplers(UINT start, UINT count, ID3D10SamplerState *const *samplers) override
    {
        ID3D11SamplerState *items[D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
        if (count > ARRAYSIZE(items)) return;
        NativeFrom10(samplers, count, items, IID_ID3D11SamplerState);
        Context()->GSSetSamplers(start, count, samplers ? items : NULL);
        NativeReleaseAll(items, count);
    }
    void STDMETHODCALLTYPE VSSetShader(ID3D10VertexShader *shader) override
    {
        ID3D11VertexShader *item = NativeAs<ID3D11VertexShader>(shader, IID_ID3D11VertexShader);
        Context()->VSSetShader(item, NULL, 0);
        if (item) item->Release();
    }
    void STDMETHODCALLTYPE PSSetShader(ID3D10PixelShader *shader) override
    {
        ID3D11PixelShader *item = NativeAs<ID3D11PixelShader>(shader, IID_ID3D11PixelShader);
        Context()->PSSetShader(item, NULL, 0);
        if (item) item->Release();
    }
    void STDMETHODCALLTYPE GSSetShader(ID3D10GeometryShader *shader) override
    {
        ID3D11GeometryShader *item = NativeAs<ID3D11GeometryShader>(shader, IID_ID3D11GeometryShader);
        Context()->GSSetShader(item, NULL, 0);
        if (item) item->Release();
    }
    void STDMETHODCALLTYPE DrawIndexed(UINT count, UINT start, INT base) override
    {
        Context()->DrawIndexed(count, start, base);
    }
    void STDMETHODCALLTYPE Draw(UINT count, UINT start) override { Context()->Draw(count, start); }
    void STDMETHODCALLTYPE DrawIndexedInstanced(UINT count, UINT instances, UINT start, INT base,
            UINT start_instance) override
    {
        Context()->DrawIndexedInstanced(count, instances, start, base, start_instance);
    }
    void STDMETHODCALLTYPE DrawInstanced(UINT count, UINT instances, UINT start, UINT start_instance) override
    {
        Context()->DrawInstanced(count, instances, start, start_instance);
    }
    void STDMETHODCALLTYPE DrawAuto() override { Context()->DrawAuto(); }
    void STDMETHODCALLTYPE IASetInputLayout(ID3D10InputLayout *layout) override
    {
        ID3D11InputLayout *item = NativeAs<ID3D11InputLayout>(layout, IID_ID3D11InputLayout);
        Context()->IASetInputLayout(item);
        if (item) item->Release();
    }
    void STDMETHODCALLTYPE IASetVertexBuffers(UINT start, UINT count, ID3D10Buffer *const *buffers,
            const UINT *strides, const UINT *offsets) override
    {
        ID3D11Buffer *items[D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
        if (count > ARRAYSIZE(items)) return;
        NativeFrom10(buffers, count, items, IID_ID3D11Buffer);
        Context()->IASetVertexBuffers(start, count, buffers ? items : NULL, strides, offsets);
        NativeReleaseAll(items, count);
    }
    void STDMETHODCALLTYPE IASetIndexBuffer(ID3D10Buffer *buffer, DXGI_FORMAT format, UINT offset) override
    {
        ID3D11Buffer *item = NativeAs<ID3D11Buffer>(buffer, IID_ID3D11Buffer);
        Context()->IASetIndexBuffer(item, format, offset);
        if (item) item->Release();
    }
    void STDMETHODCALLTYPE IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY topology) override
    {
        Context()->IASetPrimitiveTopology(static_cast<D3D11_PRIMITIVE_TOPOLOGY>(topology));
    }
    void STDMETHODCALLTYPE SetPredication(ID3D10Predicate *predicate, BOOL value) override
    {
        ID3D11Predicate *item = NativeAs<ID3D11Predicate>(predicate, IID_ID3D11Predicate);
        Context()->SetPredication(item, value);
        if (item) item->Release();
    }
    void STDMETHODCALLTYPE OMSetRenderTargets(UINT count, ID3D10RenderTargetView *const *views,
            ID3D10DepthStencilView *depth) override
    {
        ID3D11RenderTargetView *items[D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
        if (count > ARRAYSIZE(items)) return;
        NativeFrom10(views, count, items, IID_ID3D11RenderTargetView);
        ID3D11DepthStencilView *depth_view = NativeAs<ID3D11DepthStencilView>(depth, IID_ID3D11DepthStencilView);
        Context()->OMSetRenderTargets(count, views ? items : NULL, depth_view);
        NativeReleaseAll(items, count);
        if (depth_view) depth_view->Release();
    }
    void STDMETHODCALLTYPE OMSetBlendState(ID3D10BlendState *state, const FLOAT factor[4], UINT mask) override
    {
        ID3D11BlendState *item = NativeAs<ID3D11BlendState>(state, IID_ID3D11BlendState);
        Context()->OMSetBlendState(item, factor, mask);
        if (item) item->Release();
    }
    void STDMETHODCALLTYPE OMSetDepthStencilState(ID3D10DepthStencilState *state, UINT ref) override
    {
        ID3D11DepthStencilState *item = NativeAs<ID3D11DepthStencilState>(state, IID_ID3D11DepthStencilState);
        Context()->OMSetDepthStencilState(item, ref);
        if (item) item->Release();
    }
    void STDMETHODCALLTYPE SOSetTargets(UINT count, ID3D10Buffer *const *buffers, const UINT *offsets) override
    {
        ID3D11Buffer *items[D3D10_SO_BUFFER_SLOT_COUNT] = {};
        if (count > ARRAYSIZE(items)) return;
        NativeFrom10(buffers, count, items, IID_ID3D11Buffer);
        Context()->SOSetTargets(count, buffers ? items : NULL, offsets);
        NativeReleaseAll(items, count);
    }
    void STDMETHODCALLTYPE RSSetState(ID3D10RasterizerState *state) override
    {
        ID3D11RasterizerState *item = NativeAs<ID3D11RasterizerState>(state, IID_ID3D11RasterizerState);
        Context()->RSSetState(item);
        if (item) item->Release();
    }
    void STDMETHODCALLTYPE RSSetViewports(UINT count, const D3D10_VIEWPORT *viewports) override
    {
        D3D11_VIEWPORT items[D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        if (count > ARRAYSIZE(items) || (count && !viewports)) return;
        for (UINT i = 0; i < count; ++i)
        {
            items[i].TopLeftX = static_cast<FLOAT>(viewports[i].TopLeftX);
            items[i].TopLeftY = static_cast<FLOAT>(viewports[i].TopLeftY);
            items[i].Width = static_cast<FLOAT>(viewports[i].Width);
            items[i].Height = static_cast<FLOAT>(viewports[i].Height);
            items[i].MinDepth = viewports[i].MinDepth;
            items[i].MaxDepth = viewports[i].MaxDepth;
        }
        Context()->RSSetViewports(count, items);
    }
    void STDMETHODCALLTYPE RSSetScissorRects(UINT count, const D3D10_RECT *rects) override
    {
        Context()->RSSetScissorRects(count, rects);
    }
    void STDMETHODCALLTYPE CopySubresourceRegion(ID3D10Resource *dst, UINT dst_subresource, UINT x, UINT y, UINT z,
            ID3D10Resource *src, UINT src_subresource, const D3D10_BOX *box) override
    {
        ID3D11Resource *destination = NativeAs<ID3D11Resource>(dst, IID_ID3D11Resource);
        ID3D11Resource *source = NativeAs<ID3D11Resource>(src, IID_ID3D11Resource);
        if (destination && source)
            Context()->CopySubresourceRegion(destination, dst_subresource, x, y, z, source, src_subresource,
                    reinterpret_cast<const D3D11_BOX *>(box));
        if (destination) destination->Release();
        if (source) source->Release();
    }
    void STDMETHODCALLTYPE CopyResource(ID3D10Resource *dst, ID3D10Resource *src) override
    {
        ID3D11Resource *destination = NativeAs<ID3D11Resource>(dst, IID_ID3D11Resource);
        ID3D11Resource *source = NativeAs<ID3D11Resource>(src, IID_ID3D11Resource);
        if (destination && source) Context()->CopyResource(destination, source);
        if (destination) destination->Release();
        if (source) source->Release();
    }
    void STDMETHODCALLTYPE UpdateSubresource(ID3D10Resource *dst, UINT subresource, const D3D10_BOX *box,
            const void *data, UINT row_pitch, UINT depth_pitch) override
    {
        ID3D11Resource *destination = NativeAs<ID3D11Resource>(dst, IID_ID3D11Resource);
        if (destination)
        {
            Context()->UpdateSubresource(destination, subresource, reinterpret_cast<const D3D11_BOX *>(box), data,
                    row_pitch, depth_pitch);
            destination->Release();
        }
    }
    void STDMETHODCALLTYPE ClearRenderTargetView(ID3D10RenderTargetView *view, const FLOAT color[4]) override
    {
        ID3D11RenderTargetView *item = NativeAs<ID3D11RenderTargetView>(view, IID_ID3D11RenderTargetView);
        if (item)
        {
            Context()->ClearRenderTargetView(item, color);
            item->Release();
        }
    }
    void STDMETHODCALLTYPE ClearDepthStencilView(ID3D10DepthStencilView *view, UINT flags, FLOAT depth,
            UINT8 stencil) override
    {
        ID3D11DepthStencilView *item = NativeAs<ID3D11DepthStencilView>(view, IID_ID3D11DepthStencilView);
        if (item)
        {
            Context()->ClearDepthStencilView(item, flags, depth, stencil);
            item->Release();
        }
    }
    void STDMETHODCALLTYPE GenerateMips(ID3D10ShaderResourceView *view) override
    {
        ID3D11ShaderResourceView *item = NativeAs<ID3D11ShaderResourceView>(view, IID_ID3D11ShaderResourceView);
        if (item)
        {
            Context()->GenerateMips(item);
            item->Release();
        }
    }
    void STDMETHODCALLTYPE ResolveSubresource(ID3D10Resource *dst, UINT dst_subresource, ID3D10Resource *src,
            UINT src_subresource, DXGI_FORMAT format) override
    {
        ID3D11Resource *destination = NativeAs<ID3D11Resource>(dst, IID_ID3D11Resource);
        ID3D11Resource *source = NativeAs<ID3D11Resource>(src, IID_ID3D11Resource);
        if (destination && source)
            Context()->ResolveSubresource(destination, dst_subresource, source, src_subresource, format);
        if (destination) destination->Release();
        if (source) source->Release();
    }

    void STDMETHODCALLTYPE VSGetConstantBuffers(UINT start, UINT count, ID3D10Buffer **buffers) override
    {
        ID3D11Buffer *items[D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
        if (!buffers || count > ARRAYSIZE(items)) return;
        Context()->VSGetConstantBuffers(start, count, items);
        NativeTo10(items, count, buffers, IID_ID3D10Buffer);
    }
    void STDMETHODCALLTYPE PSGetConstantBuffers(UINT start, UINT count, ID3D10Buffer **buffers) override
    {
        ID3D11Buffer *items[D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
        if (!buffers || count > ARRAYSIZE(items)) return;
        Context()->PSGetConstantBuffers(start, count, items);
        NativeTo10(items, count, buffers, IID_ID3D10Buffer);
    }
    void STDMETHODCALLTYPE GSGetConstantBuffers(UINT start, UINT count, ID3D10Buffer **buffers) override
    {
        ID3D11Buffer *items[D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
        if (!buffers || count > ARRAYSIZE(items)) return;
        Context()->GSGetConstantBuffers(start, count, items);
        NativeTo10(items, count, buffers, IID_ID3D10Buffer);
    }
    void STDMETHODCALLTYPE VSGetShaderResources(UINT start, UINT count, ID3D10ShaderResourceView **views) override
    {
        ID3D11ShaderResourceView *items[D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
        if (!views || count > ARRAYSIZE(items)) return;
        Context()->VSGetShaderResources(start, count, items);
        NativeTo10(items, count, views, IID_ID3D10ShaderResourceView);
    }
    void STDMETHODCALLTYPE PSGetShaderResources(UINT start, UINT count, ID3D10ShaderResourceView **views) override
    {
        ID3D11ShaderResourceView *items[D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
        if (!views || count > ARRAYSIZE(items)) return;
        Context()->PSGetShaderResources(start, count, items);
        NativeTo10(items, count, views, IID_ID3D10ShaderResourceView);
    }
    void STDMETHODCALLTYPE GSGetShaderResources(UINT start, UINT count, ID3D10ShaderResourceView **views) override
    {
        ID3D11ShaderResourceView *items[D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
        if (!views || count > ARRAYSIZE(items)) return;
        Context()->GSGetShaderResources(start, count, items);
        NativeTo10(items, count, views, IID_ID3D10ShaderResourceView);
    }
    void STDMETHODCALLTYPE VSGetSamplers(UINT start, UINT count, ID3D10SamplerState **samplers) override
    {
        ID3D11SamplerState *items[D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
        if (!samplers || count > ARRAYSIZE(items)) return;
        Context()->VSGetSamplers(start, count, items);
        NativeTo10(items, count, samplers, IID_ID3D10SamplerState);
    }
    void STDMETHODCALLTYPE PSGetSamplers(UINT start, UINT count, ID3D10SamplerState **samplers) override
    {
        ID3D11SamplerState *items[D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
        if (!samplers || count > ARRAYSIZE(items)) return;
        Context()->PSGetSamplers(start, count, items);
        NativeTo10(items, count, samplers, IID_ID3D10SamplerState);
    }
    void STDMETHODCALLTYPE GSGetSamplers(UINT start, UINT count, ID3D10SamplerState **samplers) override
    {
        ID3D11SamplerState *items[D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
        if (!samplers || count > ARRAYSIZE(items)) return;
        Context()->GSGetSamplers(start, count, items);
        NativeTo10(items, count, samplers, IID_ID3D10SamplerState);
    }
    void STDMETHODCALLTYPE VSGetShader(ID3D10VertexShader **shader) override
    {
        if (!shader) return;
        ID3D11VertexShader *item = NULL;
        Context()->VSGetShader(&item, NULL, NULL);
        *shader = NativeAsReleasing<ID3D10VertexShader>(item, IID_ID3D10VertexShader);
    }
    void STDMETHODCALLTYPE PSGetShader(ID3D10PixelShader **shader) override
    {
        if (!shader) return;
        ID3D11PixelShader *item = NULL;
        Context()->PSGetShader(&item, NULL, NULL);
        *shader = NativeAsReleasing<ID3D10PixelShader>(item, IID_ID3D10PixelShader);
    }
    void STDMETHODCALLTYPE GSGetShader(ID3D10GeometryShader **shader) override
    {
        if (!shader) return;
        ID3D11GeometryShader *item = NULL;
        Context()->GSGetShader(&item, NULL, NULL);
        *shader = NativeAsReleasing<ID3D10GeometryShader>(item, IID_ID3D10GeometryShader);
    }
    void STDMETHODCALLTYPE IAGetInputLayout(ID3D10InputLayout **layout) override
    {
        if (!layout) return;
        ID3D11InputLayout *item = NULL;
        Context()->IAGetInputLayout(&item);
        *layout = NativeAsReleasing<ID3D10InputLayout>(item, IID_ID3D10InputLayout);
    }
    void STDMETHODCALLTYPE IAGetVertexBuffers(UINT start, UINT count, ID3D10Buffer **buffers, UINT *strides,
            UINT *offsets) override
    {
        ID3D11Buffer *items[D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
        if (count > ARRAYSIZE(items)) return;
        Context()->IAGetVertexBuffers(start, count, buffers ? items : NULL, strides, offsets);
        if (buffers) NativeTo10(items, count, buffers, IID_ID3D10Buffer);
    }
    void STDMETHODCALLTYPE IAGetIndexBuffer(ID3D10Buffer **buffer, DXGI_FORMAT *format, UINT *offset) override
    {
        ID3D11Buffer *item = NULL;
        Context()->IAGetIndexBuffer(buffer ? &item : NULL, format, offset);
        if (buffer) *buffer = NativeAsReleasing<ID3D10Buffer>(item, IID_ID3D10Buffer);
    }
    void STDMETHODCALLTYPE IAGetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY *topology) override
    {
        if (!topology) return;
        D3D11_PRIMITIVE_TOPOLOGY value = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        Context()->IAGetPrimitiveTopology(&value);
        *topology = static_cast<D3D10_PRIMITIVE_TOPOLOGY>(value);
    }
    void STDMETHODCALLTYPE GetPredication(ID3D10Predicate **predicate, BOOL *value) override
    {
        ID3D11Predicate *item = NULL;
        Context()->GetPredication(predicate ? &item : NULL, value);
        if (predicate) *predicate = NativeAsReleasing<ID3D10Predicate>(item, IID_ID3D10Predicate);
    }
    void STDMETHODCALLTYPE OMGetRenderTargets(UINT count, ID3D10RenderTargetView **views,
            ID3D10DepthStencilView **depth) override
    {
        ID3D11RenderTargetView *items[D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
        ID3D11DepthStencilView *depth_view = NULL;
        if (count > ARRAYSIZE(items)) return;
        Context()->OMGetRenderTargets(views ? count : 0, views ? items : NULL, depth ? &depth_view : NULL);
        if (views) NativeTo10(items, count, views, IID_ID3D10RenderTargetView);
        if (depth) *depth = NativeAsReleasing<ID3D10DepthStencilView>(depth_view, IID_ID3D10DepthStencilView);
    }
    void STDMETHODCALLTYPE OMGetBlendState(ID3D10BlendState **state, FLOAT factor[4], UINT *mask) override
    {
        ID3D11BlendState *item = NULL;
        Context()->OMGetBlendState(state ? &item : NULL, factor, mask);
        if (state) *state = NativeAsReleasing<ID3D10BlendState>(item, IID_ID3D10BlendState);
    }
    void STDMETHODCALLTYPE OMGetDepthStencilState(ID3D10DepthStencilState **state, UINT *ref) override
    {
        ID3D11DepthStencilState *item = NULL;
        Context()->OMGetDepthStencilState(state ? &item : NULL, ref);
        if (state) *state = NativeAsReleasing<ID3D10DepthStencilState>(item, IID_ID3D10DepthStencilState);
    }
    void STDMETHODCALLTYPE SOGetTargets(UINT count, ID3D10Buffer **buffers, UINT *offsets) override
    {
        ID3D11Buffer *items[D3D10_SO_BUFFER_SLOT_COUNT] = {};
        if (count > ARRAYSIZE(items)) return;
        Context()->SOGetTargets(count, items);
        for (UINT i = 0; i < count; ++i)
        {
            if (offsets) offsets[i] = items[i] ? ~0u : 0;
            if (buffers) buffers[i] = NativeAs<ID3D10Buffer>(items[i], IID_ID3D10Buffer);
        }
        NativeReleaseAll(items, count);
    }
    void STDMETHODCALLTYPE RSGetState(ID3D10RasterizerState **state) override
    {
        if (!state) return;
        ID3D11RasterizerState *item = NULL;
        Context()->RSGetState(&item);
        *state = NativeAsReleasing<ID3D10RasterizerState>(item, IID_ID3D10RasterizerState);
    }
    void STDMETHODCALLTYPE RSGetViewports(UINT *count, D3D10_VIEWPORT *viewports) override
    {
        if (!count) return;
        if (!viewports)
        {
            Context()->RSGetViewports(count, NULL);
            return;
        }
        D3D11_VIEWPORT items[D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
        UINT requested = *count < ARRAYSIZE(items) ? *count : ARRAYSIZE(items);
        UINT returned = requested;
        Context()->RSGetViewports(&returned, items);
        for (UINT i = 0; i < requested; ++i)
        {
            viewports[i].TopLeftX = static_cast<INT>(items[i].TopLeftX);
            viewports[i].TopLeftY = static_cast<INT>(items[i].TopLeftY);
            viewports[i].Width = static_cast<UINT>(items[i].Width);
            viewports[i].Height = static_cast<UINT>(items[i].Height);
            viewports[i].MinDepth = items[i].MinDepth;
            viewports[i].MaxDepth = items[i].MaxDepth;
        }
        *count = returned;
    }
    void STDMETHODCALLTYPE RSGetScissorRects(UINT *count, D3D10_RECT *rects) override
    {
        Context()->RSGetScissorRects(count, rects);
    }

    HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason() override { return Device()->GetDeviceRemovedReason(); }
    HRESULT STDMETHODCALLTYPE SetExceptionMode(UINT flags) override { return Device()->SetExceptionMode(flags); }
    UINT STDMETHODCALLTYPE GetExceptionMode() override { return Device()->GetExceptionMode(); }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT *size, void *data) override
    {
        return Device()->GetPrivateData(guid, size, data);
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT size, const void *data) override
    {
        return Device()->SetPrivateData(guid, size, data);
    }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *data) override
    {
        return Device()->SetPrivateDataInterface(guid, data);
    }
    void STDMETHODCALLTYPE ClearState() override { Context()->ClearState(); }
    void STDMETHODCALLTYPE Flush() override { Context()->Flush(); }

    HRESULT STDMETHODCALLTYPE CreateBuffer(const D3D10_BUFFER_DESC *desc, const D3D10_SUBRESOURCE_DATA *data,
            ID3D10Buffer **out) override
    {
        if (out) *out = NULL;
        if (!desc || (desc->MiscFlags & ~native_d3d10_misc_flags)) return E_INVALIDARG;
        D3D11_BUFFER_DESC converted = {};
        converted.ByteWidth = desc->ByteWidth;
        converted.Usage = static_cast<D3D11_USAGE>(desc->Usage);
        converted.BindFlags = desc->BindFlags;
        converted.CPUAccessFlags = desc->CPUAccessFlags;
        converted.MiscFlags = NativeMiscFlagsFrom10(desc->MiscFlags);
        ID3D11Buffer *buffer = NULL;
        HRESULT hr = Device()->CreateBuffer(&converted, reinterpret_cast<const D3D11_SUBRESOURCE_DATA *>(data),
                out ? &buffer : NULL);
        if (SUCCEEDED(hr) && out) *out = NativeAsReleasing<ID3D10Buffer>(buffer, IID_ID3D10Buffer);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateTexture1D(const D3D10_TEXTURE1D_DESC *desc, const D3D10_SUBRESOURCE_DATA *data,
            ID3D10Texture1D **out) override
    {
        if (out) *out = NULL;
        if (!desc || (desc->MiscFlags & ~native_d3d10_misc_flags)) return E_INVALIDARG;
        D3D11_TEXTURE1D_DESC converted = {};
        converted.Width = desc->Width;
        converted.MipLevels = desc->MipLevels;
        converted.ArraySize = desc->ArraySize;
        converted.Format = desc->Format;
        converted.Usage = static_cast<D3D11_USAGE>(desc->Usage);
        converted.BindFlags = desc->BindFlags;
        converted.CPUAccessFlags = desc->CPUAccessFlags;
        converted.MiscFlags = NativeMiscFlagsFrom10(desc->MiscFlags);
        ID3D11Texture1D *texture = NULL;
        HRESULT hr = Device()->CreateTexture1D(&converted, reinterpret_cast<const D3D11_SUBRESOURCE_DATA *>(data),
                out ? &texture : NULL);
        if (SUCCEEDED(hr) && out) *out = NativeAsReleasing<ID3D10Texture1D>(texture, IID_ID3D10Texture1D);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateTexture2D(const D3D10_TEXTURE2D_DESC *desc, const D3D10_SUBRESOURCE_DATA *data,
            ID3D10Texture2D **out) override
    {
        if (out) *out = NULL;
        if (!desc || (desc->MiscFlags & ~native_d3d10_misc_flags)) return E_INVALIDARG;
        D3D11_TEXTURE2D_DESC converted = {};
        converted.Width = desc->Width;
        converted.Height = desc->Height;
        converted.MipLevels = desc->MipLevels;
        converted.ArraySize = desc->ArraySize;
        converted.Format = desc->Format;
        converted.SampleDesc = desc->SampleDesc;
        converted.Usage = static_cast<D3D11_USAGE>(desc->Usage);
        converted.BindFlags = desc->BindFlags;
        converted.CPUAccessFlags = desc->CPUAccessFlags;
        converted.MiscFlags = NativeMiscFlagsFrom10(desc->MiscFlags);
        ID3D11Texture2D *texture = NULL;
        HRESULT hr = Device()->CreateTexture2D(&converted, reinterpret_cast<const D3D11_SUBRESOURCE_DATA *>(data),
                out ? &texture : NULL);
        if (SUCCEEDED(hr) && out) *out = NativeAsReleasing<ID3D10Texture2D>(texture, IID_ID3D10Texture2D);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateTexture3D(const D3D10_TEXTURE3D_DESC *desc, const D3D10_SUBRESOURCE_DATA *data,
            ID3D10Texture3D **out) override
    {
        if (out) *out = NULL;
        if (!desc || (desc->MiscFlags & ~native_d3d10_misc_flags)) return E_INVALIDARG;
        D3D11_TEXTURE3D_DESC converted = {};
        converted.Width = desc->Width;
        converted.Height = desc->Height;
        converted.Depth = desc->Depth;
        converted.MipLevels = desc->MipLevels;
        converted.Format = desc->Format;
        converted.Usage = static_cast<D3D11_USAGE>(desc->Usage);
        converted.BindFlags = desc->BindFlags;
        converted.CPUAccessFlags = desc->CPUAccessFlags;
        converted.MiscFlags = NativeMiscFlagsFrom10(desc->MiscFlags);
        ID3D11Texture3D *texture = NULL;
        HRESULT hr = Device()->CreateTexture3D(&converted, reinterpret_cast<const D3D11_SUBRESOURCE_DATA *>(data),
                out ? &texture : NULL);
        if (SUCCEEDED(hr) && out) *out = NativeAsReleasing<ID3D10Texture3D>(texture, IID_ID3D10Texture3D);
        return hr;
    }
    HRESULT CreateView(ID3D10Resource *resource, const D3D10_SHADER_RESOURCE_VIEW_DESC1 *desc, SIZE_T desc_size,
            ID3D10ShaderResourceView1 **out)
    {
        if (out) *out = NULL;
        ID3D11Resource *target = NativeAs<ID3D11Resource>(resource, IID_ID3D11Resource);
        if (!target) return E_INVALIDARG;
        D3D11_SHADER_RESOURCE_VIEW_DESC converted = {};
        if (desc) memcpy(&converted, desc, desc_size);
        ID3D11ShaderResourceView *view = NULL;
        HRESULT hr = Device()->CreateShaderResourceView(target, desc ? &converted : NULL, out ? &view : NULL);
        target->Release();
        if (SUCCEEDED(hr) && out) *out = NativeAsReleasing<ID3D10ShaderResourceView1>(view, IID_ID3D10ShaderResourceView1);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateShaderResourceView(ID3D10Resource *resource,
            const D3D10_SHADER_RESOURCE_VIEW_DESC *desc, ID3D10ShaderResourceView **out) override
    {
        return CreateView(resource, reinterpret_cast<const D3D10_SHADER_RESOURCE_VIEW_DESC1 *>(desc),
                sizeof(*desc), reinterpret_cast<ID3D10ShaderResourceView1 **>(out));
    }
    HRESULT STDMETHODCALLTYPE CreateShaderResourceView1(ID3D10Resource *resource,
            const D3D10_SHADER_RESOURCE_VIEW_DESC1 *desc, ID3D10ShaderResourceView1 **out) override
    {
        return CreateView(resource, desc, sizeof(*desc), out);
    }
    HRESULT STDMETHODCALLTYPE CreateRenderTargetView(ID3D10Resource *resource, const D3D10_RENDER_TARGET_VIEW_DESC *desc,
            ID3D10RenderTargetView **out) override
    {
        if (out) *out = NULL;
        ID3D11Resource *target = NativeAs<ID3D11Resource>(resource, IID_ID3D11Resource);
        if (!target) return E_INVALIDARG;
        ID3D11RenderTargetView *view = NULL;
        HRESULT hr = Device()->CreateRenderTargetView(target, reinterpret_cast<const D3D11_RENDER_TARGET_VIEW_DESC *>(desc),
                out ? &view : NULL);
        target->Release();
        if (SUCCEEDED(hr) && out) *out = NativeAsReleasing<ID3D10RenderTargetView>(view, IID_ID3D10RenderTargetView);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateDepthStencilView(ID3D10Resource *resource, const D3D10_DEPTH_STENCIL_VIEW_DESC *desc,
            ID3D10DepthStencilView **out) override
    {
        if (out) *out = NULL;
        ID3D11Resource *target = NativeAs<ID3D11Resource>(resource, IID_ID3D11Resource);
        if (!target) return E_INVALIDARG;
        D3D11_DEPTH_STENCIL_VIEW_DESC converted = {};
        if (desc)
        {
            converted.Format = desc->Format;
            converted.ViewDimension = static_cast<D3D11_DSV_DIMENSION>(desc->ViewDimension);
            memcpy(&converted.Texture2DArray, &desc->Texture2DArray, sizeof(desc->Texture2DArray));
        }
        ID3D11DepthStencilView *view = NULL;
        HRESULT hr = Device()->CreateDepthStencilView(target, desc ? &converted : NULL, out ? &view : NULL);
        target->Release();
        if (SUCCEEDED(hr) && out) *out = NativeAsReleasing<ID3D10DepthStencilView>(view, IID_ID3D10DepthStencilView);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateInputLayout(const D3D10_INPUT_ELEMENT_DESC *elements, UINT count,
            const void *code, SIZE_T length, ID3D10InputLayout **out) override
    {
        if (out) *out = NULL;
        ID3D11InputLayout *layout = NULL;
        HRESULT hr = Device()->CreateInputLayout(reinterpret_cast<const D3D11_INPUT_ELEMENT_DESC *>(elements), count,
                code, length, out ? &layout : NULL);
        if (SUCCEEDED(hr) && out) *out = NativeAsReleasing<ID3D10InputLayout>(layout, IID_ID3D10InputLayout);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const void *code, SIZE_T length, ID3D10VertexShader **out) override
    {
        if (out) *out = NULL;
        ID3D11VertexShader *shader = NULL;
        HRESULT hr = Device()->CreateVertexShader(code, length, NULL, out ? &shader : NULL);
        if (SUCCEEDED(hr) && out) *out = NativeAsReleasing<ID3D10VertexShader>(shader, IID_ID3D10VertexShader);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateGeometryShader(const void *code, SIZE_T length, ID3D10GeometryShader **out) override
    {
        if (out) *out = NULL;
        ID3D11GeometryShader *shader = NULL;
        HRESULT hr = Device()->CreateGeometryShader(code, length, NULL, out ? &shader : NULL);
        if (SUCCEEDED(hr) && out) *out = NativeAsReleasing<ID3D10GeometryShader>(shader, IID_ID3D10GeometryShader);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateGeometryShaderWithStreamOutput(const void *code, SIZE_T length,
            const D3D10_SO_DECLARATION_ENTRY *entries, UINT count, UINT stride, ID3D10GeometryShader **out) override
    {
        if (out) *out = NULL;
        if (count > D3D10_SO_SINGLE_BUFFER_COMPONENT_LIMIT * D3D10_SO_BUFFER_SLOT_COUNT || (count && !entries))
            return E_INVALIDARG;
        D3D11_SO_DECLARATION_ENTRY converted[D3D10_SO_SINGLE_BUFFER_COMPONENT_LIMIT * D3D10_SO_BUFFER_SLOT_COUNT] = {};
        for (UINT i = 0; i < count; ++i)
        {
            converted[i].Stream = 0;
            converted[i].SemanticName = entries[i].SemanticName;
            converted[i].SemanticIndex = entries[i].SemanticIndex;
            converted[i].StartComponent = entries[i].StartComponent;
            converted[i].ComponentCount = entries[i].ComponentCount;
            converted[i].OutputSlot = entries[i].OutputSlot;
        }
        ID3D11GeometryShader *shader = NULL;
        HRESULT hr = Device()->CreateGeometryShaderWithStreamOutput(code, length, count ? converted : NULL, count,
                &stride, stride ? 1 : 0, 0, NULL, out ? &shader : NULL);
        if (SUCCEEDED(hr) && out) *out = NativeAsReleasing<ID3D10GeometryShader>(shader, IID_ID3D10GeometryShader);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const void *code, SIZE_T length, ID3D10PixelShader **out) override
    {
        if (out) *out = NULL;
        ID3D11PixelShader *shader = NULL;
        HRESULT hr = Device()->CreatePixelShader(code, length, NULL, out ? &shader : NULL);
        if (SUCCEEDED(hr) && out) *out = NativeAsReleasing<ID3D10PixelShader>(shader, IID_ID3D10PixelShader);
        return hr;
    }
    HRESULT CreateBlend(const D3D11_BLEND_DESC *desc, ID3D10BlendState1 **out)
    {
        if (out) *out = NULL;
        ID3D11BlendState *state = NULL;
        HRESULT hr = Device()->CreateBlendState(desc, out ? &state : NULL);
        if (SUCCEEDED(hr) && out) *out = NativeAsReleasing<ID3D10BlendState1>(state, IID_ID3D10BlendState1);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateBlendState(const D3D10_BLEND_DESC *desc, ID3D10BlendState **out) override
    {
        if (out) *out = NULL;
        if (!desc) return E_INVALIDARG;
        D3D11_BLEND_DESC converted = {};
        converted.AlphaToCoverageEnable = desc->AlphaToCoverageEnable;
        for (UINT i = 0; i < D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
        {
            if (desc->BlendEnable[i] != desc->BlendEnable[0]
                    || desc->RenderTargetWriteMask[i] != desc->RenderTargetWriteMask[0])
                converted.IndependentBlendEnable = TRUE;
            converted.RenderTarget[i].BlendEnable = desc->BlendEnable[i];
            converted.RenderTarget[i].SrcBlend = static_cast<D3D11_BLEND>(desc->SrcBlend);
            converted.RenderTarget[i].DestBlend = static_cast<D3D11_BLEND>(desc->DestBlend);
            converted.RenderTarget[i].BlendOp = static_cast<D3D11_BLEND_OP>(desc->BlendOp);
            converted.RenderTarget[i].SrcBlendAlpha = static_cast<D3D11_BLEND>(desc->SrcBlendAlpha);
            converted.RenderTarget[i].DestBlendAlpha = static_cast<D3D11_BLEND>(desc->DestBlendAlpha);
            converted.RenderTarget[i].BlendOpAlpha = static_cast<D3D11_BLEND_OP>(desc->BlendOpAlpha);
            converted.RenderTarget[i].RenderTargetWriteMask = desc->RenderTargetWriteMask[i];
        }
        return CreateBlend(&converted, reinterpret_cast<ID3D10BlendState1 **>(out));
    }
    HRESULT STDMETHODCALLTYPE CreateBlendState1(const D3D10_BLEND_DESC1 *desc, ID3D10BlendState1 **out) override
    {
        if (out) *out = NULL;
        if (!desc) return E_INVALIDARG;
        return CreateBlend(reinterpret_cast<const D3D11_BLEND_DESC *>(desc), out);
    }
    HRESULT STDMETHODCALLTYPE CreateDepthStencilState(const D3D10_DEPTH_STENCIL_DESC *desc,
            ID3D10DepthStencilState **out) override
    {
        if (out) *out = NULL;
        ID3D11DepthStencilState *state = NULL;
        HRESULT hr = Device()->CreateDepthStencilState(reinterpret_cast<const D3D11_DEPTH_STENCIL_DESC *>(desc),
                out ? &state : NULL);
        if (SUCCEEDED(hr) && out) *out = NativeAsReleasing<ID3D10DepthStencilState>(state, IID_ID3D10DepthStencilState);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateRasterizerState(const D3D10_RASTERIZER_DESC *desc,
            ID3D10RasterizerState **out) override
    {
        if (out) *out = NULL;
        ID3D11RasterizerState *state = NULL;
        HRESULT hr = Device()->CreateRasterizerState(reinterpret_cast<const D3D11_RASTERIZER_DESC *>(desc),
                out ? &state : NULL);
        if (SUCCEEDED(hr) && out) *out = NativeAsReleasing<ID3D10RasterizerState>(state, IID_ID3D10RasterizerState);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateSamplerState(const D3D10_SAMPLER_DESC *desc, ID3D10SamplerState **out) override
    {
        if (out) *out = NULL;
        ID3D11SamplerState *state = NULL;
        HRESULT hr = Device()->CreateSamplerState(reinterpret_cast<const D3D11_SAMPLER_DESC *>(desc),
                out ? &state : NULL);
        if (SUCCEEDED(hr) && out) *out = NativeAsReleasing<ID3D10SamplerState>(state, IID_ID3D10SamplerState);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateQuery(const D3D10_QUERY_DESC *desc, ID3D10Query **out) override
    {
        if (out) *out = NULL;
        ID3D11Query *query = NULL;
        HRESULT hr = Device()->CreateQuery(reinterpret_cast<const D3D11_QUERY_DESC *>(desc), out ? &query : NULL);
        if (SUCCEEDED(hr) && out) *out = NativeAsReleasing<ID3D10Query>(query, IID_ID3D10Query);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreatePredicate(const D3D10_QUERY_DESC *desc, ID3D10Predicate **out) override
    {
        if (out) *out = NULL;
        ID3D11Predicate *predicate = NULL;
        HRESULT hr = Device()->CreatePredicate(reinterpret_cast<const D3D11_QUERY_DESC *>(desc),
                out ? &predicate : NULL);
        if (SUCCEEDED(hr) && out) *out = NativeAsReleasing<ID3D10Predicate>(predicate, IID_ID3D10Predicate);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateCounter(const D3D10_COUNTER_DESC *desc, ID3D10Counter **out) override
    {
        if (out) *out = NULL;
        ID3D11Counter *counter = NULL;
        HRESULT hr = Device()->CreateCounter(reinterpret_cast<const D3D11_COUNTER_DESC *>(desc), out ? &counter : NULL);
        if (SUCCEEDED(hr) && out) *out = NativeAsReleasing<ID3D10Counter>(counter, IID_ID3D10Counter);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CheckFormatSupport(DXGI_FORMAT format, UINT *support) override
    {
        return Device()->CheckFormatSupport(format, support);
    }
    HRESULT STDMETHODCALLTYPE CheckMultisampleQualityLevels(DXGI_FORMAT format, UINT samples, UINT *levels) override
    {
        return Device()->CheckMultisampleQualityLevels(format, samples, levels);
    }
    void STDMETHODCALLTYPE CheckCounterInfo(D3D10_COUNTER_INFO *info) override
    {
        Device()->CheckCounterInfo(reinterpret_cast<D3D11_COUNTER_INFO *>(info));
    }
    HRESULT STDMETHODCALLTYPE CheckCounter(const D3D10_COUNTER_DESC *desc, D3D10_COUNTER_TYPE *type, UINT *active,
            char *name, UINT *name_length, char *units, UINT *units_length, char *description,
            UINT *description_length) override
    {
        return Device()->CheckCounter(reinterpret_cast<const D3D11_COUNTER_DESC *>(desc),
                reinterpret_cast<D3D11_COUNTER_TYPE *>(type), active, name, name_length, units, units_length,
                description, description_length);
    }
    UINT STDMETHODCALLTYPE GetCreationFlags() override { return device->d3d10_flags; }
    HRESULT STDMETHODCALLTYPE OpenSharedResource(HANDLE handle, REFIID iid, void **out) override
    {
        if (!out) return E_INVALIDARG;
        *out = NULL;
        ID3D11Resource *resource = NULL;
        HRESULT hr = Device()->OpenSharedResource(handle, IID_ID3D11Resource, reinterpret_cast<void **>(&resource));
        if (FAILED(hr)) return hr;
        hr = resource->QueryInterface(iid, out);
        resource->Release();
        return hr;
    }
    void STDMETHODCALLTYPE SetTextFilterSize(UINT width, UINT height) override
    {
        text_filter_width = width;
        text_filter_height = height;
    }
    void STDMETHODCALLTYPE GetTextFilterSize(UINT *width, UINT *height) override
    {
        if (width) *width = text_filter_width;
        if (height) *height = text_filter_height;
    }
    D3D10_FEATURE_LEVEL1 STDMETHODCALLTYPE GetFeatureLevel() override
    {
        return static_cast<D3D10_FEATURE_LEVEL1>(device->d3d10_feature_level);
    }
};

static NativeD3D10Device *NativeD3D10DeviceOf(NativeDevice *device)
{
    if (!device->d3d10)
    {
        NativeD3D10Device *created = new NativeD3D10Device(device);
        if (!created) return NULL;
        if (InterlockedCompareExchangePointer(reinterpret_cast<void **>(&device->d3d10), created, NULL))
            delete created;
    }
    return device->d3d10;
}

static void NativeD3D10GetDevice(NativeDevice *device, ID3D10Device **out)
{
    if (!out) return;
    NativeD3D10Device *d3d10 = device->d3d10_exposed ? NativeD3D10DeviceOf(device) : NULL;
    *out = d3d10;
    if (d3d10) d3d10->AddRef();
}

void *NativeD3D10DeviceInterface(NativeDevice *device, REFIID iid)
{
    NativeD3D10Device *d3d10 = NativeD3D10DeviceOf(device);
    if (!d3d10) return NULL;
    if (IsEqualGUID(iid, IID_ID3D10Multithread)) return static_cast<ID3D10Multithread *>(d3d10);
    return static_cast<ID3D10Device1 *>(d3d10);
}

void NativeD3D10DestroyDevice(NativeD3D10Device *d3d10)
{
    delete d3d10;
}

extern "C" HRESULT WINAPI RosD3D11CreateD3D10Device(IDXGIAdapter *adapter, UINT flags, D3D_FEATURE_LEVEL level,
        ID3D10Device **out)
{
    if (!out) return E_INVALIDARG;
    *out = NULL;
    if (level != D3D_FEATURE_LEVEL_10_0 && level != D3D_FEATURE_LEVEL_10_1) return E_INVALIDARG;
    UINT device_flags = flags & (D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_DEBUG
            | D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS | D3D11_CREATE_DEVICE_BGRA_SUPPORT);
    ID3D11Device *created = NULL;
    HRESULT hr = d3d11_native_create_device(adapter, device_flags, &level, 1, &created);
    if (FAILED(hr)) return hr;
    NativeDevice *device = static_cast<NativeDevice *>(created);
    device->d3d10_flags = flags;
    if (!(flags & D3D10_CREATE_DEVICE_SINGLETHREADED))
        static_cast<ID3D11Multithread *>(device)->SetMultithreadProtected(TRUE);
    ID3DDeviceContextState *state = NULL;
    UINT state_flags = device_flags & D3D11_CREATE_DEVICE_SINGLETHREADED
            ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED : 0;
    hr = device->CreateDeviceContextState(state_flags, &level, 1, D3D11_SDK_VERSION, IID_ID3D10Device1, NULL, &state);
    if (SUCCEEDED(hr))
    {
        device->context->SwapDeviceContextState(state, NULL);
        state->Release();
        hr = created->QueryInterface(IID_ID3D10Device, reinterpret_cast<void **>(out));
    }
    created->Release();
    return hr;
}
