/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Video decode device, capabilities, decoder objects and decode command lists
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "d3d12_apitest.h"
#include <d3d12video.h>

static const GUID profile_h264 = { 0x1b81be68, 0xa0c7, 0x11d3, { 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5 } };

static void TestDecodeList(ID3D12Device *device)
{
    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    ID3D12VideoDecodeCommandList *list = NULL;
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12CommandQueue *queue = NULL;
    ID3D12Fence *fence = NULL;
    HANDLE event;
    HRESULT hr;

    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE;
    hr = device->CreateCommandQueue(&queue_desc, __uuidof(ID3D12CommandQueue), reinterpret_cast<void **>(&queue));
    if (FAILED(hr))
    {
        skip("no video decode queue: %#lx\n", hr);
        return;
    }
    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE, __uuidof(ID3D12CommandAllocator),
            reinterpret_cast<void **>(&allocator));
    ok(hr == S_OK && allocator, "video decode allocator: %#lx\n", hr);
    if (!queue || !allocator)
    {
        if (queue) queue->Release();
        if (allocator) allocator->Release();
        return;
    }
    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE, allocator, NULL,
            __uuidof(ID3D12VideoDecodeCommandList), reinterpret_cast<void **>(&list));
    ok(hr == S_OK && list, "video decode list: %#lx\n", hr);
    if (list)
    {
        ok(list->GetType() == D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE, "list type %u\n", list->GetType());
        hr = list->Reset(allocator);
        ok(hr == E_FAIL, "Reset while recording: %#lx\n", hr);
        hr = list->Close();
        ok(hr == S_OK, "Close: %#lx\n", hr);
        ID3D12CommandList *lists[] = { list };
        queue->ExecuteCommandLists(1, lists);
        hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), reinterpret_cast<void **>(&fence));
        ok(hr == S_OK && fence, "fence: %#lx\n", hr);
        if (fence)
        {
            hr = queue->Signal(fence, 1);
            ok(hr == S_OK, "Signal on the video queue: %#lx\n", hr);
            event = CreateEventW(NULL, FALSE, FALSE, NULL);
            fence->SetEventOnCompletion(1, event);
            ok(WaitForSingleObject(event, D3D12_TEST_WAIT) == WAIT_OBJECT_0, "video queue did not complete\n");
            CloseHandle(event);
            fence->Release();
        }
        hr = allocator->Reset();
        ok(hr == S_OK, "allocator Reset: %#lx\n", hr);
        hr = list->Reset(allocator);
        ok(hr == S_OK, "list Reset: %#lx\n", hr);
        hr = list->Close();
        ok(hr == S_OK, "second Close: %#lx\n", hr);
        list->Release();
    }
    ok(device->GetDeviceRemovedReason() == S_OK, "device removed reason %#lx\n", device->GetDeviceRemovedReason());
    allocator->Release();
    queue->Release();
}

static void RunVideo(void *)
{
    D3D12_FEATURE_DATA_VIDEO_DECODE_PROFILE_COUNT count = {};
    D3D12_FEATURE_DATA_VIDEO_DECODE_PROFILES profiles = {};
    D3D12_FEATURE_DATA_VIDEO_DECODE_SUPPORT support = {};
    D3D12_FEATURE_DATA_VIDEO_DECODE_FORMAT_COUNT format_count = {};
    D3D12_FEATURE_DATA_VIDEO_DECODE_FORMATS formats = {};
    D3D12_FEATURE_DATA_VIDEO_DECODER_HEAP_SIZE heap_size = {};
    D3D12_VIDEO_DECODER_HEAP_DESC heap_desc = {};
    D3D12_VIDEO_DECODER_DESC decoder_desc = {}, got = {};
    ID3D12VideoDecoderHeap *heap = NULL;
    ID3D12VideoDecoder *decoder = NULL;
    ID3D12VideoDevice *video = NULL;
    ID3D12Device *device;
    GUID list[64];
    DXGI_FORMAT format_list[32];
    BOOL h264 = FALSE, nv12 = FALSE;
    HRESULT hr;

    if (!(device = d3d12_test_create_device())) return;
    hr = device->QueryInterface(__uuidof(ID3D12VideoDevice), reinterpret_cast<void **>(&video));
    trace("ID3D12VideoDevice: %#lx\n", hr);
    if (!video)
    {
        ok(hr == E_NOINTERFACE, "video device QI: %#lx\n", hr);
        skip("no video support\n");
        device->Release();
        return;
    }

    hr = video->CheckFeatureSupport(D3D12_FEATURE_VIDEO_DECODE_PROFILE_COUNT, &count, sizeof(count));
    ok(hr == S_OK && count.ProfileCount, "profile count: %#lx, %u\n", hr, count.ProfileCount);
    hr = video->CheckFeatureSupport(D3D12_FEATURE_VIDEO_DECODE_PROFILE_COUNT, &count, sizeof(count) - 1);
    ok(hr == E_INVALIDARG, "profile count with a short size: %#lx\n", hr);
    if (count.ProfileCount && count.ProfileCount <= ARRAYSIZE(list))
    {
        profiles.ProfileCount = count.ProfileCount + 1;
        profiles.pProfiles = list;
        hr = video->CheckFeatureSupport(D3D12_FEATURE_VIDEO_DECODE_PROFILES, &profiles, sizeof(profiles));
        ok(hr == E_INVALIDARG, "profiles with a wrong count: %#lx\n", hr);
        profiles.ProfileCount = count.ProfileCount;
        hr = video->CheckFeatureSupport(D3D12_FEATURE_VIDEO_DECODE_PROFILES, &profiles, sizeof(profiles));
        ok(hr == S_OK, "profiles: %#lx\n", hr);
        for (UINT i = 0; SUCCEEDED(hr) && i < count.ProfileCount; ++i)
            if (IsEqualGUID(list[i], profile_h264)) h264 = TRUE;
    }
    trace("%u decode profiles, H.264 %s\n", count.ProfileCount, h264 ? "listed" : "not listed");
    if (!h264)
    {
        skip("no H.264 decode profile\n");
        video->Release();
        device->Release();
        return;
    }

    support.Configuration.DecodeProfile = profile_h264;
    support.Width = 1920;
    support.Height = 1080;
    support.DecodeFormat = DXGI_FORMAT_NV12;
    support.FrameRate.Numerator = 30;
    support.FrameRate.Denominator = 1;
    hr = video->CheckFeatureSupport(D3D12_FEATURE_VIDEO_DECODE_SUPPORT, &support, sizeof(support));
    ok(hr == S_OK && (support.SupportFlags & D3D12_VIDEO_DECODE_SUPPORT_FLAG_SUPPORTED),
            "H.264 1080p NV12 support: %#lx, flags %#x, configuration %#x, tier %u\n", hr, support.SupportFlags,
            support.ConfigurationFlags, support.DecodeTier);

    format_count.Configuration.DecodeProfile = profile_h264;
    hr = video->CheckFeatureSupport(D3D12_FEATURE_VIDEO_DECODE_FORMAT_COUNT, &format_count, sizeof(format_count));
    ok(hr == S_OK && format_count.FormatCount, "H.264 format count: %#lx, %u\n", hr, format_count.FormatCount);
    if (format_count.FormatCount && format_count.FormatCount <= ARRAYSIZE(format_list))
    {
        formats.Configuration.DecodeProfile = profile_h264;
        formats.FormatCount = format_count.FormatCount;
        formats.pOutputFormats = format_list;
        hr = video->CheckFeatureSupport(D3D12_FEATURE_VIDEO_DECODE_FORMATS, &formats, sizeof(formats));
        ok(hr == S_OK, "H.264 formats: %#lx\n", hr);
        for (UINT i = 0; SUCCEEDED(hr) && i < formats.FormatCount; ++i)
            if (format_list[i] == DXGI_FORMAT_NV12) nv12 = TRUE;
        ok(nv12, "NV12 is not an H.264 decode format\n");
    }

    heap_desc.Configuration.DecodeProfile = profile_h264;
    heap_desc.DecodeWidth = 1920;
    heap_desc.DecodeHeight = 1088;
    heap_desc.Format = DXGI_FORMAT_NV12;
    heap_desc.FrameRate.Numerator = 30;
    heap_desc.FrameRate.Denominator = 1;
    heap_desc.MaxDecodePictureBufferCount = 17;
    heap_size.VideoDecoderHeapDesc = heap_desc;
    hr = video->CheckFeatureSupport(D3D12_FEATURE_VIDEO_DECODER_HEAP_SIZE, &heap_size, sizeof(heap_size));
    ok(hr == S_OK, "decoder heap size: %#lx\n", hr);
    trace("decoder heap L0 %I64u L1 %I64u\n", heap_size.MemoryPoolL0Size, heap_size.MemoryPoolL1Size);

    decoder_desc.Configuration.DecodeProfile = profile_h264;
    hr = video->CreateVideoDecoder(&decoder_desc, __uuidof(ID3D12VideoDecoder), reinterpret_cast<void **>(&decoder));
    ok(hr == S_OK && decoder, "H.264 decoder: %#lx\n", hr);
    if (decoder)
    {
        decoder->GetDesc(&got);
        ok(IsEqualGUID(got.Configuration.DecodeProfile, profile_h264), "decoder profile %s\n",
                wine_dbgstr_guid(&got.Configuration.DecodeProfile));
        decoder->Release();
    }
    hr = video->CreateVideoDecoderHeap(&heap_desc, __uuidof(ID3D12VideoDecoderHeap), reinterpret_cast<void **>(&heap));
    ok(hr == S_OK && heap, "H.264 decoder heap: %#lx\n", hr);
    if (heap) heap->Release();

    TestDecodeList(device);
    video->Release();
    device->Release();
}

START_TEST(video)
{
    d3d12_test_bounded("video", RunVideo, NULL, D3D12_TEST_BUDGET);
}
