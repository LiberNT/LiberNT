/*
 * PROJECT:     LiberNT Direct3D 12 runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Direct3D 12 device on a user-mode display driver, class declaration
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#pragma once

class Native12DescriptorHeap;

class Native12Device final : public ID3D12Device5, public ID3D12DeviceRemovedExtendedData1, public Native12Allocation
{
public:
    LONG references = 1;
    Native12Device *next = NULL;
    bool listed = false;
    SRWLOCK queue_lock = SRWLOCK_INIT;
    SRWLOCK dsv_lock = SRWLOCK_INIT;
    SRWLOCK root_signature_lock = SRWLOCK_INIT;
    class Native12RootSignature *root_signatures = NULL;
    Native12DescriptorHeap *dsv_heaps = NULL;
    void SetDsvFormat(SIZE_T descriptor, DXGI_FORMAT format);
    DXGI_FORMAT GetDsvFormat(SIZE_T descriptor);
    class Native12CommandQueue *queues = NULL;
    IDXGIAdapter *adapter = NULL;
    LUID luid = {};
    D3DKMT_HANDLE km_adapter = 0;
    D3DKMT_HANDLE km_device = 0;
    HANDLE runtime_device = NULL;
    HMODULE umd = NULL;
    HMODULE runtime = NULL;
    HRESULT (WINAPI *destroy_callbacks)(HANDLE) = NULL;
    HRESULT (WINAPI *release_resource)(HANDLE, HANDLE) = NULL;
    HRESULT (WINAPI *register_resource)(HANDLE, HANDLE, D3DKMT_CREATEALLOCATIONFLAGS, const void *, UINT) = NULL;
    HRESULT (WINAPI *get_resource_handles)(HANDLE, HANDLE, D3DKMT_HANDLE *, D3DKMT_HANDLE *) = NULL;
    HRESULT (WINAPI *adopt_resource)(HANDLE, HANDLE, D3DKMT_HANDLE, D3DKMT_HANDLE, D3DKMT_HANDLE) = NULL;
    HRESULT (WINAPI *enqueue_set_event)(HANDLE, HANDLE) = NULL;
    ROS_UMD_ADAPTER_CALLBACKS adapter_callbacks = {};
    D3D12DDI_ADAPTERFUNCS_0109 adapter_functions = {};
    D3D12DDI_HADAPTER driver_adapter = {};
    bool adapter_open = false;
    D3D12DDI_HDEVICE driver_device = {};
    void *driver_device_storage = NULL;
    bool driver_created = false;
    D3DDDI_DEVICECALLBACKS kernel_callbacks = {};
    D3DDDI_DEVICECALLBACKS driver_callbacks = {};
    D3DDDI_DEVICECALLBACKS traced_callbacks = {};
    NATIVE12_CORE_CALLBACKS core_callbacks = {};
    NATIVE12_DEVICE_FUNCS functions = {};
    NATIVE12_QUEUE_FUNCS queue_functions = {};
    NATIVE12_LIST_FUNCS list_functions = {};
    NATIVE12_LIST_FUNCS *list_tables = NULL;
    UINT list_table_count = 0;
    HRESULT FillOptionalTables(SIZE_T list_table_size);
    const NATIVE12_LIST_FUNCS *FindListTable(HANDLE table);
    D3DDDICB_CREATEPAGINGQUEUE paging_queue = {};
    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
    UINT ddi_build = NATIVE12_DDI_BUILD;
    D3D12DDI_D3D12_OPTIONS_DATA_0089 options = {};
    D3D12DDI_SHADER_CAPS_0084 shader_caps = {};
    D3D12DDI_ARCHITECTURE_INFO_DATA architecture = {};
    D3D12DDI_MEMORY_ARCHITECTURE_CAPS_0041 memory = {};
    HRESULT ValidateHeapProperties(const D3D12_HEAP_PROPERTIES *properties);
    D3D12DDI_GPUVA_CAPS_0004 gpuva = {};
    UINT highest_shader_model = D3D_SHADER_MODEL_5_1;
    HRESULT removed_reason = S_OK;
    D3D12_DRED_ENABLEMENT dred_auto_breadcrumbs = D3D12_DRED_ENABLEMENT_SYSTEM_CONTROLLED;
    D3D12_DRED_ENABLEMENT dred_page_fault = D3D12_DRED_ENABLEMENT_SYSTEM_CONTROLLED;
    LONG64 resource_sequence = 0;
    LONG64 list_sequence = 0;
    Native12PrivateData private_data;

    ~Native12Device();
    HRESULT Initialize(IDXGIAdapter *, D3D_FEATURE_LEVEL);
    HRESULT QueryCaps(D3D12DDICAPS_TYPE, void *, void *, UINT);
    HRESULT WaitForPaging(UINT64 fence_value);
    static Native12Device *FromRuntime(HANDLE);
    static Native12Device *FromKernelHandle(HANDLE);

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, UINT *, void *) override;
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, UINT, const void *) override;
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID, const IUnknown *) override;
    HRESULT STDMETHODCALLTYPE SetName(const WCHAR *) override;
    UINT STDMETHODCALLTYPE GetNodeCount() override;
    HRESULT STDMETHODCALLTYPE CreateCommandQueue(const D3D12_COMMAND_QUEUE_DESC *, REFIID, void **) override;
    HRESULT STDMETHODCALLTYPE CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE, REFIID, void **) override;
    HRESULT STDMETHODCALLTYPE CreateGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC *, REFIID,
            void **) override;
    HRESULT STDMETHODCALLTYPE CreateComputePipelineState(const D3D12_COMPUTE_PIPELINE_STATE_DESC *, REFIID,
            void **) override;
    HRESULT STDMETHODCALLTYPE CreateCommandList(UINT, D3D12_COMMAND_LIST_TYPE, ID3D12CommandAllocator *,
            ID3D12PipelineState *, REFIID, void **) override;
    HRESULT STDMETHODCALLTYPE CheckFeatureSupport(D3D12_FEATURE, void *, UINT) override;
    HRESULT STDMETHODCALLTYPE CreateDescriptorHeap(const D3D12_DESCRIPTOR_HEAP_DESC *, REFIID, void **) override;
    UINT STDMETHODCALLTYPE GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE) override;
    HRESULT STDMETHODCALLTYPE CreateRootSignature(UINT, const void *, SIZE_T, REFIID, void **) override;
    void STDMETHODCALLTYPE CreateConstantBufferView(const D3D12_CONSTANT_BUFFER_VIEW_DESC *,
            D3D12_CPU_DESCRIPTOR_HANDLE) override;
    void STDMETHODCALLTYPE CreateShaderResourceView(ID3D12Resource *, const D3D12_SHADER_RESOURCE_VIEW_DESC *,
            D3D12_CPU_DESCRIPTOR_HANDLE) override;
    void STDMETHODCALLTYPE CreateUnorderedAccessView(ID3D12Resource *, ID3D12Resource *,
            const D3D12_UNORDERED_ACCESS_VIEW_DESC *, D3D12_CPU_DESCRIPTOR_HANDLE) override;
    void STDMETHODCALLTYPE CreateRenderTargetView(ID3D12Resource *, const D3D12_RENDER_TARGET_VIEW_DESC *,
            D3D12_CPU_DESCRIPTOR_HANDLE) override;
    void STDMETHODCALLTYPE CreateDepthStencilView(ID3D12Resource *, const D3D12_DEPTH_STENCIL_VIEW_DESC *,
            D3D12_CPU_DESCRIPTOR_HANDLE) override;
    void STDMETHODCALLTYPE CreateSampler(const D3D12_SAMPLER_DESC *, D3D12_CPU_DESCRIPTOR_HANDLE) override;
    void STDMETHODCALLTYPE CopyDescriptors(UINT, const D3D12_CPU_DESCRIPTOR_HANDLE *, const UINT *, UINT,
            const D3D12_CPU_DESCRIPTOR_HANDLE *, const UINT *, D3D12_DESCRIPTOR_HEAP_TYPE) override;
    void STDMETHODCALLTYPE CopyDescriptorsSimple(UINT, const D3D12_CPU_DESCRIPTOR_HANDLE,
            const D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_DESCRIPTOR_HEAP_TYPE) override;
    D3D12_RESOURCE_ALLOCATION_INFO *STDMETHODCALLTYPE GetResourceAllocationInfo(D3D12_RESOURCE_ALLOCATION_INFO *,
            UINT, UINT, const D3D12_RESOURCE_DESC *) override;
    D3D12_HEAP_PROPERTIES *STDMETHODCALLTYPE GetCustomHeapProperties(D3D12_HEAP_PROPERTIES *, UINT,
            D3D12_HEAP_TYPE) override;
    HRESULT STDMETHODCALLTYPE CreateCommittedResource(const D3D12_HEAP_PROPERTIES *, D3D12_HEAP_FLAGS,
            const D3D12_RESOURCE_DESC *, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE *, REFIID,
            void **) override;
    HRESULT STDMETHODCALLTYPE CreateHeap(const D3D12_HEAP_DESC *, REFIID, void **) override;
    HRESULT STDMETHODCALLTYPE CreatePlacedResource(ID3D12Heap *, UINT64, const D3D12_RESOURCE_DESC *,
            D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE *, REFIID, void **) override;
    HRESULT STDMETHODCALLTYPE CreateReservedResource(const D3D12_RESOURCE_DESC *, D3D12_RESOURCE_STATES,
            const D3D12_CLEAR_VALUE *, REFIID, void **) override;
    HRESULT STDMETHODCALLTYPE CreateSharedHandle(ID3D12DeviceChild *, const SECURITY_ATTRIBUTES *, DWORD,
            const WCHAR *, HANDLE *) override;
    HRESULT STDMETHODCALLTYPE OpenSharedHandle(HANDLE, REFIID, void **) override;
    HRESULT STDMETHODCALLTYPE OpenSharedHandleByName(const WCHAR *, DWORD, HANDLE *) override;
    HRESULT STDMETHODCALLTYPE MakeResident(UINT, ID3D12Pageable *const *) override;
    HRESULT STDMETHODCALLTYPE Evict(UINT, ID3D12Pageable *const *) override;
    HRESULT STDMETHODCALLTYPE CreateFence(UINT64, D3D12_FENCE_FLAGS, REFIID, void **) override;
    HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason() override;
    void STDMETHODCALLTYPE GetCopyableFootprints(const D3D12_RESOURCE_DESC *, UINT, UINT, UINT64,
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT *, UINT *, UINT64 *, UINT64 *) override;
    HRESULT STDMETHODCALLTYPE CreateQueryHeap(const D3D12_QUERY_HEAP_DESC *, REFIID, void **) override;
    HRESULT STDMETHODCALLTYPE SetStablePowerState(BOOL) override;
    HRESULT STDMETHODCALLTYPE CreateCommandSignature(const D3D12_COMMAND_SIGNATURE_DESC *, ID3D12RootSignature *,
            REFIID, void **) override;
    void STDMETHODCALLTYPE GetResourceTiling(ID3D12Resource *, UINT *, D3D12_PACKED_MIP_INFO *, D3D12_TILE_SHAPE *,
            UINT *, UINT, D3D12_SUBRESOURCE_TILING *) override;
    LUID *STDMETHODCALLTYPE GetAdapterLuid(LUID *) override;
    HRESULT STDMETHODCALLTYPE CreatePipelineLibrary(const void *, SIZE_T, REFIID, void **) override;
    HRESULT STDMETHODCALLTYPE SetEventOnMultipleFenceCompletion(ID3D12Fence *const *, const UINT64 *, UINT,
            D3D12_MULTIPLE_FENCE_WAIT_FLAGS, HANDLE) override;
    HRESULT STDMETHODCALLTYPE SetResidencyPriority(UINT, ID3D12Pageable *const *,
            const D3D12_RESIDENCY_PRIORITY *) override;
    HRESULT STDMETHODCALLTYPE CreatePipelineState(const D3D12_PIPELINE_STATE_STREAM_DESC *, REFIID, void **) override;
    HRESULT STDMETHODCALLTYPE OpenExistingHeapFromAddress(const void *, REFIID, void **) override;
    HRESULT STDMETHODCALLTYPE OpenExistingHeapFromFileMapping(HANDLE, REFIID, void **) override;
    HRESULT STDMETHODCALLTYPE EnqueueMakeResident(D3D12_RESIDENCY_FLAGS, UINT, ID3D12Pageable *const *,
            ID3D12Fence *, UINT64) override;
    HRESULT STDMETHODCALLTYPE CreateCommandList1(UINT, D3D12_COMMAND_LIST_TYPE, D3D12_COMMAND_LIST_FLAGS, REFIID,
            void **) override;
    HRESULT STDMETHODCALLTYPE CreateProtectedResourceSession(const D3D12_PROTECTED_RESOURCE_SESSION_DESC *, REFIID,
            void **) override;
    HRESULT STDMETHODCALLTYPE CreateCommittedResource1(const D3D12_HEAP_PROPERTIES *, D3D12_HEAP_FLAGS,
            const D3D12_RESOURCE_DESC *, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE *,
            ID3D12ProtectedResourceSession *, REFIID, void **) override;
    HRESULT STDMETHODCALLTYPE CreateHeap1(const D3D12_HEAP_DESC *, ID3D12ProtectedResourceSession *, REFIID,
            void **) override;
    HRESULT STDMETHODCALLTYPE CreateReservedResource1(const D3D12_RESOURCE_DESC *, D3D12_RESOURCE_STATES,
            const D3D12_CLEAR_VALUE *, ID3D12ProtectedResourceSession *, REFIID, void **) override;
    D3D12_RESOURCE_ALLOCATION_INFO *STDMETHODCALLTYPE GetResourceAllocationInfo1(D3D12_RESOURCE_ALLOCATION_INFO *,
            UINT, UINT, const D3D12_RESOURCE_DESC *, D3D12_RESOURCE_ALLOCATION_INFO1 *) override;
    HRESULT STDMETHODCALLTYPE CreateLifetimeTracker(ID3D12LifetimeOwner *, REFIID, void **) override;
    void STDMETHODCALLTYPE RemoveDevice() override;
    HRESULT STDMETHODCALLTYPE EnumerateMetaCommands(UINT *, D3D12_META_COMMAND_DESC *) override;
    HRESULT STDMETHODCALLTYPE EnumerateMetaCommandParameters(REFGUID, D3D12_META_COMMAND_PARAMETER_STAGE, UINT *,
            UINT *, D3D12_META_COMMAND_PARAMETER_DESC *) override;
    HRESULT STDMETHODCALLTYPE CreateMetaCommand(REFGUID, UINT, const void *, SIZE_T, REFIID, void **) override;
    HRESULT STDMETHODCALLTYPE CreateStateObject(const D3D12_STATE_OBJECT_DESC *, REFIID, void **) override;
    void STDMETHODCALLTYPE GetRaytracingAccelerationStructurePrebuildInfo(
            const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS *,
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO *) override;
    D3D12_DRIVER_MATCHING_IDENTIFIER_STATUS STDMETHODCALLTYPE CheckDriverMatchingIdentifier(D3D12_SERIALIZED_DATA_TYPE,
            const D3D12_SERIALIZED_DATA_DRIVER_MATCHING_IDENTIFIER *) override;
    HRESULT STDMETHODCALLTYPE GetAutoBreadcrumbsOutput(D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT *) override;
    HRESULT STDMETHODCALLTYPE GetPageFaultAllocationOutput(D3D12_DRED_PAGE_FAULT_OUTPUT *) override;
    HRESULT STDMETHODCALLTYPE GetAutoBreadcrumbsOutput1(D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 *) override;
    HRESULT STDMETHODCALLTYPE GetPageFaultAllocationOutput1(D3D12_DRED_PAGE_FAULT_OUTPUT1 *) override;
};

Native12Device *Native12QueueDevice(HANDLE queue);
void Native12QueueAddContext(HANDLE queue, HANDLE context);
void Native12QueueRemoveContext(HANDLE queue, HANDLE context);
void Native12NoteSubmission(Native12Device *device, HANDLE context);
void Native12SetCommandListErrorObject(HANDLE list, HRESULT hr);
void Native12SetCommandListTableObject(HANDLE list, HANDLE table);
