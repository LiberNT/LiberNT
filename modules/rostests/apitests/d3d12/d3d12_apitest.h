/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared helpers for the Direct3D 12 tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#pragma once

#include <apitest.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#define D3D12_TEST_SIZE 32u
#define D3D12_TEST_WAIT 5000u
#define D3D12_TEST_BUDGET 30000u

struct d3d12_test_context
{
    ID3D12Device *device;
    ID3D12CommandQueue *queue;
    ID3D12CommandAllocator *allocator;
    ID3D12GraphicsCommandList *list;
    ID3D12Fence *fence;
    UINT64 fence_value;
    HANDLE event;
    ID3D12RootSignature *root_signature;
    ID3D12DescriptorHeap *rtv_heap;
    UINT rtv_increment;
};

extern const DWORD vs_fullscreen_code[];
extern const SIZE_T vs_fullscreen_code_size;
extern const DWORD ps_green_code[];
extern const SIZE_T ps_green_code_size;
extern const DWORD ps_mrt_code[];
extern const SIZE_T ps_mrt_code_size;
extern const DWORD ps_sample_code[];
extern const SIZE_T ps_sample_code_size;
extern const DWORD ps_parity_code[];
extern const SIZE_T ps_parity_code_size;
extern const DWORD vs_viewid_code[];
extern const SIZE_T vs_viewid_code_size;
extern const DWORD ps_viewid_code[];
extern const SIZE_T ps_viewid_code_size;
extern const DWORD vs_size_dep_code[];
extern const SIZE_T vs_size_dep_code_size;
extern const DWORD vs_size_flat_code[];
extern const SIZE_T vs_size_flat_code_size;
extern const DWORD ps_flat_code[];
extern const SIZE_T ps_flat_code_size;

ID3D12Device *d3d12_test_create_device(void);
BOOL d3d12_test_bounded(const char *name, void (*function)(void *), void *argument, DWORD timeout);
BOOL d3d12_test_init(struct d3d12_test_context *context);
void d3d12_test_destroy(struct d3d12_test_context *context);
BOOL d3d12_test_execute(struct d3d12_test_context *context, ID3D12GraphicsCommandList *list, DWORD timeout);
BOOL d3d12_test_wait_queue(ID3D12Device *device, ID3D12CommandQueue *queue, DWORD timeout);
ID3D12Resource *d3d12_test_create_target(ID3D12Device *device, DXGI_FORMAT format);
D3D12_CPU_DESCRIPTOR_HANDLE d3d12_test_rtv(struct d3d12_test_context *context, UINT index);
ID3D12PipelineState *d3d12_test_create_pipeline(struct d3d12_test_context *context, const DWORD *ps_code,
        SIZE_T ps_size, UINT target_count, const D3D12_BLEND_DESC *blend);
void d3d12_test_transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
        D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);
BOOL d3d12_test_read_texel(struct d3d12_test_context *context, ID3D12Resource *resource,
        D3D12_RESOURCE_STATES state, UINT x, UINT y, DWORD *value);
void d3d12_test_bind_target(struct d3d12_test_context *context, ID3D12GraphicsCommandList *list, UINT count,
        const D3D12_CPU_DESCRIPTOR_HANDLE *rtvs);
