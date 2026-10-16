/*
 * PROJECT:     LiberNT Direct3D 12 runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Direct3D 12 device child objects, class declarations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#pragma once

template <typename Interface>
class Native12Child : public Interface, public Native12Allocation
{
public:
    LONG references = 1;
    LONG internal_references = 1;
    Native12Device *device;
    Native12PrivateData private_data;

    explicit Native12Child(Native12Device *owner) : device(owner) { device->AddRef(); }
    virtual ~Native12Child() { device->Release(); }

    void AddInternal() { InterlockedIncrement(&internal_references); }
    void ReleaseInternal() { if (!InterlockedDecrement(&internal_references)) delete this; }
    ULONG STDMETHODCALLTYPE AddRef() override
    {
        ULONG count = InterlockedIncrement(&references);
        if (count == 1) AddInternal();
        return count;
    }
    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG count = InterlockedDecrement(&references);
        if (!count) ReleaseInternal();
        return count;
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT *size, void *data) override
    {
        return private_data.Get(guid, size, data);
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT size, const void *data) override
    {
        return private_data.Set(guid, size, data, NULL);
    }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *data) override
    {
        return private_data.SetInterface(guid, const_cast<IUnknown *>(data));
    }
    HRESULT STDMETHODCALLTYPE SetName(const WCHAR *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID iid, void **out) override
    {
        return device->QueryInterface(iid, out);
    }
};

class Native12Fence final : public Native12Child<ID3D12Fence1>
{
public:
    D3DKMT_HANDLE sync = 0;
    D3D12_FENCE_FLAGS flags = D3D12_FENCE_FLAG_NONE;
    const volatile UINT64 *value = NULL;
    D3D12DDI_HFENCE driver = {};
    bool driver_created = false;

    using Native12Child::Native12Child;
    ~Native12Fence();
    HRESULT Initialize(UINT64 initial_value, D3D12_FENCE_FLAGS flags);
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override;
    UINT64 STDMETHODCALLTYPE GetCompletedValue() override;
    HRESULT STDMETHODCALLTYPE SetEventOnCompletion(UINT64 value, HANDLE event) override;
    HRESULT STDMETHODCALLTYPE Signal(UINT64 value) override;
    D3D12_FENCE_FLAGS STDMETHODCALLTYPE GetCreationFlags() override;
};

class Native12CommandQueue final : public Native12Child<ID3D12CommandQueue>, public IWineDXGISwapChainFactory
{
public:
    ULONG STDMETHODCALLTYPE AddRef() override { return Native12Child::AddRef(); }
    ULONG STDMETHODCALLTYPE Release() override { return Native12Child::Release(); }
    HRESULT STDMETHODCALLTYPE create_swapchain(IDXGIFactory *factory, HWND window,
            const DXGI_SWAP_CHAIN_DESC1 *desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreen_desc,
            IDXGIOutput *output, IDXGISwapChain1 **swapchain) override;
    D3D12_COMMAND_QUEUE_DESC desc = {};
    D3D12DDI_HCOMMANDQUEUE driver = {};
    bool driver_created = false;
    Native12CommandQueue *next_queue = NULL;
    bool listed = false;
    SRWLOCK context_lock = SRWLOCK_INIT;
    SRWLOCK submit_lock = SRWLOCK_INIT;
    HANDLE contexts[D3DDDI_MAX_BROADCAST_CONTEXT] = {};
    bool submitted[D3DDDI_MAX_BROADCAST_CONTEXT] = {};
    UINT context_count = 0;

    using Native12Child::Native12Child;
    ~Native12CommandQueue();
    HRESULT Initialize(const D3D12_COMMAND_QUEUE_DESC *input);
    void AddContext(HANDLE context);
    void RemoveContext(HANDLE context);
    bool NoteSubmission(HANDLE context);
    UINT SignalContexts(HANDLE *out);
    UINT WaitContexts(HANDLE *out);
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override;
    void STDMETHODCALLTYPE UpdateTileMappings(ID3D12Resource *resource, UINT region_count,
            const D3D12_TILED_RESOURCE_COORDINATE *region_start_coordinates,
            const D3D12_TILE_REGION_SIZE *region_sizes, ID3D12Heap *heap, UINT range_count,
            const D3D12_TILE_RANGE_FLAGS *range_flags, const UINT *heap_range_offsets,
            const UINT *range_tile_counts, D3D12_TILE_MAPPING_FLAGS flags) override;
    void STDMETHODCALLTYPE CopyTileMappings(ID3D12Resource *dst_resource,
            const D3D12_TILED_RESOURCE_COORDINATE *dst_region_start_coordinate, ID3D12Resource *src_resource,
            const D3D12_TILED_RESOURCE_COORDINATE *src_region_start_coordinate,
            const D3D12_TILE_REGION_SIZE *region_size, D3D12_TILE_MAPPING_FLAGS flags) override;
    void STDMETHODCALLTYPE ExecuteCommandLists(UINT command_list_count,
            ID3D12CommandList *const *command_lists) override;
    void STDMETHODCALLTYPE SetMarker(UINT metadata, const void *data, UINT size) override;
    void STDMETHODCALLTYPE BeginEvent(UINT metadata, const void *data, UINT size) override;
    void STDMETHODCALLTYPE EndEvent() override;
    HRESULT STDMETHODCALLTYPE Signal(ID3D12Fence *fence, UINT64 value) override;
    HRESULT STDMETHODCALLTYPE Wait(ID3D12Fence *fence, UINT64 value) override;
    HRESULT STDMETHODCALLTYPE GetTimestampFrequency(UINT64 *frequency) override;
    HRESULT STDMETHODCALLTYPE GetClockCalibration(UINT64 *gpu_timestamp, UINT64 *cpu_timestamp) override;
    D3D12_COMMAND_QUEUE_DESC *STDMETHODCALLTYPE GetDesc(D3D12_COMMAND_QUEUE_DESC *__ret) override;
};

class Native12CommandList;

class Native12CommandAllocator final : public Native12Child<ID3D12CommandAllocator>
{
public:
    D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Native12CommandList *volatile recording_list = NULL;
    D3D12DDI_HCOMMANDPOOL_0040 driver = {};
    bool driver_created = false;

    using Native12Child::Native12Child;
    ~Native12CommandAllocator();
    HRESULT Initialize(D3D12_COMMAND_LIST_TYPE list_type);
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override;
    HRESULT STDMETHODCALLTYPE Reset() override;
};

class Native12Heap final : public Native12Child<ID3D12Heap1>
{
public:
    D3D12_HEAP_DESC desc = {};
    D3D12DDI_HHEAP driver = {};
    D3D12DDI_HRESOURCE buffer = {};
    HANDLE identity = NULL;
    bool driver_created = false;
    void *mapping = NULL;

    using Native12Child::Native12Child;
    ~Native12Heap();
    HRESULT Initialize(const D3D12_HEAP_DESC *input);
    HRESULT MapBase(void **base);
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override;
    D3D12_HEAP_DESC *STDMETHODCALLTYPE GetDesc(D3D12_HEAP_DESC *__ret) override;
    HRESULT STDMETHODCALLTYPE GetProtectedResourceSession(REFIID iid, void **session) override;
};

struct Native12SharedTextureData
{
    UINT signature;
    UINT version;
    D3D11_TEXTURE2D_DESC desc;
    D3DKMT_HANDLE keyed_mutex;
};

class Native12Resource final : public Native12Child<ID3D12Resource2>
{
public:
    Native12Heap *heap = NULL;
    UINT64 heap_offset = 0;
    const void *shared_data = NULL;
    UINT shared_data_size = 0;
    bool reserved = false;
    bool nt_sharing = false;
    D3D12_RESOURCE_DESC desc = {};
    D3D12_HEAP_PROPERTIES heap_properties = {};
    D3D12_HEAP_FLAGS heap_flags = D3D12_HEAP_FLAG_NONE;
    D3D12DDI_HHEAP driver_heap = {};
    D3D12DDI_HRESOURCE driver = {};
    HANDLE identity = NULL;
    bool driver_created = false;
    void *mapping = NULL;
    LONG map_count = 0;
    UINT64 address = 0;
    bool address_valid = false;

    using Native12Child::Native12Child;
    ~Native12Resource();
    HRESULT Initialize(const D3D12_HEAP_PROPERTIES *properties, D3D12_HEAP_FLAGS flags,
            const D3D12_RESOURCE_DESC *input, D3D12_RESOURCE_STATES initial_state,
            const D3D12_CLEAR_VALUE *clear_value);
    HRESULT InitializePlaced(Native12Heap *parent, UINT64 offset, const D3D12_RESOURCE_DESC *input,
            D3D12_RESOURCE_STATES initial_state, const D3D12_CLEAR_VALUE *clear_value);
    HRESULT InitializeReserved(const D3D12_RESOURCE_DESC *input, D3D12_RESOURCE_STATES initial_state,
            const D3D12_CLEAR_VALUE *clear_value);
    HRESULT InitializeOpened(const D3D12_RESOURCE_DESC *input, D3DKMT_HANDLE km_resource, UINT count,
            D3DDDI_OPENALLOCATIONINFO *allocations, void *driver_data, UINT driver_data_size, bool *adopted);
    UINT SubresourceCount() const;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override;
    HRESULT STDMETHODCALLTYPE Map(UINT sub_resource, const D3D12_RANGE *read_range, void **data) override;
    void STDMETHODCALLTYPE Unmap(UINT sub_resource, const D3D12_RANGE *written_range) override;
    D3D12_RESOURCE_DESC *STDMETHODCALLTYPE GetDesc(D3D12_RESOURCE_DESC *__ret) override;
    D3D12_GPU_VIRTUAL_ADDRESS STDMETHODCALLTYPE GetGPUVirtualAddress() override;
    HRESULT STDMETHODCALLTYPE WriteToSubresource(UINT dst_sub_resource, const D3D12_BOX *dst_box,
            const void *src_data, UINT src_row_pitch, UINT src_slice_pitch) override;
    HRESULT STDMETHODCALLTYPE ReadFromSubresource(void *dst_data, UINT dst_row_pitch, UINT dst_slice_pitch,
            UINT src_sub_resource, const D3D12_BOX *src_box) override;
    HRESULT STDMETHODCALLTYPE GetHeapProperties(D3D12_HEAP_PROPERTIES *heap_properties,
            D3D12_HEAP_FLAGS *flags) override;
    HRESULT STDMETHODCALLTYPE GetProtectedResourceSession(REFIID iid, void **session) override;
    D3D12_RESOURCE_DESC1 *STDMETHODCALLTYPE GetDesc1(D3D12_RESOURCE_DESC1 *__ret) override;
};

class Native12DescriptorHeap final : public Native12Child<ID3D12DescriptorHeap>
{
public:
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    D3D12DDI_HDESCRIPTORHEAP driver = {};
    bool driver_created = false;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_start = {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_start = {};
    Native12DescriptorHeap *next_dsv = NULL;
    DXGI_FORMAT *dsv_formats = NULL;
    UINT increment = 0;

    using Native12Child::Native12Child;
    ~Native12DescriptorHeap();
    HRESULT Initialize(const D3D12_DESCRIPTOR_HEAP_DESC *input);
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override;
    D3D12_DESCRIPTOR_HEAP_DESC *STDMETHODCALLTYPE GetDesc(D3D12_DESCRIPTOR_HEAP_DESC *__ret) override;
    D3D12_CPU_DESCRIPTOR_HANDLE *STDMETHODCALLTYPE GetCPUDescriptorHandleForHeapStart(
            D3D12_CPU_DESCRIPTOR_HANDLE *__ret) override;
    D3D12_GPU_DESCRIPTOR_HANDLE *STDMETHODCALLTYPE GetGPUDescriptorHandleForHeapStart(
            D3D12_GPU_DESCRIPTOR_HANDLE *__ret) override;
};

class Native12RootSignature final : public Native12Child<ID3D12RootSignature>
{
public:
    D3D12DDI_HROOTSIGNATURE driver = {};
    bool driver_created = false;
    D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    Native12RootSignature *next_cached = NULL;
    BYTE *cached_bytecode = NULL;
    SIZE_T cached_size = 0;
    UINT parameter_count = 0;
    UINT64 parameter_keys[D3D12_MAX_ROOT_COST] = {};
    BYTE parameter_types[D3D12_MAX_ROOT_COST] = {};
    BYTE parameter_constants[D3D12_MAX_ROOT_COST] = {};
    BYTE constant_offsets[D3D12_MAX_ROOT_COST] = {};

    using Native12Child::Native12Child;
    ~Native12RootSignature();
    HRESULT Initialize(const void *bytecode, SIZE_T length);
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override;
};

class Native12QueryHeap final : public Native12Child<ID3D12QueryHeap>
{
public:
    D3D12_QUERY_HEAP_DESC desc = {};
    D3D12DDI_HQUERYHEAP driver = {};
    bool driver_created = false;

    using Native12Child::Native12Child;
    ~Native12QueryHeap();
    HRESULT Initialize(const D3D12_QUERY_HEAP_DESC *input);
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override;
};

class Native12CommandSignature final : public Native12Child<ID3D12CommandSignature>
{
public:
    D3D12DDI_HCOMMANDSIGNATURE driver = {};
    bool driver_created = false;
    Native12RootSignature *root_signature = NULL;

    using Native12Child::Native12Child;
    ~Native12CommandSignature();
    HRESULT Initialize(const D3D12_COMMAND_SIGNATURE_DESC *input, ID3D12RootSignature *signature);
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override;
};

HRESULT Native12CreateDefaultPipelineState(Native12Device *device, bool compute, ID3D12PipelineState **out);

class Native12PipelineState final : public Native12Child<ID3D12PipelineState>
{
public:
    enum { ShaderCount = 6 };
    D3D12DDI_HPIPELINESTATE driver = {};
    bool driver_created = false;
    Native12RootSignature *root_signature = NULL;
    D3D12DDI_HSHADER shaders[ShaderCount] = {};
    bool shader_created[ShaderCount] = {};
    D3D12DDI_HBLENDSTATE blend = {};
    D3D12DDI_HRASTERIZERSTATE rasterizer = {};
    D3D12DDI_HDEPTHSTENCILSTATE depth_stencil = {};
    D3D12DDI_HELEMENTLAYOUT layout = {};
    bool blend_created = false, rasterizer_created = false, depth_created = false, layout_created = false;
    bool compute = false;
    BOOL depth_bounds_test = FALSE;
    D3D12_PIPELINE_STATE_FLAGS flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    bool face_stencil_masks = false;
    UINT8 back_stencil_read_mask = 0, back_stencil_write_mask = 0;
    bool float_depth_bias = false;
    FLOAT depth_bias = 0.0f;
    bool line_mode_set = false;
    D3D12_LINE_RASTERIZATION_MODE line_mode = D3D12_LINE_RASTERIZATION_MODE_ALIASED;
    UINT view_instance_count = 0;
    D3D12_VIEW_INSTANCE_LOCATION view_instance_locations[D3D12_MAX_VIEW_INSTANCE_COUNT] = {};
    D3D12_VIEW_INSTANCING_FLAGS view_instancing_flags = D3D12_VIEW_INSTANCING_FLAG_NONE;

    using Native12Child::Native12Child;
    ~Native12PipelineState();
    HRESULT InitializeGraphics(const D3D12_GRAPHICS_PIPELINE_STATE_DESC *input);
    HRESULT InitializeCompute(const D3D12_COMPUTE_PIPELINE_STATE_DESC *input);
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override;
    HRESULT STDMETHODCALLTYPE GetCachedBlob(ID3DBlob **blob) override;
};

struct Native12PassResolve
{
    ID3D12Resource *source;
    ID3D12Resource *destination;
    UINT count;
    D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS *parameters;
    DXGI_FORMAT format;
    D3D12_RESOLVE_MODE mode;
};

struct Native12RootArguments
{
    void *bound;
    UINT count;
    UINT64 set;
    UINT64 keys[D3D12_MAX_ROOT_COST];
    BYTE types[D3D12_MAX_ROOT_COST];
    BYTE constant_counts[D3D12_MAX_ROOT_COST];
    BYTE constant_offsets[D3D12_MAX_ROOT_COST];
    UINT64 values[D3D12_MAX_ROOT_COST];
    UINT constants[D3D12_MAX_ROOT_COST];
};

class Native12CommandList final : public Native12Child<ID3D12GraphicsCommandList10>
{
public:
    Native12RootArguments root_arguments[2] = {};
    void BindRootSignature(UINT bind_point, Native12RootSignature *signature);
    void RecordRootValue(UINT bind_point, UINT index, UINT64 value);
    void RecordRootConstants(UINT bind_point, UINT index, UINT count, const void *data, UINT offset);
    void ApplyRootArgument(UINT bind_point, UINT index);
    Native12PassResolve pass_resolves[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT + 2] = {};
    UINT pass_resolve_count = 0;
    D3D12_RENDER_PASS_FLAGS pass_flags = D3D12_RENDER_PASS_FLAG_NONE;
    void ClearPassResolves();
    void AddPassResolve(const D3D12_RENDER_PASS_ENDING_ACCESS &access);
    D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    D3D12DDI_HCOMMANDLIST driver = {};
    D3D12DDI_HCOMMANDRECORDER_0040 recorder = {};
    bool driver_created = false, recorder_created = false;
    const NATIVE12_LIST_FUNCS *table = NULL;
    Native12CommandAllocator *allocator = NULL;
    HRESULT error = S_OK;
    bool recording = false;
    bool graphics_bound = false, compute_bound = false, pipeline_set = false;
    D3D12_PIPELINE_STATE_FLAGS pipeline_flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    bool RejectInBundle();
    ID3D12PipelineState *default_states[2] = {};
    UINT64 id = 0;

    using Native12Child::Native12Child;
    ~Native12CommandList();
    HRESULT Initialize(D3D12_COMMAND_LIST_TYPE list_type, Native12CommandAllocator *pool,
            ID3D12PipelineState *initial_state);
    void SetDefaultPipelineState();
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override;
    D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE GetType() override;
    HRESULT STDMETHODCALLTYPE Close() override;
    HRESULT STDMETHODCALLTYPE Reset(ID3D12CommandAllocator *allocator, ID3D12PipelineState *initial_state) override;
    void STDMETHODCALLTYPE ClearState(ID3D12PipelineState *pipeline_state) override;
    void STDMETHODCALLTYPE DrawInstanced(UINT vertex_count_per_instance, UINT instance_count,
            UINT start_vertex_location, UINT start_instance_location) override;
    void STDMETHODCALLTYPE DrawIndexedInstanced(UINT index_count_per_instance, UINT instance_count,
            UINT start_vertex_location, INT base_vertex_location, UINT start_instance_location) override;
    void STDMETHODCALLTYPE Dispatch(UINT x, UINT u, UINT z) override;
    void STDMETHODCALLTYPE CopyBufferRegion(ID3D12Resource *dst_buffer, UINT64 dst_offset,
            ID3D12Resource *src_buffer, UINT64 src_offset, UINT64 byte_count) override;
    void STDMETHODCALLTYPE CopyTextureRegion(const D3D12_TEXTURE_COPY_LOCATION *dst, UINT dst_x, UINT dst_y,
            UINT dst_z, const D3D12_TEXTURE_COPY_LOCATION *src, const D3D12_BOX *src_box) override;
    void STDMETHODCALLTYPE CopyResource(ID3D12Resource *dst_resource, ID3D12Resource *src_resource) override;
    void STDMETHODCALLTYPE CopyTiles(ID3D12Resource *tiled_resource,
            const D3D12_TILED_RESOURCE_COORDINATE *tile_region_start_coordinate,
            const D3D12_TILE_REGION_SIZE *tile_region_size, ID3D12Resource *buffer, UINT64 buffer_offset,
            D3D12_TILE_COPY_FLAGS flags) override;
    void STDMETHODCALLTYPE ResolveSubresource(ID3D12Resource *dst_resource, UINT dst_sub_resource,
            ID3D12Resource *src_resource, UINT src_sub_resource, DXGI_FORMAT format) override;
    void STDMETHODCALLTYPE IASetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY primitive_topology) override;
    void STDMETHODCALLTYPE RSSetViewports(UINT viewport_count, const D3D12_VIEWPORT *viewports) override;
    void STDMETHODCALLTYPE RSSetScissorRects(UINT rect_count, const D3D12_RECT *rects) override;
    void STDMETHODCALLTYPE OMSetBlendFactor(const FLOAT blend_factor[4]) override;
    void STDMETHODCALLTYPE OMSetStencilRef(UINT stencil_ref) override;
    void STDMETHODCALLTYPE SetPipelineState(ID3D12PipelineState *pipeline_state) override;
    void STDMETHODCALLTYPE ResourceBarrier(UINT barrier_count, const D3D12_RESOURCE_BARRIER *barriers) override;
    void STDMETHODCALLTYPE ExecuteBundle(ID3D12GraphicsCommandList *command_list) override;
    void STDMETHODCALLTYPE SetDescriptorHeaps(UINT heap_count, ID3D12DescriptorHeap *const *heaps) override;
    void STDMETHODCALLTYPE SetComputeRootSignature(ID3D12RootSignature *root_signature) override;
    void STDMETHODCALLTYPE SetGraphicsRootSignature(ID3D12RootSignature *root_signature) override;
    void STDMETHODCALLTYPE SetComputeRootDescriptorTable(UINT root_parameter_index,
            D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor) override;
    void STDMETHODCALLTYPE SetGraphicsRootDescriptorTable(UINT root_parameter_index,
            D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor) override;
    void STDMETHODCALLTYPE SetComputeRoot32BitConstant(UINT root_parameter_index, UINT data,
            UINT dst_offset) override;
    void STDMETHODCALLTYPE SetGraphicsRoot32BitConstant(UINT root_parameter_index, UINT data,
            UINT dst_offset) override;
    void STDMETHODCALLTYPE SetComputeRoot32BitConstants(UINT root_parameter_index, UINT constant_count,
            const void *data, UINT dst_offset) override;
    void STDMETHODCALLTYPE SetGraphicsRoot32BitConstants(UINT root_parameter_index, UINT constant_count,
            const void *data, UINT dst_offset) override;
    void STDMETHODCALLTYPE SetComputeRootConstantBufferView(UINT root_parameter_index,
            D3D12_GPU_VIRTUAL_ADDRESS address) override;
    void STDMETHODCALLTYPE SetGraphicsRootConstantBufferView(UINT root_parameter_index,
            D3D12_GPU_VIRTUAL_ADDRESS address) override;
    void STDMETHODCALLTYPE SetComputeRootShaderResourceView(UINT root_parameter_index,
            D3D12_GPU_VIRTUAL_ADDRESS address) override;
    void STDMETHODCALLTYPE SetGraphicsRootShaderResourceView(UINT root_parameter_index,
            D3D12_GPU_VIRTUAL_ADDRESS address) override;
    void STDMETHODCALLTYPE SetComputeRootUnorderedAccessView(UINT root_parameter_index,
            D3D12_GPU_VIRTUAL_ADDRESS address) override;
    void STDMETHODCALLTYPE SetGraphicsRootUnorderedAccessView(UINT root_parameter_index,
            D3D12_GPU_VIRTUAL_ADDRESS address) override;
    void STDMETHODCALLTYPE IASetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW *view) override;
    void STDMETHODCALLTYPE IASetVertexBuffers(UINT start_slot, UINT view_count,
            const D3D12_VERTEX_BUFFER_VIEW *views) override;
    void STDMETHODCALLTYPE SOSetTargets(UINT start_slot, UINT view_count,
            const D3D12_STREAM_OUTPUT_BUFFER_VIEW *views) override;
    void STDMETHODCALLTYPE OMSetRenderTargets(UINT render_target_descriptor_count,
            const D3D12_CPU_DESCRIPTOR_HANDLE *render_target_descriptors, BOOL single_descriptor_handle,
            const D3D12_CPU_DESCRIPTOR_HANDLE *depth_stencil_descriptor) override;
    void STDMETHODCALLTYPE ClearDepthStencilView(D3D12_CPU_DESCRIPTOR_HANDLE dsv, D3D12_CLEAR_FLAGS flags,
            FLOAT depth, UINT8 stencil, UINT rect_count, const D3D12_RECT *rects) override;
    void STDMETHODCALLTYPE ClearRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const FLOAT color[4],
            UINT rect_count, const D3D12_RECT *rects) override;
    void STDMETHODCALLTYPE ClearUnorderedAccessViewUint(D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle,
            D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, ID3D12Resource *resource, const UINT values[4],
            UINT rect_count, const D3D12_RECT *rects) override;
    void STDMETHODCALLTYPE ClearUnorderedAccessViewFloat(D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle,
            D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, ID3D12Resource *resource, const float values[4],
            UINT rect_count, const D3D12_RECT *rects) override;
    void STDMETHODCALLTYPE DiscardResource(ID3D12Resource *resource, const D3D12_DISCARD_REGION *region) override;
    void STDMETHODCALLTYPE BeginQuery(ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT index) override;
    void STDMETHODCALLTYPE EndQuery(ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT index) override;
    void STDMETHODCALLTYPE ResolveQueryData(ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT start_index,
            UINT query_count, ID3D12Resource *dst_buffer, UINT64 aligned_dst_buffer_offset) override;
    void STDMETHODCALLTYPE SetPredication(ID3D12Resource *buffer, UINT64 aligned_buffer_offset,
            D3D12_PREDICATION_OP operation) override;
    void STDMETHODCALLTYPE SetMarker(UINT metadata, const void *data, UINT size) override;
    void STDMETHODCALLTYPE BeginEvent(UINT metadata, const void *data, UINT size) override;
    void STDMETHODCALLTYPE EndEvent() override;
    void STDMETHODCALLTYPE ExecuteIndirect(ID3D12CommandSignature *command_signature, UINT max_command_count,
            ID3D12Resource *arg_buffer, UINT64 arg_buffer_offset, ID3D12Resource *count_buffer,
            UINT64 count_buffer_offset) override;
    void STDMETHODCALLTYPE AtomicCopyBufferUINT(ID3D12Resource *dst_buffer, UINT64 dst_offset,
            ID3D12Resource *src_buffer, UINT64 src_offset, UINT dependent_resource_count,
            ID3D12Resource *const *dependent_resources,
            const D3D12_SUBRESOURCE_RANGE_UINT64 *dependent_sub_resource_ranges) override;
    void STDMETHODCALLTYPE AtomicCopyBufferUINT64(ID3D12Resource *dst_buffer, UINT64 dst_offset,
            ID3D12Resource *src_buffer, UINT64 src_offset, UINT dependent_resource_count,
            ID3D12Resource *const *dependent_resources,
            const D3D12_SUBRESOURCE_RANGE_UINT64 *dependent_sub_resource_ranges) override;
    void STDMETHODCALLTYPE OMSetDepthBounds(FLOAT min, FLOAT max) override;
    void STDMETHODCALLTYPE SetSamplePositions(UINT sample_count, UINT pixel_count,
            D3D12_SAMPLE_POSITION *sample_positions) override;
    void STDMETHODCALLTYPE ResolveSubresourceRegion(ID3D12Resource *dst_resource, UINT dst_sub_resource_idx,
            UINT dst_x, UINT dst_y, ID3D12Resource *src_resource, UINT src_sub_resource_idx, D3D12_RECT *src_rect,
            DXGI_FORMAT format, D3D12_RESOLVE_MODE mode) override;
    void STDMETHODCALLTYPE SetViewInstanceMask(UINT mask) override;
    void STDMETHODCALLTYPE WriteBufferImmediate(UINT count, const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER *parameters,
            const D3D12_WRITEBUFFERIMMEDIATE_MODE *modes) override;
    void STDMETHODCALLTYPE SetProtectedResourceSession(ID3D12ProtectedResourceSession *session) override;
    void STDMETHODCALLTYPE BeginRenderPass(UINT render_targets_count,
            const D3D12_RENDER_PASS_RENDER_TARGET_DESC *render_targets,
            const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC *depth_stencil, D3D12_RENDER_PASS_FLAGS flags) override;
    void STDMETHODCALLTYPE EndRenderPass() override;
    void STDMETHODCALLTYPE InitializeMetaCommand(ID3D12MetaCommand *meta_command, const void *data,
            SIZE_T data_size) override;
    void STDMETHODCALLTYPE ExecuteMetaCommand(ID3D12MetaCommand *meta_command, const void *data,
            SIZE_T data_size) override;
    void STDMETHODCALLTYPE BuildRaytracingAccelerationStructure(
            const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC *desc, UINT postbuild_info_descs_count,
            const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *postbuild_info_descs) override;
    void STDMETHODCALLTYPE EmitRaytracingAccelerationStructurePostbuildInfo(
            const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *desc,
            UINT src_acceleration_structures_count, const D3D12_GPU_VIRTUAL_ADDRESS *src_data) override;
    void STDMETHODCALLTYPE CopyRaytracingAccelerationStructure(D3D12_GPU_VIRTUAL_ADDRESS dst_data,
            D3D12_GPU_VIRTUAL_ADDRESS src_data, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE mode) override;
    void STDMETHODCALLTYPE SetPipelineState1(ID3D12StateObject *state_object) override;
    void STDMETHODCALLTYPE DispatchRays(const D3D12_DISPATCH_RAYS_DESC *desc) override;
    void STDMETHODCALLTYPE RSSetShadingRate(D3D12_SHADING_RATE base_shading_rate,
            const D3D12_SHADING_RATE_COMBINER *combiners) override;
    void STDMETHODCALLTYPE RSSetShadingRateImage(ID3D12Resource *shading_rate_image) override;
    void STDMETHODCALLTYPE DispatchMesh(UINT x, UINT y, UINT z) override;
    void STDMETHODCALLTYPE Barrier(UINT32 group_count, const D3D12_BARRIER_GROUP *groups) override;
    void STDMETHODCALLTYPE OMSetFrontAndBackStencilRef(UINT front, UINT back) override;
    void STDMETHODCALLTYPE RSSetDepthBias(FLOAT bias, FLOAT clamp, FLOAT slope_scaled_bias) override;
    void STDMETHODCALLTYPE IASetIndexBufferStripCutValue(D3D12_INDEX_BUFFER_STRIP_CUT_VALUE value) override;
    void STDMETHODCALLTYPE SetProgram(const D3D12_SET_PROGRAM_DESC *desc) override;
    void STDMETHODCALLTYPE DispatchGraph(const D3D12_DISPATCH_GRAPH_DESC *desc) override;
};

struct Native12FormatInfo
{
    UINT block_width;
    UINT block_height;
    UINT block_bytes;
    UINT planes;
};

bool Native12GetFormatInfo(DXGI_FORMAT format, Native12FormatInfo *info);
HRESULT Native12ValidateResourceDesc(const D3D12_RESOURCE_DESC *desc);
HRESULT Native12ValidateResource(D3D12_HEAP_TYPE heap_type, const D3D12_RESOURCE_DESC *desc,
        D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE *clear_value);
void Native12FormatSupport(DXGI_FORMAT format, UINT ddi_support, D3D12_FORMAT_SUPPORT1 *support1,
        D3D12_FORMAT_SUPPORT2 *support2);
D3D12DDI_COMMAND_QUEUE_FLAGS Native12QueueFlags(D3D12_COMMAND_LIST_TYPE type);

static inline Native12Resource *Native12UnwrapResource(ID3D12Resource *resource)
{
    return static_cast<Native12Resource *>(resource);
}
