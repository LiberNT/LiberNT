/*
 * PROJECT:     LiberNT DXGI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Desktop duplication of an output
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "dxgi_private.h"
#include "d3d11.h"

WINE_DEFAULT_DEBUG_CHANNEL(dxgi);

struct dxgi_output_duplication
{
    IDXGIOutputDuplication IDXGIOutputDuplication_iface;
    LONG refcount;
    struct wined3d_private_store private_store;
    IDXGIOutput *output;
    ID3D11Device *device;
    DXGI_OUTDUPL_DESC desc;
    RECT desktop;
    HDC screen_dc;
    HDC memory_dc;
    HBITMAP bitmap;
    BYTE *bits;
    BYTE *previous;
    UINT stride;
    BOOL have_previous;
    ID3D11Texture2D *frame;
    BOOL frame_acquired;
    RECT dirty;
    UINT dirty_count;
    POINT pointer_position;
    BOOL pointer_visible;
    HCURSOR pointer_cursor;
    BOOL pointer_reported;
    BOOL shape_pending;
};

static inline struct dxgi_output_duplication *impl_from_IDXGIOutputDuplication(IDXGIOutputDuplication *iface)
{
    return CONTAINING_RECORD(iface, struct dxgi_output_duplication, IDXGIOutputDuplication_iface);
}

static HRESULT STDMETHODCALLTYPE dxgi_output_duplication_QueryInterface(IDXGIOutputDuplication *iface,
        REFIID iid, void **object)
{
    TRACE("iface %p, iid %s, object %p.\n", iface, debugstr_guid(iid), object);

    if (IsEqualGUID(iid, &IID_IDXGIOutputDuplication)
            || IsEqualGUID(iid, &IID_IDXGIObject)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        IDXGIOutputDuplication_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE dxgi_output_duplication_AddRef(IDXGIOutputDuplication *iface)
{
    struct dxgi_output_duplication *duplication = impl_from_IDXGIOutputDuplication(iface);
    ULONG refcount = InterlockedIncrement(&duplication->refcount);

    TRACE("%p increasing refcount to %lu.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE dxgi_output_duplication_Release(IDXGIOutputDuplication *iface)
{
    struct dxgi_output_duplication *duplication = impl_from_IDXGIOutputDuplication(iface);
    ULONG refcount = InterlockedDecrement(&duplication->refcount);

    TRACE("%p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
    {
        if (duplication->frame)
            ID3D11Texture2D_Release(duplication->frame);
        if (duplication->memory_dc)
            DeleteDC(duplication->memory_dc);
        if (duplication->bitmap)
            DeleteObject(duplication->bitmap);
        if (duplication->screen_dc)
            ReleaseDC(NULL, duplication->screen_dc);
        free(duplication->previous);
        ID3D11Device_Release(duplication->device);
        IDXGIOutput_Release(duplication->output);
        wined3d_private_store_cleanup(&duplication->private_store);
        free(duplication);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_duplication_SetPrivateData(IDXGIOutputDuplication *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct dxgi_output_duplication *duplication = impl_from_IDXGIOutputDuplication(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return dxgi_set_private_data(&duplication->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE dxgi_output_duplication_SetPrivateDataInterface(IDXGIOutputDuplication *iface,
        REFGUID guid, const IUnknown *object)
{
    struct dxgi_output_duplication *duplication = impl_from_IDXGIOutputDuplication(iface);

    TRACE("iface %p, guid %s, object %p.\n", iface, debugstr_guid(guid), object);

    return dxgi_set_private_data_interface(&duplication->private_store, guid, object);
}

static HRESULT STDMETHODCALLTYPE dxgi_output_duplication_GetPrivateData(IDXGIOutputDuplication *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct dxgi_output_duplication *duplication = impl_from_IDXGIOutputDuplication(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return dxgi_get_private_data(&duplication->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE dxgi_output_duplication_GetParent(IDXGIOutputDuplication *iface,
        REFIID iid, void **parent)
{
    struct dxgi_output_duplication *duplication = impl_from_IDXGIOutputDuplication(iface);

    TRACE("iface %p, iid %s, parent %p.\n", iface, debugstr_guid(iid), parent);

    return IDXGIOutput_QueryInterface(duplication->output, iid, parent);
}

static void STDMETHODCALLTYPE dxgi_output_duplication_GetDesc(IDXGIOutputDuplication *iface,
        DXGI_OUTDUPL_DESC *desc)
{
    struct dxgi_output_duplication *duplication = impl_from_IDXGIOutputDuplication(iface);

    TRACE("iface %p, desc %p.\n", iface, desc);

    if (desc)
        *desc = duplication->desc;
}

static BOOL dxgi_output_duplication_capture(struct dxgi_output_duplication *duplication)
{
    UINT width = duplication->desc.ModeDesc.Width, height = duplication->desc.ModeDesc.Height;
    UINT x, y, top = height, bottom = 0, left = width, right = 0;
    BYTE *row;

    if (!BitBlt(duplication->memory_dc, 0, 0, width, height, duplication->screen_dc,
            duplication->desktop.left, duplication->desktop.top, SRCCOPY | CAPTUREBLT))
        return FALSE;
    GdiFlush();

    for (y = 0; y < height; ++y)
    {
        row = duplication->bits + y * duplication->stride;
        for (x = 0; x < width; ++x)
            row[x * 4 + 3] = 0xff;
    }

    if (!duplication->have_previous)
    {
        SetRect(&duplication->dirty, 0, 0, width, height);
        duplication->dirty_count = 1;
        duplication->have_previous = TRUE;
        memcpy(duplication->previous, duplication->bits, duplication->stride * height);
        return TRUE;
    }

    for (y = 0; y < height; ++y)
    {
        const DWORD *now = (const DWORD *)(duplication->bits + y * duplication->stride);
        const DWORD *before = (const DWORD *)(duplication->previous + y * duplication->stride);

        if (!memcmp(now, before, width * 4))
            continue;
        top = min(top, y);
        bottom = y + 1;
        for (x = 0; x < left && x < width; ++x)
        {
            if (now[x] != before[x])
            {
                left = x;
                break;
            }
        }
        for (x = width; x > right && x > 0; --x)
        {
            if (now[x - 1] != before[x - 1])
            {
                right = x;
                break;
            }
        }
    }

    if (top >= bottom)
        return FALSE;

    SetRect(&duplication->dirty, left, top, right, bottom);
    duplication->dirty_count = 1;
    memcpy(duplication->previous, duplication->bits, duplication->stride * height);
    return TRUE;
}

static BOOL dxgi_output_duplication_check_pointer(struct dxgi_output_duplication *duplication)
{
    CURSORINFO info = {sizeof(info)};
    POINT position;
    BOOL visible, changed;

    if (!GetCursorInfo(&info))
        return FALSE;

    visible = (info.flags & CURSOR_SHOWING) && info.hCursor;
    position.x = info.ptScreenPos.x - duplication->desktop.left;
    position.y = info.ptScreenPos.y - duplication->desktop.top;
    changed = !duplication->pointer_reported
            || visible != duplication->pointer_visible
            || position.x != duplication->pointer_position.x
            || position.y != duplication->pointer_position.y;
    if (visible && info.hCursor != duplication->pointer_cursor)
    {
        duplication->pointer_cursor = info.hCursor;
        duplication->shape_pending = TRUE;
        changed = TRUE;
    }
    duplication->pointer_visible = visible;
    duplication->pointer_position = position;
    duplication->pointer_reported = TRUE;
    return changed;
}

struct dxgi_pointer_bitmap_info
{
    BITMAPINFOHEADER header;
    RGBQUAD colors[2];
};

static void dxgi_pointer_bitmap_info_init(struct dxgi_pointer_bitmap_info *bmi, UINT width, UINT height, WORD bpp)
{
    memset(bmi, 0, sizeof(*bmi));
    bmi->header.biSize = sizeof(bmi->header);
    bmi->header.biWidth = width;
    bmi->header.biHeight = -(LONG)height;
    bmi->header.biPlanes = 1;
    bmi->header.biBitCount = bpp;
    bmi->header.biCompression = BI_RGB;
}

static void dxgi_pointer_apply_mask(HDC dc, HBITMAP mask_bitmap, DWORD *pixels, UINT width, UINT height)
{
    struct dxgi_pointer_bitmap_info bmi;
    UINT x, y, stride = ((width + 31) / 32) * 4;
    BYTE *mask;

    for (x = 0; x < width * height; ++x)
    {
        if (pixels[x] & 0xff000000)
            return;
    }
    if (!(mask = malloc(stride * height)))
        return;
    dxgi_pointer_bitmap_info_init(&bmi, width, height, 1);
    if (GetDIBits(dc, mask_bitmap, 0, height, mask, (BITMAPINFO *)&bmi, DIB_RGB_COLORS) == height)
    {
        for (y = 0; y < height; ++y)
        {
            for (x = 0; x < width; ++x)
            {
                if (!(mask[y * stride + x / 8] & (0x80 >> (x % 8))))
                    pixels[y * width + x] |= 0xff000000;
            }
        }
    }
    free(mask);
}

static BOOL dxgi_pointer_color_shape(HDC dc, const ICONINFO *icon, UINT width, UINT height, void *buffer,
        UINT buffer_size, UINT *required, DXGI_OUTDUPL_POINTER_SHAPE_INFO *info)
{
    struct dxgi_pointer_bitmap_info bmi;

    info->Type = DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR;
    info->Height = height;
    info->Pitch = width * 4;
    *required = info->Pitch * height;
    if (buffer_size < *required)
        return TRUE;
    dxgi_pointer_bitmap_info_init(&bmi, width, height, 32);
    if (GetDIBits(dc, icon->hbmColor, 0, height, buffer, (BITMAPINFO *)&bmi, DIB_RGB_COLORS) != height)
        return FALSE;
    dxgi_pointer_apply_mask(dc, icon->hbmMask, buffer, width, height);
    return TRUE;
}

static BOOL dxgi_pointer_monochrome_shape(HDC dc, const ICONINFO *icon, UINT width, UINT height, void *buffer,
        UINT buffer_size, UINT *required, DXGI_OUTDUPL_POINTER_SHAPE_INFO *info)
{
    struct dxgi_pointer_bitmap_info bmi;

    info->Type = DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME;
    info->Height = height * 2;
    info->Pitch = ((width + 31) / 32) * 4;
    *required = info->Pitch * height * 2;
    if (buffer_size < *required)
        return TRUE;
    dxgi_pointer_bitmap_info_init(&bmi, width, height * 2, 1);
    return GetDIBits(dc, icon->hbmMask, 0, height * 2, buffer, (BITMAPINFO *)&bmi, DIB_RGB_COLORS) == height * 2;
}

static BOOL dxgi_output_duplication_shape(HCURSOR cursor, void *buffer, UINT buffer_size,
        UINT *required, DXGI_OUTDUPL_POINTER_SHAPE_INFO *info)
{
    ICONINFO icon;
    BITMAP bitmap;
    BOOL ret = FALSE;
    HDC dc;

    if (!GetIconInfo(cursor, &icon))
        return FALSE;
    if (GetObjectW(icon.hbmMask, sizeof(bitmap), &bitmap) && (dc = GetDC(NULL)))
    {
        info->HotSpot.x = icon.xHotspot;
        info->HotSpot.y = icon.yHotspot;
        info->Width = bitmap.bmWidth;
        if (icon.hbmColor)
            ret = dxgi_pointer_color_shape(dc, &icon, bitmap.bmWidth, bitmap.bmHeight, buffer, buffer_size,
                    required, info);
        else
            ret = dxgi_pointer_monochrome_shape(dc, &icon, bitmap.bmWidth, bitmap.bmHeight / 2, buffer,
                    buffer_size, required, info);
        ReleaseDC(NULL, dc);
    }
    if (icon.hbmColor)
        DeleteObject(icon.hbmColor);
    DeleteObject(icon.hbmMask);
    return ret;
}

static UINT dxgi_output_duplication_shape_size(HCURSOR cursor)
{
    DXGI_OUTDUPL_POINTER_SHAPE_INFO info;
    UINT required = 0;

    if (!cursor || !dxgi_output_duplication_shape(cursor, NULL, 0, &required, &info))
        return 0;
    return required;
}

static BOOL dxgi_output_duplication_desktop_changed(struct dxgi_output_duplication *duplication)
{
    DXGI_OUTPUT_DESC desc;

    if (FAILED(IDXGIOutput_GetDesc(duplication->output, &desc)))
        return TRUE;
    return !EqualRect(&desc.DesktopCoordinates, &duplication->desktop);
}

static HRESULT STDMETHODCALLTYPE dxgi_output_duplication_AcquireNextFrame(IDXGIOutputDuplication *iface,
        UINT timeout, DXGI_OUTDUPL_FRAME_INFO *frame_info, IDXGIResource **resource)
{
    struct dxgi_output_duplication *duplication = impl_from_IDXGIOutputDuplication(iface);
    UINT width = duplication->desc.ModeDesc.Width, height = duplication->desc.ModeDesc.Height;
    BOOL image_changed, pointer_changed;
    D3D11_SUBRESOURCE_DATA data;
    D3D11_TEXTURE2D_DESC texture_desc;
    ID3D11Texture2D *texture;
    LARGE_INTEGER now;
    ULONGLONG start = GetTickCount64();
    DWORD elapsed;
    HRESULT hr;

    TRACE("iface %p, timeout %u, frame_info %p, resource %p.\n", iface, timeout, frame_info, resource);

    if (!frame_info || !resource)
        return E_INVALIDARG;
    *resource = NULL;
    if (duplication->frame_acquired)
        return DXGI_ERROR_INVALID_CALL;
    if (dxgi_output_duplication_desktop_changed(duplication))
        return DXGI_ERROR_ACCESS_LOST;

    for (;;)
    {
        image_changed = dxgi_output_duplication_capture(duplication);
        pointer_changed = dxgi_output_duplication_check_pointer(duplication);
        if (image_changed || pointer_changed)
            break;
        elapsed = (DWORD)(GetTickCount64() - start);
        if (timeout != INFINITE && elapsed >= timeout)
            return DXGI_ERROR_WAIT_TIMEOUT;
        Sleep(timeout == INFINITE ? 16 : min(16, timeout - elapsed));
    }

    if (image_changed || !duplication->frame)
    {
        texture_desc.Width = width;
        texture_desc.Height = height;
        texture_desc.MipLevels = 1;
        texture_desc.ArraySize = 1;
        texture_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        texture_desc.SampleDesc.Count = 1;
        texture_desc.SampleDesc.Quality = 0;
        texture_desc.Usage = D3D11_USAGE_DEFAULT;
        texture_desc.BindFlags = 0;
        texture_desc.CPUAccessFlags = 0;
        texture_desc.MiscFlags = 0;
        data.pSysMem = duplication->bits;
        data.SysMemPitch = duplication->stride;
        data.SysMemSlicePitch = 0;
        if (FAILED(hr = ID3D11Device_CreateTexture2D(duplication->device, &texture_desc, &data, &texture)))
        {
            WARN("Failed to create the frame texture, hr %#lx.\n", hr);
            return hr;
        }
        if (duplication->frame)
            ID3D11Texture2D_Release(duplication->frame);
        duplication->frame = texture;
    }
    else
    {
        duplication->dirty_count = 0;
    }

    QueryPerformanceCounter(&now);
    memset(frame_info, 0, sizeof(*frame_info));
    if (image_changed)
    {
        frame_info->LastPresentTime = now;
        frame_info->AccumulatedFrames = 1;
    }
    if (pointer_changed)
        frame_info->LastMouseUpdateTime = now;
    frame_info->PointerPosition.Position = duplication->pointer_position;
    frame_info->PointerPosition.Visible = duplication->pointer_visible;
    frame_info->TotalMetadataBufferSize = duplication->dirty_count * sizeof(RECT);
    if (duplication->shape_pending)
        frame_info->PointerShapeBufferSize = dxgi_output_duplication_shape_size(duplication->pointer_cursor);

    hr = ID3D11Texture2D_QueryInterface(duplication->frame, &IID_IDXGIResource, (void **)resource);
    if (SUCCEEDED(hr))
        duplication->frame_acquired = TRUE;
    return hr;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_duplication_GetFrameDirtyRects(IDXGIOutputDuplication *iface,
        UINT buffer_size, RECT *buffer, UINT *required)
{
    struct dxgi_output_duplication *duplication = impl_from_IDXGIOutputDuplication(iface);
    UINT size = duplication->dirty_count * sizeof(RECT);

    TRACE("iface %p, buffer_size %u, buffer %p, required %p.\n", iface, buffer_size, buffer, required);

    if (!required)
        return E_INVALIDARG;
    if (!duplication->frame_acquired)
        return DXGI_ERROR_INVALID_CALL;
    *required = size;
    if (buffer_size < size)
        return DXGI_ERROR_MORE_DATA;
    if (size)
    {
        if (!buffer)
            return E_INVALIDARG;
        buffer[0] = duplication->dirty;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_duplication_GetFrameMoveRects(IDXGIOutputDuplication *iface,
        UINT buffer_size, DXGI_OUTDUPL_MOVE_RECT *buffer, UINT *required)
{
    struct dxgi_output_duplication *duplication = impl_from_IDXGIOutputDuplication(iface);

    TRACE("iface %p, buffer_size %u, buffer %p, required %p.\n", iface, buffer_size, buffer, required);

    if (!required)
        return E_INVALIDARG;
    if (!duplication->frame_acquired)
        return DXGI_ERROR_INVALID_CALL;
    *required = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_duplication_GetFramePointerShape(IDXGIOutputDuplication *iface,
        UINT buffer_size, void *buffer, UINT *required, DXGI_OUTDUPL_POINTER_SHAPE_INFO *info)
{
    struct dxgi_output_duplication *duplication = impl_from_IDXGIOutputDuplication(iface);

    TRACE("iface %p, buffer_size %u, buffer %p, required %p, info %p.\n",
            iface, buffer_size, buffer, required, info);

    if (!required || !info)
        return E_INVALIDARG;
    if (!duplication->frame_acquired)
        return DXGI_ERROR_INVALID_CALL;
    memset(info, 0, sizeof(*info));
    *required = 0;
    if (!duplication->pointer_cursor)
        return S_OK;
    if (!dxgi_output_duplication_shape(duplication->pointer_cursor, buffer, buffer_size, required, info))
        return E_FAIL;
    if (buffer_size < *required)
        return DXGI_ERROR_MORE_DATA;
    duplication->shape_pending = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_duplication_MapDesktopSurface(IDXGIOutputDuplication *iface,
        DXGI_MAPPED_RECT *locked_rect)
{
    TRACE("iface %p, locked_rect %p.\n", iface, locked_rect);

    return DXGI_ERROR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_duplication_UnMapDesktopSurface(IDXGIOutputDuplication *iface)
{
    TRACE("iface %p.\n", iface);

    return DXGI_ERROR_INVALID_CALL;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_duplication_ReleaseFrame(IDXGIOutputDuplication *iface)
{
    struct dxgi_output_duplication *duplication = impl_from_IDXGIOutputDuplication(iface);

    TRACE("iface %p.\n", iface);

    if (!duplication->frame_acquired)
        return DXGI_ERROR_INVALID_CALL;
    duplication->frame_acquired = FALSE;
    return S_OK;
}

static const struct IDXGIOutputDuplicationVtbl dxgi_output_duplication_vtbl =
{
    dxgi_output_duplication_QueryInterface,
    dxgi_output_duplication_AddRef,
    dxgi_output_duplication_Release,
    dxgi_output_duplication_SetPrivateData,
    dxgi_output_duplication_SetPrivateDataInterface,
    dxgi_output_duplication_GetPrivateData,
    dxgi_output_duplication_GetParent,
    dxgi_output_duplication_GetDesc,
    dxgi_output_duplication_AcquireNextFrame,
    dxgi_output_duplication_GetFrameDirtyRects,
    dxgi_output_duplication_GetFrameMoveRects,
    dxgi_output_duplication_GetFramePointerShape,
    dxgi_output_duplication_MapDesktopSurface,
    dxgi_output_duplication_UnMapDesktopSurface,
    dxgi_output_duplication_ReleaseFrame,
};

static BOOL dxgi_output_duplication_same_adapter(IDXGIOutput *output, ID3D11Device *device)
{
    IDXGIDevice *dxgi_device;
    IDXGIAdapter *device_adapter, *output_adapter;
    DXGI_ADAPTER_DESC device_desc, output_desc;
    BOOL same = FALSE;

    if (FAILED(ID3D11Device_QueryInterface(device, &IID_IDXGIDevice, (void **)&dxgi_device)))
        return FALSE;
    if (SUCCEEDED(IDXGIDevice_GetAdapter(dxgi_device, &device_adapter)))
    {
        if (SUCCEEDED(IDXGIOutput_GetParent(output, &IID_IDXGIAdapter, (void **)&output_adapter)))
        {
            if (SUCCEEDED(IDXGIAdapter_GetDesc(device_adapter, &device_desc))
                    && SUCCEEDED(IDXGIAdapter_GetDesc(output_adapter, &output_desc)))
            {
                same = device_desc.AdapterLuid.LowPart == output_desc.AdapterLuid.LowPart
                        && device_desc.AdapterLuid.HighPart == output_desc.AdapterLuid.HighPart;
            }
            IDXGIAdapter_Release(output_adapter);
        }
        IDXGIAdapter_Release(device_adapter);
    }
    IDXGIDevice_Release(dxgi_device);
    return same;
}

HRESULT dxgi_output_duplication_create(IDXGIOutput *output, IUnknown *device_unknown,
        IDXGIOutputDuplication **out)
{
    struct dxgi_output_duplication *duplication;
    DXGI_OUTPUT_DESC output_desc;
    DEVMODEW mode = {0};
    BITMAPINFO bmi = {{0}};
    ID3D11Device *device;
    UINT width, height;
    void *bits;

    if (!out)
        return E_INVALIDARG;
    *out = NULL;
    if (!device_unknown || FAILED(IUnknown_QueryInterface(device_unknown, &IID_ID3D11Device, (void **)&device)))
        return E_INVALIDARG;
    if (!dxgi_output_duplication_same_adapter(output, device) || FAILED(IDXGIOutput_GetDesc(output, &output_desc)))
    {
        ID3D11Device_Release(device);
        return E_INVALIDARG;
    }
    if (!output_desc.AttachedToDesktop)
    {
        ID3D11Device_Release(device);
        return DXGI_ERROR_UNSUPPORTED;
    }

    if (!(duplication = calloc(1, sizeof(*duplication))))
    {
        ID3D11Device_Release(device);
        return E_OUTOFMEMORY;
    }
    duplication->IDXGIOutputDuplication_iface.lpVtbl = &dxgi_output_duplication_vtbl;
    duplication->refcount = 1;
    wined3d_private_store_init(&duplication->private_store);
    duplication->output = output;
    IDXGIOutput_AddRef(output);
    duplication->device = device;
    duplication->desktop = output_desc.DesktopCoordinates;

    width = output_desc.DesktopCoordinates.right - output_desc.DesktopCoordinates.left;
    height = output_desc.DesktopCoordinates.bottom - output_desc.DesktopCoordinates.top;
    mode.dmSize = sizeof(mode);
    EnumDisplaySettingsW(output_desc.DeviceName, ENUM_CURRENT_SETTINGS, &mode);
    duplication->desc.ModeDesc.Width = width;
    duplication->desc.ModeDesc.Height = height;
    duplication->desc.ModeDesc.RefreshRate.Numerator = mode.dmDisplayFrequency > 1 ? mode.dmDisplayFrequency : 60;
    duplication->desc.ModeDesc.RefreshRate.Denominator = 1;
    duplication->desc.ModeDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    duplication->desc.ModeDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;
    duplication->desc.ModeDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    duplication->desc.Rotation = DXGI_MODE_ROTATION_IDENTITY;
    duplication->desc.DesktopImageInSystemMemory = FALSE;

    duplication->stride = width * 4;
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -(LONG)height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    duplication->screen_dc = GetDC(NULL);
    duplication->memory_dc = CreateCompatibleDC(duplication->screen_dc);
    duplication->bitmap = CreateDIBSection(duplication->screen_dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    duplication->previous = malloc(duplication->stride * height);
    if (!duplication->screen_dc || !duplication->memory_dc || !duplication->bitmap || !duplication->previous)
    {
        IDXGIOutputDuplication_Release(&duplication->IDXGIOutputDuplication_iface);
        return E_OUTOFMEMORY;
    }
    duplication->bits = bits;
    SelectObject(duplication->memory_dc, duplication->bitmap);

    TRACE("Created output duplication %p, %ux%u.\n", duplication, width, height);
    *out = &duplication->IDXGIOutputDuplication_iface;
    return S_OK;
}
