/*
 * Copyright 2014 Henri Verbeet for CodeWeavers, 2026 Earzu Chan
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "device_private.h"
#include <float.h>

WINE_DEFAULT_DEBUG_CHANNEL(d2d);

struct d2d_draw_text_layout_ctx
{
    ID2D1Brush *brush;
    D2D1_DRAW_TEXT_OPTIONS options;
};
static ID2D1Brush *d2d_draw_get_text_brush(struct d2d_draw_text_layout_ctx *context, IUnknown *effect)
{
    ID2D1Brush *brush = NULL;

    if (effect && SUCCEEDED(IUnknown_QueryInterface(effect, &IID_ID2D1Brush, (void**)&brush)))
        return brush;

    ID2D1Brush_AddRef(context->brush);
    return context->brush;
}

static void d2d_size_set(D2D1_SIZE_U *dst, float width, float height)
{
    dst->width = width;
    dst->height = height;
}

void STDMETHODCALLTYPE d2d_device_context_DrawText(ID2D1DeviceContext6 *iface,
        const WCHAR *string, UINT32 string_len, IDWriteTextFormat *text_format, const D2D1_RECT_F *layout_rect,
        ID2D1Brush *brush, D2D1_DRAW_TEXT_OPTIONS options, DWRITE_MEASURING_MODE measuring_mode)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);
    IDWriteTextLayout *text_layout;
    IDWriteFactory *dwrite_factory;
    D2D1_POINT_2F origin;
    float width, height;
    HRESULT hr;

    TRACE("iface %p, string %s, string_len %u, text_format %p, layout_rect %s, "
            "brush %p, options %#x, measuring_mode %#x.\n",
            iface, debugstr_wn(string, string_len), string_len, text_format, debug_d2d_rect_f(layout_rect),
            brush, options, measuring_mode);

    if (FAILED(hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
            &IID_IDWriteFactory, (IUnknown **)&dwrite_factory)))
    {
        ERR("Failed to create dwrite factory, hr %#lx.\n", hr);
        return;
    }

    width = max(0.0f, layout_rect->right - layout_rect->left);
    height = max(0.0f, layout_rect->bottom - layout_rect->top);
    if (measuring_mode == DWRITE_MEASURING_MODE_NATURAL)
        hr = IDWriteFactory_CreateTextLayout(dwrite_factory, string, string_len, text_format,
                width, height, &text_layout);
    else
        hr = IDWriteFactory_CreateGdiCompatibleTextLayout(dwrite_factory, string, string_len, text_format,
                width, height, render_target->desc.dpiX / 96.0f, (DWRITE_MATRIX *)&render_target->drawing_state.transform,
                measuring_mode == DWRITE_MEASURING_MODE_GDI_NATURAL, &text_layout);
    IDWriteFactory_Release(dwrite_factory);
    if (FAILED(hr))
    {
        ERR("Failed to create text layout, hr %#lx.\n", hr);
        return;
    }

    d2d_point_set(&origin, min(layout_rect->left, layout_rect->right), min(layout_rect->top, layout_rect->bottom));
    ID2D1DeviceContext1_DrawTextLayout((ID2D1DeviceContext1 *)iface, origin, text_layout, brush, options);
    IDWriteTextLayout_Release(text_layout);
}

void STDMETHODCALLTYPE d2d_device_context_DrawTextLayout(ID2D1DeviceContext6 *iface,
        D2D1_POINT_2F origin, IDWriteTextLayout *layout, ID2D1Brush *brush, D2D1_DRAW_TEXT_OPTIONS options)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);
    struct d2d_draw_text_layout_ctx ctx;
    HRESULT hr;

    TRACE("iface %p, origin %s, layout %p, brush %p, options %#x.\n",
            iface, debug_d2d_point_2f(&origin), layout, brush, options);

    ctx.brush = brush;
    ctx.options = options;

    if (FAILED(hr = IDWriteTextLayout_Draw(layout,
            &ctx, &render_target->IDWriteTextRenderer_iface, origin.x, origin.y)))
        FIXME("Failed to draw text layout, hr %#lx.\n", hr);
}

static D2D1_ANTIALIAS_MODE d2d_device_context_set_aa_mode_from_text_aa_mode(struct d2d_device_context *rt)
{
    D2D1_ANTIALIAS_MODE prev_antialias_mode = rt->drawing_state.antialiasMode;
    rt->drawing_state.antialiasMode = rt->drawing_state.textAntialiasMode == D2D1_TEXT_ANTIALIAS_MODE_ALIASED ?
            D2D1_ANTIALIAS_MODE_ALIASED : D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
    return prev_antialias_mode;
}

static HRESULT d2d_device_context_get_glyph_run_geometry(struct d2d_device_context *context,
        const DWRITE_GLYPH_RUN *glyph_run, ID2D1PathGeometry **result)
{
    ID2D1PathGeometry *geometry;
    ID2D1GeometrySink *sink;
    HRESULT hr;

    *result = NULL;

    if (FAILED(hr = ID2D1Factory_CreatePathGeometry(context->factory, &geometry)))
        return hr;

    if (FAILED(hr = ID2D1PathGeometry_Open(geometry, &sink)))
    {
        ID2D1PathGeometry_Release(geometry);
        return hr;
    }

    if (FAILED(hr = IDWriteFontFace_GetGlyphRunOutline(glyph_run->fontFace, glyph_run->fontEmSize,
            glyph_run->glyphIndices, glyph_run->glyphAdvances, glyph_run->glyphOffsets, glyph_run->glyphCount,
            glyph_run->isSideways, glyph_run->bidiLevel & 1, (IDWriteGeometrySink *)sink)))
    {
        ERR("Failed to get glyph run outline, hr %#lx.\n", hr);
        ID2D1GeometrySink_Release(sink);
        ID2D1PathGeometry_Release(geometry);
        return hr;
    }

    if (FAILED(hr = ID2D1GeometrySink_Close(sink)))
        ERR("Failed to close geometry sink, hr %#lx.\n", hr);
    ID2D1GeometrySink_Release(sink);

    if (hr == S_OK)
        *result = geometry;
    else
        ID2D1PathGeometry_Release(geometry);

    return hr;
}

static void d2d_device_context_draw_glyph_run_outline(struct d2d_device_context *context,
        D2D1_POINT_2F baseline_origin, const DWRITE_GLYPH_RUN *glyph_run, ID2D1Brush *brush)
{
    D2D1_MATRIX_3X2_F *transform, prev_transform;
    D2D1_ANTIALIAS_MODE prev_antialias_mode;
    ID2D1PathGeometry *geometry;
    HRESULT hr;

    if (FAILED(hr = d2d_device_context_get_glyph_run_geometry(context, glyph_run, &geometry)))
    {
        ERR("Failed to create geometry, hr %#lx.\n", hr);
        return;
    }

    transform = &context->drawing_state.transform;
    prev_transform = *transform;
    transform->_31 += baseline_origin.x * transform->_11 + baseline_origin.y * transform->_21;
    transform->_32 += baseline_origin.x * transform->_12 + baseline_origin.y * transform->_22;
    prev_antialias_mode = d2d_device_context_set_aa_mode_from_text_aa_mode(context);
    d2d_device_context_fill_geometry(context, unsafe_impl_from_ID2D1Geometry((ID2D1Geometry *)geometry),
            unsafe_impl_from_ID2D1Brush(brush), NULL);
    context->drawing_state.antialiasMode = prev_antialias_mode;
    *transform = prev_transform;

    ID2D1PathGeometry_Release(geometry);
}

static HRESULT d2d_device_context_get_glyph_run_analysis(struct d2d_device_context *context,
        D2D1_POINT_2F baseline_origin, const DWRITE_GLYPH_RUN *glyph_run,
        DWRITE_RENDERING_MODE rendering_mode, DWRITE_MEASURING_MODE measuring_mode,
        DWRITE_TEXT_ANTIALIAS_MODE antialias_mode, DWRITE_TEXTURE_TYPE *texture_type,
        IDWriteGlyphRunAnalysis **result, float shift_x, float shift_y)
{
    IDWriteGlyphRunAnalysis *analysis;
    IDWriteFactory2 *dwrite_factory;
    D2D1_MATRIX_3X2_F *transform;
    float scale_x, scale_y;
    DWRITE_MATRIX m;
    HRESULT hr;

    *result = NULL;

    if (FAILED(hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_IDWriteFactory2,
            (IUnknown **)&dwrite_factory)))
    {
        ERR("Failed to create dwrite factory, hr %#lx.\n", hr);
        return hr;
    }

    transform = &context->drawing_state.transform;

    scale_x = context->desc.dpiX / 96.0f;
    m.m11 = transform->_11 * scale_x;
    m.m21 = transform->_21 * scale_x;
    m.dx  = transform->_31 * scale_x - shift_x;

    scale_y = context->desc.dpiY / 96.0f;
    m.m12 = transform->_12 * scale_y;
    m.m22 = transform->_22 * scale_y;
    m.dy  = transform->_32 * scale_y - shift_y;

    hr = IDWriteFactory2_CreateGlyphRunAnalysis(dwrite_factory, glyph_run, &m, rendering_mode,
            measuring_mode, DWRITE_GRID_FIT_MODE_DEFAULT, antialias_mode, baseline_origin.x,
            baseline_origin.y, &analysis);
    IDWriteFactory2_Release(dwrite_factory);
    if (FAILED(hr))
    {
        ERR("Failed to create glyph run analysis, hr %#lx.\n", hr);
        return hr;
    }

    if (rendering_mode == DWRITE_RENDERING_MODE_ALIASED || antialias_mode == DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE)
        *texture_type = DWRITE_TEXTURE_ALIASED_1x1;
    else
        *texture_type = DWRITE_TEXTURE_CLEARTYPE_3x1;

    *result = analysis;

    return S_OK;
}

#define D2D_GLYPH_PAD 3
#define D2D_GLYPH_ENTRIES 2048
#define D2D_GLYPH_KEY_MAX 8192

struct d2d_glyph_entry
{
    UINT64 hash;
    void *key;
    UINT32 key_size;
    IDWriteFontFace *face;
    UINT32 x, y;
    RECT bounds;
};

struct d2d_glyph_atlas
{
    ID2D1Bitmap *bitmap;
    ID2D1BitmapBrush *brush;
    float dpi_x, dpi_y;
    UINT32 width, height, shelf_x, shelf_y, shelf_h, count;
    struct d2d_glyph_entry entries[D2D_GLYPH_ENTRIES];
};

struct d2d_glyph_key
{
    void *font_face;
    float em_size, m[4], frac_x, frac_y, dpi_x, dpi_y;
    UINT32 count, flags, rendering_mode, measuring_mode, antialias_mode;
};

static void d2d_glyph_atlas_clear(struct d2d_glyph_atlas *atlas)
{
    UINT32 i;

    for (i = 0; i < D2D_GLYPH_ENTRIES; ++i)
    {
        if (!atlas->entries[i].key)
            continue;
        free(atlas->entries[i].key);
        IDWriteFontFace_Release(atlas->entries[i].face);
        memset(&atlas->entries[i], 0, sizeof(atlas->entries[i]));
    }
    atlas->count = atlas->shelf_x = atlas->shelf_y = atlas->shelf_h = 0;
}

void d2d_glyph_atlas_destroy(struct d2d_glyph_atlas *atlas)
{
    if (!atlas)
        return;
    d2d_glyph_atlas_clear(atlas);
    if (atlas->brush)
        ID2D1BitmapBrush_Release(atlas->brush);
    if (atlas->bitmap)
        ID2D1Bitmap_Release(atlas->bitmap);
    free(atlas);
}

static struct d2d_glyph_atlas *d2d_device_context_get_glyph_atlas(struct d2d_device_context *context,
        BOOL cleartype)
{
    struct d2d_glyph_atlas *atlas = context->glyph_atlas[cleartype];
    D2D1_BITMAP_PROPERTIES bitmap_desc;
    D2D1_BRUSH_PROPERTIES brush_desc;
    D2D1_SIZE_U size;

    if (atlas && atlas->dpi_x == context->desc.dpiX && atlas->dpi_y == context->desc.dpiY)
        return atlas;

    d2d_glyph_atlas_destroy(atlas);
    context->glyph_atlas[cleartype] = NULL;
    if (!(atlas = calloc(1, sizeof(*atlas))))
        return NULL;

    atlas->dpi_x = context->desc.dpiX;
    atlas->dpi_y = context->desc.dpiY;
    atlas->width = cleartype ? 3 * 2048 : 2048;
    atlas->height = 2048;
    size.width = atlas->width;
    size.height = atlas->height;
    bitmap_desc.pixelFormat.format = DXGI_FORMAT_A8_UNORM;
    bitmap_desc.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bitmap_desc.dpiX = context->desc.dpiX * (cleartype ? 3.0f : 1.0f);
    bitmap_desc.dpiY = context->desc.dpiY;
    brush_desc.opacity = 1.0f;
    brush_desc.transform = identity;
    if (FAILED(d2d_device_context_CreateBitmap(&context->ID2D1DeviceContext6_iface,
            size, NULL, 0, &bitmap_desc, &atlas->bitmap))
            || FAILED(d2d_device_context_CreateBitmapBrush(&context->ID2D1DeviceContext6_iface,
            atlas->bitmap, NULL, &brush_desc, &atlas->brush)))
    {
        d2d_glyph_atlas_destroy(atlas);
        return NULL;
    }

    context->glyph_atlas[cleartype] = atlas;
    return atlas;
}

static BOOL d2d_glyph_atlas_alloc(struct d2d_glyph_atlas *atlas, UINT32 width, UINT32 height,
        UINT32 *x, UINT32 *y)
{
    width = (width + 2) / 3 * 3;
    if (width > atlas->width || height > atlas->height)
        return FALSE;

    if (atlas->shelf_x + width > atlas->width)
    {
        atlas->shelf_y += atlas->shelf_h;
        atlas->shelf_x = atlas->shelf_h = 0;
    }
    if (atlas->shelf_y + height > atlas->height)
        return FALSE;

    *x = atlas->shelf_x;
    *y = atlas->shelf_y;
    atlas->shelf_x += width;
    atlas->shelf_h = max(atlas->shelf_h, height);
    return TRUE;
}

static BOOL d2d_device_context_draw_glyph_run_cached(struct d2d_device_context *context,
        D2D1_POINT_2F baseline_origin, const DWRITE_GLYPH_RUN *glyph_run, struct d2d_brush *brush,
        DWRITE_RENDERING_MODE rendering_mode, DWRITE_MEASURING_MODE measuring_mode,
        DWRITE_TEXT_ANTIALIAS_MODE antialias_mode)
{
    BOOL cleartype = !(rendering_mode == DWRITE_RENDERING_MODE_ALIASED
            || antialias_mode == DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    const D2D1_MATRIX_3X2_F *world = &context->drawing_state.transform;
    float scale_x = context->desc.dpiX / 96.0f, scale_y = context->desc.dpiY / 96.0f;
    BYTE key_buffer[D2D_GLYPH_KEY_MAX];
    struct d2d_glyph_key *key = (struct d2d_glyph_key *)key_buffer;
    struct d2d_glyph_entry *entry = NULL;
    struct d2d_glyph_atlas *atlas;
    float dev_x, dev_y, shift_x, shift_y;
    UINT32 key_size, count, slot, i;
    D2D1_MATRIX_3X2_F saved;
    struct d2d_brush *opacity;
    D2D1_RECT_F run_rect;
    UINT64 hash;
    BYTE *p;

    count = glyph_run->glyphCount;
    key_size = sizeof(*key) + count * (sizeof(UINT16) + (glyph_run->glyphAdvances ? sizeof(float) : 0)
            + (glyph_run->glyphOffsets ? sizeof(DWRITE_GLYPH_OFFSET) : 0));
    if (!count || key_size > sizeof(key_buffer))
        return FALSE;

    dev_x = (baseline_origin.x * world->_11 + baseline_origin.y * world->_21 + world->_31) * scale_x;
    dev_y = (baseline_origin.x * world->_12 + baseline_origin.y * world->_22 + world->_32) * scale_y;
    shift_x = floorf(dev_x);
    shift_y = floorf(dev_y);

    memset(key, 0, sizeof(*key));
    key->font_face = glyph_run->fontFace;
    key->em_size = glyph_run->fontEmSize;
    key->m[0] = world->_11 * scale_x;
    key->m[1] = world->_12 * scale_y;
    key->m[2] = world->_21 * scale_x;
    key->m[3] = world->_22 * scale_y;
    key->frac_x = dev_x - shift_x;
    key->frac_y = dev_y - shift_y;
    key->dpi_x = context->desc.dpiX;
    key->dpi_y = context->desc.dpiY;
    key->count = count;
    key->flags = (glyph_run->isSideways ? 1 : 0) | (glyph_run->bidiLevel << 1)
            | (glyph_run->glyphAdvances ? 0x100 : 0) | (glyph_run->glyphOffsets ? 0x200 : 0);
    key->rendering_mode = rendering_mode;
    key->measuring_mode = measuring_mode;
    key->antialias_mode = antialias_mode;
    p = (BYTE *)(key + 1);
    memcpy(p, glyph_run->glyphIndices, count * sizeof(UINT16));
    p += count * sizeof(UINT16);
    if (glyph_run->glyphAdvances)
    {
        memcpy(p, glyph_run->glyphAdvances, count * sizeof(float));
        p += count * sizeof(float);
    }
    if (glyph_run->glyphOffsets)
        memcpy(p, glyph_run->glyphOffsets, count * sizeof(DWRITE_GLYPH_OFFSET));

    hash = 14695981039346656037ull;
    for (i = 0; i < key_size; ++i)
        hash = (hash ^ key_buffer[i]) * 1099511628211ull;

    if (!(atlas = d2d_device_context_get_glyph_atlas(context, cleartype)))
        return FALSE;

    for (slot = hash % D2D_GLYPH_ENTRIES, i = 0; i < D2D_GLYPH_ENTRIES; ++i, slot = (slot + 1) % D2D_GLYPH_ENTRIES)
    {
        if (!atlas->entries[slot].key)
            break;
        if (atlas->entries[slot].hash == hash && atlas->entries[slot].key_size == key_size
                && !memcmp(atlas->entries[slot].key, key_buffer, key_size))
        {
            entry = &atlas->entries[slot];
            break;
        }
    }

    if (!entry)
    {
        IDWriteGlyphRunAnalysis *analysis;
        DWRITE_TEXTURE_TYPE texture_type;
        UINT32 width, height, pw, ph, x, y, row;
        BYTE *alpha = NULL, *padded = NULL;
        D2D1_RECT_U upload;
        RECT bounds;

        if (FAILED(d2d_device_context_get_glyph_run_analysis(context, baseline_origin, glyph_run,
                rendering_mode, measuring_mode, antialias_mode, &texture_type, &analysis, shift_x, shift_y)))
            return FALSE;

        if (FAILED(IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(analysis, texture_type, &bounds)))
        {
            IDWriteGlyphRunAnalysis_Release(analysis);
            return FALSE;
        }

        width = bounds.right - bounds.left;
        height = bounds.bottom - bounds.top;
        if (!width || !height)
        {
            IDWriteGlyphRunAnalysis_Release(analysis);
            return TRUE;
        }
        if (cleartype)
            width *= 3;
        pw = width + 2 * D2D_GLYPH_PAD;
        ph = height + 2 * D2D_GLYPH_PAD;

        alpha = calloc(height, width);
        padded = calloc(ph, pw);
        if (!alpha || !padded
                || FAILED(IDWriteGlyphRunAnalysis_CreateAlphaTexture(analysis, texture_type, &bounds,
                alpha, height * width)))
        {
            free(alpha);
            free(padded);
            IDWriteGlyphRunAnalysis_Release(analysis);
            return FALSE;
        }
        IDWriteGlyphRunAnalysis_Release(analysis);

        if (!d2d_glyph_atlas_alloc(atlas, pw, ph, &x, &y))
        {
            d2d_glyph_atlas_clear(atlas);
            if (!d2d_glyph_atlas_alloc(atlas, pw, ph, &x, &y))
            {
                free(alpha);
                free(padded);
                return FALSE;
            }
            slot = hash % D2D_GLYPH_ENTRIES;
        }
        if (atlas->count >= D2D_GLYPH_ENTRIES * 3 / 4)
        {
            d2d_glyph_atlas_clear(atlas);
            d2d_glyph_atlas_alloc(atlas, pw, ph, &x, &y);
            slot = hash % D2D_GLYPH_ENTRIES;
        }
        while (atlas->entries[slot].key)
            slot = (slot + 1) % D2D_GLYPH_ENTRIES;

        for (row = 0; row < height; ++row)
            memcpy(padded + (row + D2D_GLYPH_PAD) * pw + D2D_GLYPH_PAD, alpha + row * width, width);
        upload.left = x;
        upload.top = y;
        upload.right = x + pw;
        upload.bottom = y + ph;
        ID2D1Bitmap_CopyFromMemory(atlas->bitmap, &upload, padded, pw);
        free(alpha);
        free(padded);

        entry = &atlas->entries[slot];
        if (!(entry->key = malloc(key_size)))
            return FALSE;
        memcpy(entry->key, key_buffer, key_size);
        entry->key_size = key_size;
        entry->hash = hash;
        entry->face = glyph_run->fontFace;
        IDWriteFontFace_AddRef(entry->face);
        entry->x = x + D2D_GLYPH_PAD;
        entry->y = y + D2D_GLYPH_PAD;
        entry->bounds = bounds;
        ++atlas->count;
    }

    d2d_rect_set(&run_rect, (entry->bounds.left + shift_x) / scale_x, (entry->bounds.top + shift_y) / scale_y,
            (entry->bounds.right + shift_x) / scale_x, (entry->bounds.bottom + shift_y) / scale_y);

    opacity = unsafe_impl_from_ID2D1Brush((ID2D1Brush *)atlas->brush);
    opacity->transform._31 = run_rect.left - entry->x * 96.0f / (atlas->dpi_x * (cleartype ? 3.0f : 1.0f));
    opacity->transform._32 = run_rect.top - entry->y * 96.0f / atlas->dpi_y;

    saved = context->drawing_state.transform;
    context->drawing_state.transform = identity;
    if (!d2d_device_context_fill_rectangle(context, &run_rect, brush, opacity))
    {
        ID2D1RectangleGeometry *geometry;

        if (SUCCEEDED(ID2D1Factory_CreateRectangleGeometry(context->factory, &run_rect, &geometry)))
        {
            d2d_device_context_fill_geometry(context, unsafe_impl_from_ID2D1Geometry((ID2D1Geometry *)geometry),
                    brush, opacity);
            ID2D1RectangleGeometry_Release(geometry);
        }
    }
    context->drawing_state.transform = saved;
    return TRUE;
}

static void d2d_device_context_draw_glyph_run_bitmap(struct d2d_device_context *context,
        D2D1_POINT_2F baseline_origin, const DWRITE_GLYPH_RUN *glyph_run, ID2D1Brush *brush,
        DWRITE_RENDERING_MODE rendering_mode, DWRITE_MEASURING_MODE measuring_mode,
        DWRITE_TEXT_ANTIALIAS_MODE antialias_mode)
{
    ID2D1RectangleGeometry *geometry = NULL;
    ID2D1BitmapBrush *opacity_brush = NULL;
    D2D1_BITMAP_PROPERTIES bitmap_desc;
    ID2D1Bitmap *opacity_bitmap = NULL;
    IDWriteGlyphRunAnalysis *analysis;
    DWRITE_TEXTURE_TYPE texture_type;
    D2D1_BRUSH_PROPERTIES brush_desc;
    D2D1_MATRIX_3X2_F *transform, m;
    void *opacity_values = NULL;
    size_t opacity_values_size;
    D2D1_SIZE_U bitmap_size;
    float scale_x, scale_y;
    D2D1_RECT_F run_rect;
    UINT32 max_dimension;
    RECT bounds;
    HRESULT hr;

    if (d2d_device_context_draw_glyph_run_cached(context, baseline_origin, glyph_run,
            unsafe_impl_from_ID2D1Brush(brush), rendering_mode, measuring_mode, antialias_mode))
        return;

    hr = d2d_device_context_get_glyph_run_analysis(context, baseline_origin, glyph_run,
            rendering_mode, measuring_mode, antialias_mode, &texture_type, &analysis, 0.0f, 0.0f);
    if (FAILED(hr))
    {
        ERR("Failed to create glyph run analysis, hr %#lx.\n", hr);
        return;
    }

    if (FAILED(hr = IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(analysis, texture_type, &bounds)))
    {
        ERR("Failed to get alpha texture bounds, hr %#lx.\n", hr);
        goto done;
    }

    d2d_size_set(&bitmap_size, bounds.right - bounds.left, bounds.bottom - bounds.top);
    if (!bitmap_size.width || !bitmap_size.height)
    {
        /* Empty run, nothing to do. */
        goto done;
    }

    if (texture_type == DWRITE_TEXTURE_CLEARTYPE_3x1)
        bitmap_size.width *= 3;
    max_dimension = ID3D11Device1_GetFeatureLevel(context->d3d_device) >= D3D_FEATURE_LEVEL_11_0 ?
            D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION : D3D10_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    if (bitmap_size.width > max_dimension || bitmap_size.height > max_dimension)
    {
        /* FIXME: a run wider than a texture is drawn as outlines, not as bitmap pieces */
        d2d_device_context_draw_glyph_run_outline(context, baseline_origin, glyph_run, brush);
        goto done;
    }
    if (!(opacity_values = calloc(bitmap_size.height, bitmap_size.width)))
    {
        ERR("Failed to allocate opacity values.\n");
        goto done;
    }
    opacity_values_size = bitmap_size.height * bitmap_size.width;

    if (FAILED(hr = IDWriteGlyphRunAnalysis_CreateAlphaTexture(analysis,
            texture_type, &bounds, opacity_values, opacity_values_size)))
    {
        ERR("Failed to create alpha texture, hr %#lx.\n", hr);
        goto done;
    }

    bitmap_desc.pixelFormat.format = DXGI_FORMAT_A8_UNORM;
    bitmap_desc.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bitmap_desc.dpiX = context->desc.dpiX;
    if (texture_type == DWRITE_TEXTURE_CLEARTYPE_3x1)
        bitmap_desc.dpiX *= 3.0f;
    bitmap_desc.dpiY = context->desc.dpiY;
    if (FAILED(hr = d2d_device_context_CreateBitmap(&context->ID2D1DeviceContext6_iface,
            bitmap_size, opacity_values, bitmap_size.width, &bitmap_desc, &opacity_bitmap)))
    {
        ERR("Failed to create opacity bitmap, hr %#lx.\n", hr);
        goto done;
    }

    scale_x = context->desc.dpiX / 96.0f;
    scale_y = context->desc.dpiY / 96.0f;
    d2d_rect_set(&run_rect, bounds.left / scale_x, bounds.top / scale_y,
            bounds.right / scale_x, bounds.bottom / scale_y);

    brush_desc.opacity = 1.0f;
    brush_desc.transform._11 = 1.0f;
    brush_desc.transform._12 = 0.0f;
    brush_desc.transform._21 = 0.0f;
    brush_desc.transform._22 = 1.0f;
    brush_desc.transform._31 = run_rect.left;
    brush_desc.transform._32 = run_rect.top;
    if (FAILED(hr = d2d_device_context_CreateBitmapBrush(&context->ID2D1DeviceContext6_iface,
            opacity_bitmap, NULL, &brush_desc, &opacity_brush)))
    {
        ERR("Failed to create opacity bitmap brush, hr %#lx.\n", hr);
        goto done;
    }

    if (FAILED(hr = ID2D1Factory_CreateRectangleGeometry(context->factory, &run_rect, &geometry)))
    {
        ERR("Failed to create geometry, hr %#lx.\n", hr);
        goto done;
    }

    transform = &context->drawing_state.transform;
    m = *transform;
    *transform = identity;
    d2d_device_context_fill_geometry(context, unsafe_impl_from_ID2D1Geometry((ID2D1Geometry *)geometry),
            unsafe_impl_from_ID2D1Brush(brush), unsafe_impl_from_ID2D1Brush((ID2D1Brush *)opacity_brush));
    *transform = m;

done:
    if (geometry)
        ID2D1RectangleGeometry_Release(geometry);
    if (opacity_brush)
        ID2D1BitmapBrush_Release(opacity_brush);
    if (opacity_bitmap)
        ID2D1Bitmap_Release(opacity_bitmap);
    free(opacity_values);
    IDWriteGlyphRunAnalysis_Release(analysis);
}

static HRESULT d2d_device_context_get_text_rendering_mode(struct d2d_device_context *context,
        const DWRITE_GLYPH_RUN *glyph_run, DWRITE_MEASURING_MODE measuring_mode,
        DWRITE_TEXT_ANTIALIAS_MODE *antialias_mode, DWRITE_RENDERING_MODE *rendering_mode)
{
    IDWriteRenderingParams *rendering_params;
    HRESULT hr = S_OK;

    rendering_params = context->text_rendering_params ? context->text_rendering_params
            : context->default_text_rendering_params;

    *antialias_mode = DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE;
    *rendering_mode = IDWriteRenderingParams_GetRenderingMode(rendering_params);

    switch (context->drawing_state.textAntialiasMode)
    {
        case D2D1_TEXT_ANTIALIAS_MODE_ALIASED:
            if (*rendering_mode == DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL
                    || *rendering_mode == DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL_SYMMETRIC
                    || *rendering_mode == DWRITE_RENDERING_MODE_CLEARTYPE_GDI_NATURAL
                    || *rendering_mode == DWRITE_RENDERING_MODE_CLEARTYPE_GDI_CLASSIC)
            {
                return E_INVALIDARG;
            }
            break;

        case D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE:
            if (*rendering_mode == DWRITE_RENDERING_MODE_ALIASED
                    || *rendering_mode == DWRITE_RENDERING_MODE_OUTLINE)
            {
                return E_INVALIDARG;
            }
            break;

        case D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE:
            if (*rendering_mode == DWRITE_RENDERING_MODE_ALIASED)
                return E_INVALIDARG;
            break;

        default:
            break;
    }

    *rendering_mode = DWRITE_RENDERING_MODE_DEFAULT;
    switch (context->drawing_state.textAntialiasMode)
    {
        case D2D1_TEXT_ANTIALIAS_MODE_DEFAULT:
            if (IDWriteRenderingParams_GetClearTypeLevel(rendering_params) > 0.0f)
                *antialias_mode = DWRITE_TEXT_ANTIALIAS_MODE_CLEARTYPE;
            break;

        case D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE:
            *antialias_mode = DWRITE_TEXT_ANTIALIAS_MODE_CLEARTYPE;
            break;

        case D2D1_TEXT_ANTIALIAS_MODE_ALIASED:
            *rendering_mode = DWRITE_RENDERING_MODE_ALIASED;
            break;

        default:
            break;
    }

    if (*rendering_mode == DWRITE_RENDERING_MODE_DEFAULT)
    {
        const D2D1_MATRIX_3X2_F *world = &context->drawing_state.transform;
        DWRITE_MATRIX transform = { world->_11, world->_12, world->_21, world->_22, world->_31, world->_32 };
        DWRITE_GRID_FIT_MODE grid_fit_mode;
        IDWriteFontFace2 *face2;

        /* The world transform scales the em size the outline threshold is measured against. */
        if (SUCCEEDED(IDWriteFontFace_QueryInterface(glyph_run->fontFace, &IID_IDWriteFontFace2, (void **)&face2)))
        {
            hr = IDWriteFontFace2_GetRecommendedRenderingMode(face2, glyph_run->fontEmSize,
                    context->desc.dpiX, context->desc.dpiY, &transform, glyph_run->isSideways,
                    DWRITE_OUTLINE_THRESHOLD_ANTIALIASED, measuring_mode, rendering_params, rendering_mode,
                    &grid_fit_mode);
            IDWriteFontFace2_Release(face2);
        }
        else
        {
            hr = IDWriteFontFace_GetRecommendedRenderingMode(glyph_run->fontFace, glyph_run->fontEmSize,
                    max(context->desc.dpiX, context->desc.dpiY) / 96.0f,
                    measuring_mode, rendering_params, rendering_mode);
        }
        if (FAILED(hr))
        {
            ERR("Failed to get recommended rendering mode, hr %#lx.\n", hr);
            *rendering_mode = DWRITE_RENDERING_MODE_OUTLINE;
        }
    }

    return hr;
}

static void d2d_device_context_draw_glyph_run(struct d2d_device_context *context,
        D2D1_POINT_2F baseline_origin, const DWRITE_GLYPH_RUN *glyph_run,
        const DWRITE_GLYPH_RUN_DESCRIPTION *glyph_run_desc, ID2D1Brush *brush, DWRITE_MEASURING_MODE measuring_mode)
{
    DWRITE_TEXT_ANTIALIAS_MODE antialias_mode;
    DWRITE_RENDERING_MODE rendering_mode;
    HRESULT hr;

    if (FAILED(context->error.code))
        return;

    if (context->target.type == D2D_TARGET_UNKNOWN)
    {
        d2d_device_context_set_error(context, D2DERR_WRONG_STATE);
        return;
    }

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
    {
        d2d_command_list_draw_glyph_run(context->target.command_list, context, baseline_origin, glyph_run,
                glyph_run_desc, brush, measuring_mode);
        return;
    }

    if (FAILED(hr = d2d_device_context_get_text_rendering_mode(context, glyph_run, measuring_mode,
            &antialias_mode, &rendering_mode)))
    {
        d2d_device_context_set_error(context, hr);
        return;
    }

    if (rendering_mode == DWRITE_RENDERING_MODE_OUTLINE)
        d2d_device_context_draw_glyph_run_outline(context, baseline_origin, glyph_run, brush);
    else
        d2d_device_context_draw_glyph_run_bitmap(context, baseline_origin, glyph_run, brush,
                rendering_mode, measuring_mode, antialias_mode);
}

void STDMETHODCALLTYPE d2d_device_context_DrawGlyphRun(ID2D1DeviceContext6 *iface,
        D2D1_POINT_2F baseline_origin, const DWRITE_GLYPH_RUN *glyph_run, ID2D1Brush *brush,
        DWRITE_MEASURING_MODE measuring_mode)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, baseline_origin %s, glyph_run %p, brush %p, measuring_mode %#x.\n",
            iface, debug_d2d_point_2f(&baseline_origin), glyph_run, brush, measuring_mode);

    d2d_device_context_draw_glyph_run(context, baseline_origin, glyph_run, NULL, brush, measuring_mode);
}

HRESULT STDMETHODCALLTYPE d2d_device_context_GetGlyphRunWorldBounds(ID2D1DeviceContext6 *iface,
        D2D1_POINT_2F baseline_origin, const DWRITE_GLYPH_RUN *glyph_run,
        DWRITE_MEASURING_MODE measuring_mode, D2D1_RECT_F *bounds)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    DWRITE_TEXT_ANTIALIAS_MODE antialias_mode;
    DWRITE_RENDERING_MODE rendering_mode;
    IDWriteGlyphRunAnalysis *analysis;
    DWRITE_TEXTURE_TYPE texture_type;
    ID2D1PathGeometry *geometry;
    D2D1_MATRIX_3X2_F transform;
    float scale_x, scale_y;
    HRESULT hr;
    RECT rect;

    TRACE("iface %p, baseline_origin %s, glyph_run %p, measuring_mode %#x, bounds %p.\n",
            iface, debug_d2d_point_2f(&baseline_origin), glyph_run, measuring_mode, bounds);

    if (FAILED(hr = d2d_device_context_get_text_rendering_mode(context, glyph_run, measuring_mode,
            &antialias_mode, &rendering_mode)))
    {
        return hr;
    }

    if (rendering_mode == DWRITE_RENDERING_MODE_OUTLINE)
    {
        if (FAILED(hr = d2d_device_context_get_glyph_run_geometry(context, glyph_run, &geometry)))
            return hr;

        transform = context->drawing_state.transform;
        transform._31 += baseline_origin.x * transform._11 + baseline_origin.y * transform._21;
        transform._32 += baseline_origin.x * transform._12 + baseline_origin.y * transform._22;
        hr = ID2D1PathGeometry_GetBounds(geometry, &transform, bounds);
        ID2D1PathGeometry_Release(geometry);
    }
    else
    {
        if (FAILED(hr = d2d_device_context_get_glyph_run_analysis(context, baseline_origin,
                glyph_run, rendering_mode, measuring_mode, antialias_mode, &texture_type, &analysis, 0.0f, 0.0f)))
        {
            return hr;
        }

        if (SUCCEEDED(hr = IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(analysis, texture_type, &rect)))
        {
            scale_x = context->desc.dpiX / 96.0f;
            scale_y = context->desc.dpiY / 96.0f;
            d2d_rect_set(bounds, rect.left / scale_x, rect.top / scale_y,
                    rect.right / scale_x, rect.bottom / scale_y);
        }

        IDWriteGlyphRunAnalysis_Release(analysis);
    }

    return hr;
}

void STDMETHODCALLTYPE d2d_device_context_ID2D1DeviceContext_DrawGlyphRun(ID2D1DeviceContext6 *iface,
        D2D1_POINT_2F baseline_origin, const DWRITE_GLYPH_RUN *glyph_run,
        const DWRITE_GLYPH_RUN_DESCRIPTION *glyph_run_desc, ID2D1Brush *brush, DWRITE_MEASURING_MODE measuring_mode)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, baseline_origin %s, glyph_run %p, glyph_run_desc %p, brush %p, measuring_mode %#x.\n",
            iface, debug_d2d_point_2f(&baseline_origin), glyph_run, glyph_run_desc, brush, measuring_mode);

    d2d_device_context_draw_glyph_run(context, baseline_origin, glyph_run, glyph_run_desc, brush, measuring_mode);
}

HRESULT STDMETHODCALLTYPE d2d_device_context_CreateSvgGlyphStyle(ID2D1DeviceContext6 *iface,
        ID2D1SvgGlyphStyle **svg_glyph_style)
{
    FIXME("iface %p, svg_glyph_style %p stub!\n", iface, svg_glyph_style);

    return E_NOTIMPL;
}

void STDMETHODCALLTYPE d2d_device_context_ID2D1DeviceContext4_DrawText(ID2D1DeviceContext6 *iface,
        const WCHAR *string, UINT32 string_length, IDWriteTextFormat *text_format, const D2D1_RECT_F *layout_rect,
        ID2D1Brush *default_fill_brush, ID2D1SvgGlyphStyle *svg_glyph_style, UINT32 color_palette_index,
        D2D1_DRAW_TEXT_OPTIONS options, DWRITE_MEASURING_MODE measuring_mode)
{
    FIXME("iface %p, string %s, string_length %u, text_format %p, layout_rect %s, default_fill_brush %p,"
            "svg_glyph_style %p, color_palette_index %u, options %#x, measuring_mode %u stub!\n",
            iface, debugstr_wn(string, string_length), string_length, text_format, debug_d2d_rect_f(layout_rect),
            default_fill_brush, svg_glyph_style, color_palette_index, options, measuring_mode);
}

void STDMETHODCALLTYPE d2d_device_context_ID2D1DeviceContext4_DrawTextLayout(ID2D1DeviceContext6 *iface,
        D2D1_POINT_2F origin, IDWriteTextLayout *text_layout, ID2D1Brush *default_fill_brush,
        ID2D1SvgGlyphStyle *svg_glyph_style, UINT32 color_palette_index, D2D1_DRAW_TEXT_OPTIONS options)
{
    FIXME("iface %p, origin %s, text_layout %p, default_fill_brush %p, svg_glyph_style %p, color_palette_index %u,"
            "options %#x stub!\n", iface, debug_d2d_point_2f(&origin), text_layout, default_fill_brush,
            svg_glyph_style, color_palette_index, options);
}

void STDMETHODCALLTYPE d2d_device_context_DrawColorBitmapGlyphRun(ID2D1DeviceContext6 *iface,
        DWRITE_GLYPH_IMAGE_FORMATS glyph_image_format, D2D1_POINT_2F baseline_origin, const DWRITE_GLYPH_RUN *glyph_run,
        DWRITE_MEASURING_MODE measuring_mode, D2D1_COLOR_BITMAP_GLYPH_SNAP_OPTION bitmap_snap_option)
{
    FIXME("iface %p, glyph_image_format %#x, baseline_origin %s, glyph_run %p, measuring_mode %u, bitmap_snap_option %#x stub!\n",
            iface, glyph_image_format, debug_d2d_point_2f(&baseline_origin), glyph_run, measuring_mode, bitmap_snap_option);
}

void STDMETHODCALLTYPE d2d_device_context_DrawSvgGlyphRun(ID2D1DeviceContext6 *iface,
        D2D1_POINT_2F baseline_origin, const DWRITE_GLYPH_RUN *glyph_run, ID2D1Brush *default_fill_brush,
        ID2D1SvgGlyphStyle *svg_glyph_style, UINT32 color_palette_index, DWRITE_MEASURING_MODE measuring_mode)
{
    FIXME("iface %p, baseline_origin %s, glyph_run %p, default_fill_brush %p, svg_glyph_style %p,"
            "color_palette_index %u, measuring_mode %u stub!\n", iface, debug_d2d_point_2f(&baseline_origin),
            glyph_run, default_fill_brush, svg_glyph_style, color_palette_index, measuring_mode);
}

HRESULT STDMETHODCALLTYPE d2d_device_context_GetColorBitmapGlyphImage(ID2D1DeviceContext6 *iface,
        DWRITE_GLYPH_IMAGE_FORMATS glyph_image_format, D2D1_POINT_2F glyph_origin, IDWriteFontFace *font_face,
        FLOAT font_em_size, UINT16 glyph_index, BOOL is_sideways, const D2D1_MATRIX_3X2_F *world_transform,
        FLOAT dpi_x, FLOAT dpi_y, D2D1_MATRIX_3X2_F *glyph_transform, ID2D1Image **glyph_image)
{
    FIXME("iface %p, glyph_image_format %u, glyph_origin %s, font_face %p, font_em_size %f, glyph_index %u,"
            "is_sideways %d, world_transform %p, dpi_x %f, dpi_y %f, glyph_transform %p, glyph_image %p stub!\n",
            iface, glyph_image_format, debug_d2d_point_2f(&glyph_origin), font_face, font_em_size, glyph_index,
            is_sideways, world_transform, dpi_x, dpi_y, glyph_transform, glyph_image);

    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE d2d_device_context_GetSvgGlyphImage(ID2D1DeviceContext6 *iface,
        D2D1_POINT_2F glyph_origin, IDWriteFontFace *font_face, FLOAT font_em_size, UINT16 glyph_index,
        BOOL is_sideways, const D2D1_MATRIX_3X2_F *world_transform, ID2D1Brush *default_fill_brush,
        ID2D1SvgGlyphStyle *svg_glyph_style, UINT32 color_palette_index, D2D1_MATRIX_3X2_F *glyph_transform,
        ID2D1CommandList **glyph_image)
{
    FIXME("iface %p, glyph_origin %s, font_face %p, font_em_size %f, glyph_index %u, is_sideways %d,"
            "world_transform %p, default_fill_brush %p, svg_glyph_style %p, color_palette_index %u,"
            "glyph_transform %p, glyph_image %p stub!\n", iface, debug_d2d_point_2f(&glyph_origin),
            font_face, font_em_size, glyph_index, is_sideways, world_transform, default_fill_brush,
            svg_glyph_style, color_palette_index, glyph_transform, glyph_image);

    return E_NOTIMPL;
}

static inline struct d2d_device_context *impl_from_IDWriteTextRenderer(IDWriteTextRenderer *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_device_context, IDWriteTextRenderer_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_text_renderer_QueryInterface(IDWriteTextRenderer *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IDWriteTextRenderer)
            || IsEqualGUID(iid, &IID_IDWritePixelSnapping)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        IDWriteTextRenderer_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_text_renderer_AddRef(IDWriteTextRenderer *iface)
{
    struct d2d_device_context *context = impl_from_IDWriteTextRenderer(iface);

    TRACE("iface %p.\n", iface);

    return d2d_device_context_AddRef(&context->ID2D1DeviceContext6_iface);
}

static ULONG STDMETHODCALLTYPE d2d_text_renderer_Release(IDWriteTextRenderer *iface)
{
    struct d2d_device_context *context = impl_from_IDWriteTextRenderer(iface);

    TRACE("iface %p.\n", iface);

    return d2d_device_context_Release(&context->ID2D1DeviceContext6_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_text_renderer_IsPixelSnappingDisabled(IDWriteTextRenderer *iface,
        void *ctx, BOOL *disabled)
{
    struct d2d_draw_text_layout_ctx *context = ctx;

    TRACE("iface %p, ctx %p, disabled %p.\n", iface, ctx, disabled);

    *disabled = context->options & D2D1_DRAW_TEXT_OPTIONS_NO_SNAP;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_text_renderer_GetCurrentTransform(IDWriteTextRenderer *iface,
        void *ctx, DWRITE_MATRIX *transform)
{
    struct d2d_device_context *context = impl_from_IDWriteTextRenderer(iface);

    TRACE("iface %p, ctx %p, transform %p.\n", iface, ctx, transform);

    d2d_device_context_GetTransform(&context->ID2D1DeviceContext6_iface, (D2D1_MATRIX_3X2_F *)transform);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_text_renderer_GetPixelsPerDip(IDWriteTextRenderer *iface, void *ctx, float *ppd)
{
    struct d2d_device_context *render_target = impl_from_IDWriteTextRenderer(iface);

    TRACE("iface %p, ctx %p, ppd %p.\n", iface, ctx, ppd);

    *ppd = render_target->desc.dpiY / 96.0f;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_text_renderer_DrawGlyphRun(IDWriteTextRenderer *iface, void *ctx,
        float baseline_origin_x, float baseline_origin_y, DWRITE_MEASURING_MODE measuring_mode,
        const DWRITE_GLYPH_RUN *glyph_run, const DWRITE_GLYPH_RUN_DESCRIPTION *glyph_run_desc, IUnknown *effect)
{
    struct d2d_device_context *render_target = impl_from_IDWriteTextRenderer(iface);
    D2D1_POINT_2F baseline_origin = {baseline_origin_x, baseline_origin_y};
    struct d2d_draw_text_layout_ctx *context = ctx;
    BOOL color_font = FALSE;
    ID2D1Brush *brush;

    TRACE("iface %p, ctx %p, baseline_origin_x %.8e, baseline_origin_y %.8e, "
            "measuring_mode %#x, glyph_run %p, glyph_run_desc %p, effect %p.\n",
            iface, ctx, baseline_origin_x, baseline_origin_y,
            measuring_mode, glyph_run, glyph_run_desc, effect);

    if (context->options & ~(D2D1_DRAW_TEXT_OPTIONS_NO_SNAP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT))
        FIXME("Ignoring options %#x.\n", context->options);

    brush = d2d_draw_get_text_brush(context, effect);

    TRACE("%s\n", debugstr_wn(glyph_run_desc->string, glyph_run_desc->stringLength));

    if (context->options & D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT)
    {
        IDWriteFontFace2 *fontface;

        if (SUCCEEDED(IDWriteFontFace_QueryInterface(glyph_run->fontFace,
                &IID_IDWriteFontFace2, (void **)&fontface)))
        {
            color_font = IDWriteFontFace2_IsColorFont(fontface);
            IDWriteFontFace2_Release(fontface);
        }
    }

    if (color_font)
    {
        IDWriteColorGlyphRunEnumerator *layers;
        IDWriteFactory2 *dwrite_factory;
        HRESULT hr;

        if (FAILED(hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_IDWriteFactory2,
                (IUnknown **)&dwrite_factory)))
        {
            ERR("Failed to create dwrite factory, hr %#lx.\n", hr);
            ID2D1Brush_Release(brush);
            return hr;
        }

        hr = IDWriteFactory2_TranslateColorGlyphRun(dwrite_factory, baseline_origin_x, baseline_origin_y,
                glyph_run, glyph_run_desc, measuring_mode, (DWRITE_MATRIX *)&render_target->drawing_state.transform, 0, &layers);
        IDWriteFactory2_Release(dwrite_factory);
        if (FAILED(hr))
        {
            ERR("Failed to create colour glyph run enumerator, hr %#lx.\n", hr);
            ID2D1Brush_Release(brush);
            return hr;
        }

        for (;;)
        {
            const DWRITE_COLOR_GLYPH_RUN *color_run;
            ID2D1Brush *color_brush;
            D2D1_POINT_2F origin;
            BOOL has_run = FALSE;

            if (FAILED(hr = IDWriteColorGlyphRunEnumerator_MoveNext(layers, &has_run)))
            {
                ERR("Failed to switch colour glyph layer, hr %#lx.\n", hr);
                break;
            }

            if (!has_run)
                break;

            if (FAILED(hr = IDWriteColorGlyphRunEnumerator_GetCurrentRun(layers, &color_run)))
            {
                ERR("Failed to get current colour run, hr %#lx.\n", hr);
                break;
            }

            if (color_run->paletteIndex == 0xffff)
                color_brush = brush;
            else
            {
                if (FAILED(hr = d2d_device_context_CreateSolidColorBrush(&render_target->ID2D1DeviceContext6_iface,
                        &color_run->runColor, NULL, (ID2D1SolidColorBrush **)&color_brush)))
                {
                    ERR("Failed to create solid colour brush, hr %#lx.\n", hr);
                    break;
                }
            }

            origin.x = color_run->baselineOriginX;
            origin.y = color_run->baselineOriginY;
            d2d_device_context_draw_glyph_run(render_target, origin, &color_run->glyphRun,
                    color_run->glyphRunDescription, color_brush, measuring_mode);

            if (color_brush != brush)
                ID2D1Brush_Release(color_brush);
        }

        IDWriteColorGlyphRunEnumerator_Release(layers);
    }
    else
        d2d_device_context_draw_glyph_run(render_target, baseline_origin, glyph_run, glyph_run_desc,
                brush, measuring_mode);

    ID2D1Brush_Release(brush);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_text_renderer_DrawUnderline(IDWriteTextRenderer *iface, void *ctx,
        float baseline_origin_x, float baseline_origin_y, const DWRITE_UNDERLINE *underline, IUnknown *effect)
{
    struct d2d_device_context *render_target = impl_from_IDWriteTextRenderer(iface);
    const D2D1_MATRIX_3X2_F *m = &render_target->drawing_state.transform;
    struct d2d_draw_text_layout_ctx *context = ctx;
    D2D1_ANTIALIAS_MODE prev_antialias_mode;
    D2D1_POINT_2F start, end;
    ID2D1Brush *brush;
    float thickness;

    TRACE("iface %p, ctx %p, baseline_origin_x %.8e, baseline_origin_y %.8e, underline %p, effect %p\n",
            iface, ctx, baseline_origin_x, baseline_origin_y, underline, effect);

    /* minimal thickness in DIPs that will result in at least 1 pixel thick line */
    thickness = max(96.0f / (render_target->desc.dpiY * sqrtf(m->_21 * m->_21 + m->_22 * m->_22)),
            underline->thickness);

    brush = d2d_draw_get_text_brush(context, effect);

    start.x = baseline_origin_x;
    start.y = baseline_origin_y + underline->offset + thickness / 2.0f;
    end.x = start.x + underline->width;
    end.y = start.y;
    prev_antialias_mode = d2d_device_context_set_aa_mode_from_text_aa_mode(render_target);
    d2d_device_context_DrawLine(&render_target->ID2D1DeviceContext6_iface, start, end, brush, thickness, NULL);
    render_target->drawing_state.antialiasMode = prev_antialias_mode;

    ID2D1Brush_Release(brush);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_text_renderer_DrawStrikethrough(IDWriteTextRenderer *iface, void *ctx,
        float baseline_origin_x, float baseline_origin_y, const DWRITE_STRIKETHROUGH *strikethrough, IUnknown *effect)
{
    struct d2d_device_context *render_target = impl_from_IDWriteTextRenderer(iface);
    const D2D1_MATRIX_3X2_F *m = &render_target->drawing_state.transform;
    struct d2d_draw_text_layout_ctx *context = ctx;
    D2D1_ANTIALIAS_MODE prev_antialias_mode;
    D2D1_POINT_2F start, end;
    ID2D1Brush *brush;
    float thickness;

    TRACE("iface %p, ctx %p, baseline_origin_x %.8e, baseline_origin_y %.8e, strikethrough %p, effect %p.\n",
            iface, ctx, baseline_origin_x, baseline_origin_y, strikethrough, effect);

    /* minimal thickness in DIPs that will result in at least 1 pixel thick line */
    thickness = max(96.0f / (render_target->desc.dpiY * sqrtf(m->_21 * m->_21 + m->_22 * m->_22)),
            strikethrough->thickness);

    brush = d2d_draw_get_text_brush(context, effect);

    start.x = baseline_origin_x;
    start.y = baseline_origin_y + strikethrough->offset + thickness / 2.0f;
    end.x = start.x + strikethrough->width;
    end.y = start.y;
    prev_antialias_mode = d2d_device_context_set_aa_mode_from_text_aa_mode(render_target);
    d2d_device_context_DrawLine(&render_target->ID2D1DeviceContext6_iface, start, end, brush, thickness, NULL);
    render_target->drawing_state.antialiasMode = prev_antialias_mode;

    ID2D1Brush_Release(brush);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_text_renderer_DrawInlineObject(IDWriteTextRenderer *iface, void *ctx,
        float origin_x, float origin_y, IDWriteInlineObject *object, BOOL is_sideways, BOOL is_rtl, IUnknown *effect)
{
    struct d2d_draw_text_layout_ctx *context = ctx;
    ID2D1Brush *brush;
    HRESULT hr;

    TRACE("iface %p, ctx %p, origin_x %.8e, origin_y %.8e, object %p, is_sideways %#x, is_rtl %#x, effect %p.\n",
            iface, ctx, origin_x, origin_y, object, is_sideways, is_rtl, effect);

    /* Inline objects may not pass effects all the way down, when using layout object internally for example.
       This is how default trimming sign object in DirectWrite works - it does not use effect passed to Draw(),
       and resulting DrawGlyphRun() is always called with NULL effect, however original effect is used and correct
       brush is selected at Direct2D level. */
    brush = context->brush;
    context->brush = d2d_draw_get_text_brush(context, effect);

    hr = IDWriteInlineObject_Draw(object, ctx, iface, origin_x, origin_y, is_sideways, is_rtl, effect);

    ID2D1Brush_Release(context->brush);
    context->brush = brush;

    return hr;
}

const struct IDWriteTextRendererVtbl d2d_text_renderer_vtbl =
{
    d2d_text_renderer_QueryInterface,
    d2d_text_renderer_AddRef,
    d2d_text_renderer_Release,
    d2d_text_renderer_IsPixelSnappingDisabled,
    d2d_text_renderer_GetCurrentTransform,
    d2d_text_renderer_GetPixelsPerDip,
    d2d_text_renderer_DrawGlyphRun,
    d2d_text_renderer_DrawUnderline,
    d2d_text_renderer_DrawStrikethrough,
    d2d_text_renderer_DrawInlineObject,
};
