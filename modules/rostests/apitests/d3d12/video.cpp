/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Video device capabilities, decoders, processors and video command lists
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

static ID3D12Resource *CreateVideoTextureSize(ID3D12Device *device, DXGI_FORMAT format, UINT width, UINT height,
        D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state)
{
    D3D12_HEAP_PROPERTIES heap = {};
    D3D12_RESOURCE_DESC desc = {};
    ID3D12Resource *resource = NULL;
    HRESULT hr;

    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Flags = flags;
    hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, NULL, __uuidof(ID3D12Resource),
            reinterpret_cast<void **>(&resource));
    ok(hr == S_OK, "CreateCommittedResource(format %u): %#lx\n", format, hr);
    return resource;
}

static ID3D12Resource *CreateVideoTexture(ID3D12Device *device, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags,
        D3D12_RESOURCE_STATES state)
{
    return CreateVideoTextureSize(device, format, D3D12_TEST_SIZE, D3D12_TEST_SIZE, flags, state);
}

static BOOL UploadNv12(struct d3d12_test_context *context, ID3D12Resource *texture, BYTE luma, BYTE chroma)
{
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layouts[2];
    D3D12_RESOURCE_DESC desc = texture->GetDesc(), buffer_desc = {};
    D3D12_HEAP_PROPERTIES heap = {};
    D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
    ID3D12Resource *upload = NULL;
    UINT rows[2];
    UINT64 row_sizes[2], total = 0;
    BYTE *data;
    BOOL done = FALSE;
    HRESULT hr;

    context->device->GetCopyableFootprints(&desc, 0, 2, 0, layouts, rows, row_sizes, &total);
    ok(total != 0 && total != ~0ull, "NV12 copyable size %I64u\n", total);
    if (!total || total == ~0ull) return FALSE;
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer_desc.Width = total;
    buffer_desc.Height = 1;
    buffer_desc.DepthOrArraySize = 1;
    buffer_desc.MipLevels = 1;
    buffer_desc.SampleDesc.Count = 1;
    buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = context->device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, NULL, __uuidof(ID3D12Resource), reinterpret_cast<void **>(&upload));
    ok(hr == S_OK, "CreateCommittedResource(upload): %#lx\n", hr);
    if (FAILED(hr)) return FALSE;
    hr = upload->Map(0, NULL, reinterpret_cast<void **>(&data));
    ok(hr == S_OK, "Map: %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        memset(data + layouts[0].Offset, luma, static_cast<size_t>(layouts[0].Footprint.RowPitch) * rows[0]);
        memset(data + layouts[1].Offset, chroma, static_cast<size_t>(layouts[1].Footprint.RowPitch) * rows[1]);
        upload->Unmap(0, NULL);
        context->allocator->Reset();
        context->list->Reset(context->allocator, NULL);
        for (UINT plane = 0; plane < 2; ++plane)
        {
            dst.pResource = texture;
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.SubresourceIndex = plane;
            src.pResource = upload;
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint = layouts[plane];
            context->list->CopyTextureRegion(&dst, 0, 0, 0, &src, NULL);
        }
        d3d12_test_transition(context->list, texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        done = d3d12_test_execute(context, context->list, D3D12_TEST_WAIT);
        ok(done, "NV12 upload did not complete\n");
    }
    upload->Release();
    return done;
}

static void VideoBarrier(ID3D12VideoProcessCommandList *list, ID3D12Resource *resource, D3D12_RESOURCE_STATES before,
        D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier = {};

    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    list->ResourceBarrier(1, &barrier);
}

static void TestProcessFrame(ID3D12VideoProcessor *processor)
{
    D3D12_VIDEO_PROCESS_OUTPUT_STREAM_ARGUMENTS output_args = {};
    D3D12_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS input_args = {};
    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    struct d3d12_test_context context;
    ID3D12VideoProcessCommandList *list = NULL;
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12CommandQueue *queue = NULL;
    ID3D12Resource *input = NULL, *output = NULL;
    DWORD texel = 0;
    HRESULT hr;

    if (!d3d12_test_init(&context)) return;
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS;
    hr = context.device->CreateCommandQueue(&queue_desc, __uuidof(ID3D12CommandQueue), reinterpret_cast<void **>(&queue));
    ok(hr == S_OK, "video process queue: %#lx\n", hr);
    hr = context.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS, __uuidof(ID3D12CommandAllocator),
            reinterpret_cast<void **>(&allocator));
    ok(hr == S_OK, "video process allocator: %#lx\n", hr);
    if (allocator)
    {
        hr = context.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS, allocator, NULL,
                __uuidof(ID3D12VideoProcessCommandList), reinterpret_cast<void **>(&list));
        ok(hr == S_OK, "video process list: %#lx\n", hr);
    }
    input = CreateVideoTexture(context.device, DXGI_FORMAT_NV12, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    output = CreateVideoTexture(context.device, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
            D3D12_RESOURCE_STATE_COMMON);
    if (queue && list && input && output && UploadNv12(&context, input, 180, 128))
    {
        ID3D12VideoProcessCommandList1 *list1 = NULL;

        ok(list->GetType() == D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS, "list type %u\n", list->GetType());
        hr = list->QueryInterface(__uuidof(ID3D12VideoProcessCommandList1), reinterpret_cast<void **>(&list1));
        ok(hr == S_OK, "ID3D12VideoProcessCommandList1: %#lx\n", hr);
        if (list1) list1->Release();
        VideoBarrier(list, input, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VIDEO_PROCESS_READ);
        VideoBarrier(list, output, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VIDEO_PROCESS_WRITE);
        output_args.OutputStream[0].pTexture2D = output;
        SetRect(&output_args.TargetRectangle, 0, 0, D3D12_TEST_SIZE, D3D12_TEST_SIZE);
        input_args.InputStream[0].pTexture2D = input;
        SetRect(&input_args.Transform.SourceRectangle, 0, 0, D3D12_TEST_SIZE, D3D12_TEST_SIZE);
        SetRect(&input_args.Transform.DestinationRectangle, 0, 0, D3D12_TEST_SIZE, D3D12_TEST_SIZE);
        list->ProcessFrames(processor, &output_args, 1, &input_args);
        VideoBarrier(list, input, D3D12_RESOURCE_STATE_VIDEO_PROCESS_READ, D3D12_RESOURCE_STATE_COMMON);
        VideoBarrier(list, output, D3D12_RESOURCE_STATE_VIDEO_PROCESS_WRITE, D3D12_RESOURCE_STATE_COMMON);
        hr = list->Close();
        ok(hr == S_OK, "Close: %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            ID3D12CommandList *lists[] = { list };
            queue->ExecuteCommandLists(1, lists);
            ok(d3d12_test_wait_queue(context.device, queue, D3D12_TEST_WAIT), "video process queue did not complete\n");
            ok(d3d12_test_read_texel(&context, output, D3D12_RESOURCE_STATE_COMMON, 5, 7, &texel), "readback failed\n");
            ok(abs(static_cast<int>(texel & 0xff) - 191) <= 8 && abs(static_cast<int>((texel >> 8) & 0xff) - 191) <= 8
                    && abs(static_cast<int>((texel >> 16) & 0xff) - 191) <= 8 && (texel >> 24) == 0xff,
                    "processed texel %#lx, expected about 0xffbfbfbf\n", texel);
        }
    }
    ok(context.device->GetDeviceRemovedReason() == S_OK, "device removed reason %#lx\n",
            context.device->GetDeviceRemovedReason());
    if (output) output->Release();
    if (input) input->Release();
    if (list) list->Release();
    if (allocator) allocator->Release();
    if (queue) queue->Release();
    d3d12_test_destroy(&context);
}

static void TestProcess(ID3D12VideoDevice *video)
{
    D3D12_FEATURE_DATA_VIDEO_PROCESS_SUPPORT support = {};
    D3D12_FEATURE_DATA_VIDEO_PROCESS_MAX_INPUT_STREAMS streams = {};
    D3D12_FEATURE_DATA_VIDEO_PROCESS_REFERENCE_INFO reference = {};
    D3D12_FEATURE_DATA_VIDEO_PROCESSOR_SIZE1 size = {};
    D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC input = {}, got_input = {};
    D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC output = {}, got_output;
    ID3D12VideoProcessor *processor = NULL;
    HRESULT hr;

    support.InputSample.Width = D3D12_TEST_SIZE;
    support.InputSample.Height = D3D12_TEST_SIZE;
    support.InputSample.Format.Format = DXGI_FORMAT_NV12;
    support.InputSample.Format.ColorSpace = DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
    support.InputFrameRate.Numerator = 30;
    support.InputFrameRate.Denominator = 1;
    support.OutputFormat.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    support.OutputFormat.ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    support.OutputFrameRate = support.InputFrameRate;
    hr = video->CheckFeatureSupport(D3D12_FEATURE_VIDEO_PROCESS_SUPPORT, &support, sizeof(support) - 4);
    ok(hr == E_INVALIDARG, "process support with a short size: %#lx\n", hr);
    support.NodeIndex = 1;
    hr = video->CheckFeatureSupport(D3D12_FEATURE_VIDEO_PROCESS_SUPPORT, &support, sizeof(support));
    ok(hr == E_INVALIDARG, "process support on node 1: %#lx\n", hr);
    support.NodeIndex = 0;
    support.InputSample.Width = 0;
    hr = video->CheckFeatureSupport(D3D12_FEATURE_VIDEO_PROCESS_SUPPORT, &support, sizeof(support));
    ok(hr == E_INVALIDARG, "process support with a zero width: %#lx\n", hr);
    support.InputSample.Width = D3D12_TEST_SIZE;
    hr = video->CheckFeatureSupport(D3D12_FEATURE_VIDEO_PROCESS_SUPPORT, &support, sizeof(support));
    ok(hr == S_OK, "process support: %#lx\n", hr);
    trace("NV12 -> RGBA support %#x, output %ux%u..%ux%u, scale flags %#x, features %#x, deinterlace %#x, filters %#x\n",
            support.SupportFlags, support.ScaleSupport.OutputSizeRange.MinWidth,
            support.ScaleSupport.OutputSizeRange.MinHeight, support.ScaleSupport.OutputSizeRange.MaxWidth,
            support.ScaleSupport.OutputSizeRange.MaxHeight, support.ScaleSupport.Flags, support.FeatureSupport,
            support.DeinterlaceSupport, support.FilterSupport);

    hr = video->CheckFeatureSupport(D3D12_FEATURE_VIDEO_PROCESS_MAX_INPUT_STREAMS, &streams, sizeof(streams));
    ok(hr == S_OK, "max input streams: %#lx\n", hr);
    trace("max input streams %u\n", streams.MaxInputStreams);
    reference.InputFrameRate = support.InputFrameRate;
    reference.OutputFrameRate = support.InputFrameRate;
    hr = video->CheckFeatureSupport(D3D12_FEATURE_VIDEO_PROCESS_REFERENCE_INFO, &reference, sizeof(reference));
    ok(hr == S_OK, "reference info: %#lx\n", hr);
    trace("reference frames past %u future %u\n", reference.PastFrames, reference.FutureFrames);
    if (!(support.SupportFlags & D3D12_VIDEO_PROCESS_SUPPORT_FLAG_SUPPORTED))
    {
        skip("NV12 to RGBA processing is not supported\n");
        return;
    }
    ok(streams.MaxInputStreams >= 1, "max input streams %u\n", streams.MaxInputStreams);

    input.Format = DXGI_FORMAT_NV12;
    input.ColorSpace = DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
    input.SourceAspectRatio.Numerator = input.SourceAspectRatio.Denominator = 1;
    input.DestinationAspectRatio = input.SourceAspectRatio;
    input.FrameRate = support.InputFrameRate;
    input.SourceSizeRange.MaxWidth = input.SourceSizeRange.MinWidth = D3D12_TEST_SIZE;
    input.SourceSizeRange.MaxHeight = input.SourceSizeRange.MinHeight = D3D12_TEST_SIZE;
    input.DestinationSizeRange = input.SourceSizeRange;
    output.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    output.ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    output.FrameRate = support.InputFrameRate;

    size.pOutputStreamDesc = &output;
    size.NumInputStreamDescs = 1;
    size.pInputStreamDescs = &input;
    hr = video->CheckFeatureSupport(D3D12_FEATURE_VIDEO_PROCESSOR_SIZE1, &size, sizeof(size));
    ok(hr == S_OK, "processor size: %#lx\n", hr);
    trace("processor L0 %I64u L1 %I64u\n", size.MemoryPoolL0Size, size.MemoryPoolL1Size);

    hr = video->CreateVideoProcessor(0, &output, 0, &input, __uuidof(ID3D12VideoProcessor),
            reinterpret_cast<void **>(&processor));
    ok(hr == E_INVALIDARG && !processor, "processor without inputs: %#lx\n", hr);
    hr = video->CreateVideoProcessor(3, &output, 1, &input, __uuidof(ID3D12VideoProcessor),
            reinterpret_cast<void **>(&processor));
    ok(hr == E_INVALIDARG && !processor, "processor on two nodes: %#lx\n", hr);
    input.SourceSizeRange.MinWidth = 0;
    hr = video->CreateVideoProcessor(0, &output, 1, &input, __uuidof(ID3D12VideoProcessor),
            reinterpret_cast<void **>(&processor));
    ok(hr == E_INVALIDARG && !processor, "processor with a zero minimum width: %#lx\n", hr);
    input.SourceSizeRange.MinWidth = D3D12_TEST_SIZE;
    hr = video->CreateVideoProcessor(0, &output, 1, &input, __uuidof(ID3D12VideoProcessor),
            reinterpret_cast<void **>(&processor));
    ok(hr == S_OK && processor, "processor: %#lx\n", hr);
    if (!processor) return;
    ok(processor->GetNodeMask() == 0, "node mask %#x\n", processor->GetNodeMask());
    ok(processor->GetNumInputStreamDescs() == 1, "input count %u\n", processor->GetNumInputStreamDescs());
    hr = processor->GetInputStreamDescs(1, &got_input);
    ok(hr == S_OK && got_input.Format == DXGI_FORMAT_NV12 && got_input.SourceSizeRange.MaxWidth == D3D12_TEST_SIZE,
            "input descs: %#lx, format %u\n", hr, got_input.Format);
    got_output = processor->GetOutputStreamDesc();
    ok(got_output.Format == DXGI_FORMAT_R8G8B8A8_UNORM && got_output.FrameRate.Numerator == 30,
            "output desc format %u\n", got_output.Format);
    TestProcessFrame(processor);
    processor->Release();
}

static void EncodeBarrier(ID3D12VideoEncodeCommandList *list, ID3D12Resource *resource, D3D12_RESOURCE_STATES before,
        D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier = {};

    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    list->ResourceBarrier(1, &barrier);
}

static BOOL ReadMotionVectors(struct d3d12_test_context *context, ID3D12Resource *texture, UINT width, UINT height,
        UINT *nonzero)
{
    D3D12_HEAP_PROPERTIES heap = {};
    D3D12_RESOURCE_DESC desc = {};
    D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
    ID3D12Resource *readback = NULL;
    const BYTE *data;
    BOOL done;
    HRESULT hr;

    heap.Type = D3D12_HEAP_TYPE_READBACK;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = static_cast<UINT64>(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT) * height;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = context->device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
            NULL, __uuidof(ID3D12Resource), reinterpret_cast<void **>(&readback));
    ok(hr == S_OK, "CreateCommittedResource(readback): %#lx\n", hr);
    if (FAILED(hr)) return FALSE;
    context->allocator->Reset();
    context->list->Reset(context->allocator, NULL);
    d3d12_test_transition(context->list, texture, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
    dst.pResource = readback;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R16G16_SINT;
    dst.PlacedFootprint.Footprint.Width = width;
    dst.PlacedFootprint.Footprint.Height = height;
    dst.PlacedFootprint.Footprint.Depth = 1;
    dst.PlacedFootprint.Footprint.RowPitch = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
    src.pResource = texture;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    context->list->CopyTextureRegion(&dst, 0, 0, 0, &src, NULL);
    d3d12_test_transition(context->list, texture, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
    done = d3d12_test_execute(context, context->list, D3D12_TEST_WAIT);
    ok(done, "motion vector readback did not complete\n");
    *nonzero = 0;
    if (done && SUCCEEDED(readback->Map(0, NULL, reinterpret_cast<void **>(const_cast<BYTE **>(&data)))))
    {
        for (UINT y = 0; y < height; ++y)
            for (UINT x = 0; x < width; ++x)
                if (*reinterpret_cast<const DWORD *>(data + y * D3D12_TEXTURE_DATA_PITCH_ALIGNMENT + x * 4)) ++*nonzero;
        readback->Unmap(0, NULL);
    }
    else done = FALSE;
    readback->Release();
    return done;
}

static BOOL FillTexture32(struct d3d12_test_context *context, ID3D12Resource *texture, UINT width, UINT height,
        DWORD value)
{
    D3D12_HEAP_PROPERTIES heap = {};
    D3D12_RESOURCE_DESC desc = {};
    D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
    ID3D12Resource *upload = NULL;
    DWORD *data;
    BOOL done = FALSE;
    HRESULT hr;

    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = static_cast<UINT64>(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT) * height;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = context->device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
            NULL, __uuidof(ID3D12Resource), reinterpret_cast<void **>(&upload));
    ok(hr == S_OK, "CreateCommittedResource(upload): %#lx\n", hr);
    if (FAILED(hr)) return FALSE;
    if (SUCCEEDED(hr = upload->Map(0, NULL, reinterpret_cast<void **>(&data))))
    {
        for (UINT i = 0; i < desc.Width / 4; ++i) data[i] = value;
        upload->Unmap(0, NULL);
        context->allocator->Reset();
        context->list->Reset(context->allocator, NULL);
        dst.pResource = texture;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.pResource = upload;
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Footprint.Format = texture->GetDesc().Format;
        src.PlacedFootprint.Footprint.Width = width;
        src.PlacedFootprint.Footprint.Height = height;
        src.PlacedFootprint.Footprint.Depth = 1;
        src.PlacedFootprint.Footprint.RowPitch = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
        context->list->CopyTextureRegion(&dst, 0, 0, 0, &src, NULL);
        d3d12_test_transition(context->list, texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        done = d3d12_test_execute(context, context->list, D3D12_TEST_WAIT);
        ok(done, "texture fill did not complete\n");
    }
    upload->Release();
    return done;
}

static void TestMotionFrame(ID3D12VideoMotionEstimator *estimator, ID3D12VideoMotionVectorHeap *heap, UINT size,
        UINT block)
{
    D3D12_VIDEO_MOTION_ESTIMATOR_OUTPUT output = {};
    D3D12_VIDEO_MOTION_ESTIMATOR_INPUT input = {};
    D3D12_RESOLVE_VIDEO_MOTION_VECTOR_HEAP_OUTPUT resolve_output = {};
    D3D12_RESOLVE_VIDEO_MOTION_VECTOR_HEAP_INPUT resolve_input = {};
    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    struct d3d12_test_context context;
    ID3D12VideoEncodeCommandList *list = NULL;
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12CommandQueue *queue = NULL;
    ID3D12Resource *current = NULL, *reference = NULL, *vectors = NULL;
    UINT nonzero = 0, blocks = size / block;
    HRESULT hr;

    if (!d3d12_test_init(&context)) return;
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE;
    hr = context.device->CreateCommandQueue(&queue_desc, __uuidof(ID3D12CommandQueue), reinterpret_cast<void **>(&queue));
    ok(hr == S_OK, "video encode queue: %#lx\n", hr);
    hr = context.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE, __uuidof(ID3D12CommandAllocator),
            reinterpret_cast<void **>(&allocator));
    ok(hr == S_OK, "video encode allocator: %#lx\n", hr);
    if (allocator)
    {
        hr = context.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE, allocator, NULL,
                __uuidof(ID3D12VideoEncodeCommandList), reinterpret_cast<void **>(&list));
        ok(hr == S_OK, "video encode list: %#lx\n", hr);
    }
    current = CreateVideoTextureSize(context.device, DXGI_FORMAT_NV12, size, size, D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_COPY_DEST);
    reference = CreateVideoTextureSize(context.device, DXGI_FORMAT_NV12, size, size, D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_COPY_DEST);
    vectors = CreateVideoTextureSize(context.device, DXGI_FORMAT_R16G16_SINT, blocks, blocks,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
    if (queue && list && current && reference && vectors && UploadNv12(&context, current, 120, 128)
            && UploadNv12(&context, reference, 120, 128) && FillTexture32(&context, vectors, blocks, blocks, 0x7fff7fff))
    {
        ok(list->GetType() == D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE, "list type %u\n", list->GetType());
        EncodeBarrier(list, current, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ);
        EncodeBarrier(list, reference, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ);
        output.pMotionVectorHeap = heap;
        input.pInputTexture2D = current;
        input.pReferenceTexture2D = reference;
        list->EstimateMotion(estimator, &output, &input);
        EncodeBarrier(list, vectors, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE);
        resolve_output.pMotionVectorTexture2D = vectors;
        resolve_input.pMotionVectorHeap = heap;
        resolve_input.PixelWidth = size;
        resolve_input.PixelHeight = size;
        list->ResolveMotionVectorHeap(&resolve_output, &resolve_input);
        EncodeBarrier(list, vectors, D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE, D3D12_RESOURCE_STATE_COMMON);
        EncodeBarrier(list, current, D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ, D3D12_RESOURCE_STATE_COMMON);
        EncodeBarrier(list, reference, D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ, D3D12_RESOURCE_STATE_COMMON);
        hr = list->Close();
        ok(hr == S_OK, "Close: %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            ID3D12CommandList *lists[] = { list };
            queue->ExecuteCommandLists(1, lists);
            ok(d3d12_test_wait_queue(context.device, queue, D3D12_TEST_WAIT), "video encode queue did not complete\n");
            ok(ReadMotionVectors(&context, vectors, blocks, blocks, &nonzero), "motion vector readback failed\n");
            ok(!nonzero, "%u of %u motion vectors are not zero for identical frames\n", nonzero, blocks * blocks);
        }
    }
    ok(context.device->GetDeviceRemovedReason() == S_OK, "device removed reason %#lx\n",
            context.device->GetDeviceRemovedReason());
    if (vectors) vectors->Release();
    if (reference) reference->Release();
    if (current) current->Release();
    if (list) list->Release();
    if (allocator) allocator->Release();
    if (queue) queue->Release();
    d3d12_test_destroy(&context);
}

static void TestMotion(ID3D12VideoDevice *video)
{
    D3D12_FEATURE_DATA_VIDEO_MOTION_ESTIMATOR caps = {};
    D3D12_FEATURE_DATA_VIDEO_MOTION_ESTIMATOR_SIZE size = {};
    D3D12_VIDEO_MOTION_ESTIMATOR_DESC desc = {}, got;
    D3D12_VIDEO_MOTION_VECTOR_HEAP_DESC heap_desc = {};
    ID3D12VideoMotionEstimator *estimator = NULL;
    ID3D12VideoMotionVectorHeap *heap = NULL;
    ID3D12VideoDevice1 *video1 = NULL;
    void *session = &caps;
    UINT dimension;
    HRESULT hr;

    hr = video->QueryInterface(__uuidof(ID3D12VideoDevice1), reinterpret_cast<void **>(&video1));
    ok(hr == S_OK && video1, "ID3D12VideoDevice1: %#lx\n", hr);
    if (!video1) return;
    caps.InputFormat = DXGI_FORMAT_NV12;
    hr = video->CheckFeatureSupport(D3D12_FEATURE_VIDEO_MOTION_ESTIMATOR, &caps, sizeof(caps) - 4);
    ok(hr == E_INVALIDARG, "motion estimator caps with a short size: %#lx\n", hr);
    caps.NodeIndex = 1;
    hr = video->CheckFeatureSupport(D3D12_FEATURE_VIDEO_MOTION_ESTIMATOR, &caps, sizeof(caps));
    ok(hr == E_INVALIDARG, "motion estimator caps on node 1: %#lx\n", hr);
    caps.NodeIndex = 0;
    caps.InputFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    caps.BlockSizeFlags = D3D12_VIDEO_MOTION_ESTIMATOR_SEARCH_BLOCK_SIZE_FLAG_16X16;
    hr = video->CheckFeatureSupport(D3D12_FEATURE_VIDEO_MOTION_ESTIMATOR, &caps, sizeof(caps));
    ok(hr == S_OK && !caps.BlockSizeFlags && !caps.SizeRange.MaxWidth, "RGBA motion estimation: %#lx, flags %#x\n", hr,
            caps.BlockSizeFlags);
    caps.InputFormat = DXGI_FORMAT_NV12;
    hr = video->CheckFeatureSupport(D3D12_FEATURE_VIDEO_MOTION_ESTIMATOR, &caps, sizeof(caps));
    ok(hr == S_OK, "NV12 motion estimation: %#lx\n", hr);
    trace("motion estimation blocks %#x precision %#x size %ux%u..%ux%u\n", caps.BlockSizeFlags, caps.PrecisionFlags,
            caps.SizeRange.MinWidth, caps.SizeRange.MinHeight, caps.SizeRange.MaxWidth, caps.SizeRange.MaxHeight);
    if (!caps.BlockSizeFlags)
    {
        skip("no motion estimation\n");
        video1->Release();
        return;
    }
    dimension = max(64u, max(caps.SizeRange.MinWidth, caps.SizeRange.MinHeight));
    desc.InputFormat = DXGI_FORMAT_NV12;
    desc.BlockSize = (caps.BlockSizeFlags & D3D12_VIDEO_MOTION_ESTIMATOR_SEARCH_BLOCK_SIZE_FLAG_16X16)
            ? D3D12_VIDEO_MOTION_ESTIMATOR_SEARCH_BLOCK_SIZE_16X16 : D3D12_VIDEO_MOTION_ESTIMATOR_SEARCH_BLOCK_SIZE_8X8;
    desc.Precision = D3D12_VIDEO_MOTION_ESTIMATOR_VECTOR_PRECISION_QUARTER_PEL;
    desc.SizeRange.MaxWidth = desc.SizeRange.MinWidth = dimension;
    desc.SizeRange.MaxHeight = desc.SizeRange.MinHeight = dimension;

    size.InputFormat = DXGI_FORMAT_NV12;
    size.BlockSize = desc.BlockSize;
    size.Precision = desc.Precision;
    size.SizeRange = desc.SizeRange;
    hr = video->CheckFeatureSupport(D3D12_FEATURE_VIDEO_MOTION_ESTIMATOR_SIZE, &size, sizeof(size));
    if (hr == E_FAIL)
        skip("the driver does not report motion estimator sizes\n");
    else
        ok(hr == S_OK && (size.MotionVectorHeapMemoryPoolL0Size || size.MotionVectorHeapMemoryPoolL1Size),
                "motion estimator size: %#lx\n", hr);
    trace("motion vector heap L0 %I64u L1 %I64u, estimator L0 %I64u L1 %I64u\n", size.MotionVectorHeapMemoryPoolL0Size,
            size.MotionVectorHeapMemoryPoolL1Size, size.MotionEstimatorMemoryPoolL0Size,
            size.MotionEstimatorMemoryPoolL1Size);

    desc.NodeMask = 3;
    hr = video1->CreateVideoMotionEstimator(&desc, NULL, __uuidof(ID3D12VideoMotionEstimator),
            reinterpret_cast<void **>(&estimator));
    ok(hr == E_INVALIDARG && !estimator, "estimator on two nodes: %#lx\n", hr);
    desc.NodeMask = 0;
    desc.SizeRange.MinWidth = 0;
    hr = video1->CreateVideoMotionEstimator(&desc, NULL, __uuidof(ID3D12VideoMotionEstimator),
            reinterpret_cast<void **>(&estimator));
    ok(hr == E_INVALIDARG && !estimator, "estimator with a zero minimum width: %#lx\n", hr);
    desc.SizeRange.MinWidth = dimension;
    hr = video1->CreateVideoMotionEstimator(&desc, NULL, __uuidof(ID3D12VideoMotionEstimator),
            reinterpret_cast<void **>(&estimator));
    ok(hr == S_OK && estimator, "estimator: %#lx\n", hr);
    memcpy(&heap_desc, &desc, sizeof(heap_desc));
    hr = video1->CreateVideoMotionVectorHeap(&heap_desc, NULL, __uuidof(ID3D12VideoMotionVectorHeap),
            reinterpret_cast<void **>(&heap));
    ok(hr == S_OK && heap, "motion vector heap: %#lx\n", hr);
    if (estimator)
    {
        got = estimator->GetDesc();
        ok(got.BlockSize == desc.BlockSize && got.SizeRange.MaxWidth == dimension, "estimator desc block %u\n",
                got.BlockSize);
        hr = estimator->GetProtectedResourceSession(__uuidof(IUnknown), &session);
        ok(hr == DXGI_ERROR_NOT_FOUND && !session, "estimator protected session: %#lx\n", hr);
    }
    if (estimator && heap)
        TestMotionFrame(estimator, heap, dimension,
                desc.BlockSize == D3D12_VIDEO_MOTION_ESTIMATOR_SEARCH_BLOCK_SIZE_16X16 ? 16 : 8);
    if (heap) heap->Release();
    if (estimator) estimator->Release();
    video1->Release();
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
    TestProcess(video);
    TestMotion(video);

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
        got = decoder->GetDesc();
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
