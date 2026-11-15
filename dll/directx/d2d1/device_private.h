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

#ifndef D2D1_DEVICE_PRIVATE_H
#define D2D1_DEVICE_PRIVATE_H

#include "d2d1_private.h"

static const D2D1_MATRIX_3X2_F identity =
{{{
    1.0f, 0.0f,
    0.0f, 1.0f,
    0.0f, 0.0f,
}}};

static inline void d2d_rect_intersect(D2D1_RECT_F *dst, const D2D1_RECT_F *src)
{
    if (src->left > dst->left)
        dst->left = src->left;
    if (src->top > dst->top)
        dst->top = src->top;
    if (src->right < dst->right)
        dst->right = src->right;
    if (src->bottom < dst->bottom)
        dst->bottom = src->bottom;
}

static inline void d2d_rect_set(D2D1_RECT_F *dst, float left, float top, float right, float bottom)
{
    dst->left = left;
    dst->top = top;
    dst->right = right;
    dst->bottom = bottom;
}

static inline struct d2d_device_context *impl_from_ID2D1DeviceContext(ID2D1DeviceContext6 *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_device_context, ID2D1DeviceContext6_iface);
}

BOOL d2d_clip_stack_init(struct d2d_clip_stack *stack);
void d2d_device_context_state_out(struct d2d_device_context *context);
void d2d_device_context_set_error(struct d2d_device_context *context, HRESULT code);
ULONG STDMETHODCALLTYPE d2d_device_context_AddRef(ID2D1DeviceContext6 *iface);
ULONG STDMETHODCALLTYPE d2d_device_context_Release(ID2D1DeviceContext6 *iface);
HRESULT STDMETHODCALLTYPE d2d_device_context_CreateBitmap(ID2D1DeviceContext6 *iface,
        D2D1_SIZE_U size, const void *src_data, UINT32 pitch, const D2D1_BITMAP_PROPERTIES *desc, ID2D1Bitmap **bitmap);
HRESULT STDMETHODCALLTYPE d2d_device_context_CreateBitmapBrush(ID2D1DeviceContext6 *iface,
        ID2D1Bitmap *bitmap, const D2D1_BITMAP_BRUSH_PROPERTIES *bitmap_brush_desc,
        const D2D1_BRUSH_PROPERTIES *brush_desc, ID2D1BitmapBrush **brush);
HRESULT STDMETHODCALLTYPE d2d_device_context_CreateSolidColorBrush(ID2D1DeviceContext6 *iface,
        const D2D1_COLOR_F *color, const D2D1_BRUSH_PROPERTIES *desc, ID2D1SolidColorBrush **brush);
void STDMETHODCALLTYPE d2d_device_context_DrawLine(ID2D1DeviceContext6 *iface,
        D2D1_POINT_2F p0, D2D1_POINT_2F p1, ID2D1Brush *brush, float stroke_width, ID2D1StrokeStyle *stroke_style);
void STDMETHODCALLTYPE d2d_device_context_DrawRectangle(ID2D1DeviceContext6 *iface,
        const D2D1_RECT_F *rect, ID2D1Brush *brush, float stroke_width, ID2D1StrokeStyle *stroke_style);
BOOL d2d_device_context_fill_rectangle(struct d2d_device_context *context, const D2D1_RECT_F *rect,
        struct d2d_brush *brush, struct d2d_brush *opacity_brush);
void STDMETHODCALLTYPE d2d_device_context_FillRectangle(ID2D1DeviceContext6 *iface,
        const D2D1_RECT_F *rect, ID2D1Brush *brush);
void STDMETHODCALLTYPE d2d_device_context_DrawRoundedRectangle(ID2D1DeviceContext6 *iface,
        const D2D1_ROUNDED_RECT *rect, ID2D1Brush *brush, float stroke_width, ID2D1StrokeStyle *stroke_style);
void STDMETHODCALLTYPE d2d_device_context_FillRoundedRectangle(ID2D1DeviceContext6 *iface,
        const D2D1_ROUNDED_RECT *rect, ID2D1Brush *brush);
void STDMETHODCALLTYPE d2d_device_context_DrawEllipse(ID2D1DeviceContext6 *iface,
        const D2D1_ELLIPSE *ellipse, ID2D1Brush *brush, float stroke_width, ID2D1StrokeStyle *stroke_style);
void STDMETHODCALLTYPE d2d_device_context_FillEllipse(ID2D1DeviceContext6 *iface,
        const D2D1_ELLIPSE *ellipse, ID2D1Brush *brush);
void STDMETHODCALLTYPE d2d_device_context_DrawGeometry(ID2D1DeviceContext6 *iface,
        ID2D1Geometry *geometry, ID2D1Brush *brush, float stroke_width, ID2D1StrokeStyle *stroke_style);
void d2d_device_context_release_mask_ring(struct d2d_device_context *context);
void d2d_device_context_fill_geometry(struct d2d_device_context *render_target,
        const struct d2d_geometry *geometry, struct d2d_brush *brush, struct d2d_brush *opacity_brush);
void STDMETHODCALLTYPE d2d_device_context_FillGeometry(ID2D1DeviceContext6 *iface,
        ID2D1Geometry *geometry, ID2D1Brush *brush, ID2D1Brush *opacity_brush);
void STDMETHODCALLTYPE d2d_device_context_FillMesh(ID2D1DeviceContext6 *iface,
        ID2D1Mesh *mesh, ID2D1Brush *brush);
void STDMETHODCALLTYPE d2d_device_context_FillOpacityMask(ID2D1DeviceContext6 *iface,
        ID2D1Bitmap *mask, ID2D1Brush *brush, D2D1_OPACITY_MASK_CONTENT content,
        const D2D1_RECT_F *dst_rect, const D2D1_RECT_F *src_rect);
void STDMETHODCALLTYPE d2d_device_context_DrawBitmap(ID2D1DeviceContext6 *iface,
        ID2D1Bitmap *bitmap, const D2D1_RECT_F *dst_rect, float opacity,
        D2D1_BITMAP_INTERPOLATION_MODE interpolation_mode, const D2D1_RECT_F *src_rect);
void STDMETHODCALLTYPE d2d_device_context_DrawText(ID2D1DeviceContext6 *iface,
        const WCHAR *string, UINT32 string_len, IDWriteTextFormat *text_format, const D2D1_RECT_F *layout_rect,
        ID2D1Brush *brush, D2D1_DRAW_TEXT_OPTIONS options, DWRITE_MEASURING_MODE measuring_mode);
void STDMETHODCALLTYPE d2d_device_context_DrawTextLayout(ID2D1DeviceContext6 *iface,
        D2D1_POINT_2F origin, IDWriteTextLayout *layout, ID2D1Brush *brush, D2D1_DRAW_TEXT_OPTIONS options);
void d2d_glyph_atlas_destroy(struct d2d_glyph_atlas *atlas);
void STDMETHODCALLTYPE d2d_device_context_DrawGlyphRun(ID2D1DeviceContext6 *iface,
        D2D1_POINT_2F baseline_origin, const DWRITE_GLYPH_RUN *glyph_run, ID2D1Brush *brush,
        DWRITE_MEASURING_MODE measuring_mode);
void STDMETHODCALLTYPE d2d_device_context_GetTransform(ID2D1DeviceContext6 *iface,
        D2D1_MATRIX_3X2_F *transform);
void STDMETHODCALLTYPE d2d_device_context_Clear(ID2D1DeviceContext6 *iface, const D2D1_COLOR_F *colour);
HRESULT STDMETHODCALLTYPE d2d_device_context_GetGlyphRunWorldBounds(ID2D1DeviceContext6 *iface,
        D2D1_POINT_2F baseline_origin, const DWRITE_GLYPH_RUN *glyph_run,
        DWRITE_MEASURING_MODE measuring_mode, D2D1_RECT_F *bounds);
void d2d_device_context_release_blend_states(struct d2d_device_context *context);
HRESULT d2d_device_context_create_blend_states(struct d2d_device_context *context);
void STDMETHODCALLTYPE d2d_device_context_ID2D1DeviceContext_DrawGlyphRun(ID2D1DeviceContext6 *iface,
        D2D1_POINT_2F baseline_origin, const DWRITE_GLYPH_RUN *glyph_run,
        const DWRITE_GLYPH_RUN_DESCRIPTION *glyph_run_desc, ID2D1Brush *brush, DWRITE_MEASURING_MODE measuring_mode);
void STDMETHODCALLTYPE d2d_device_context_DrawImage(ID2D1DeviceContext6 *iface, ID2D1Image *image,
        const D2D1_POINT_2F *target_offset, const D2D1_RECT_F *image_rect, D2D1_INTERPOLATION_MODE interpolation_mode,
        D2D1_COMPOSITE_MODE composite_mode);
void STDMETHODCALLTYPE d2d_device_context_ID2D1DeviceContext_DrawBitmap(ID2D1DeviceContext6 *iface,
        ID2D1Bitmap *bitmap, const D2D1_RECT_F *dst_rect, float opacity, D2D1_INTERPOLATION_MODE interpolation_mode,
        const D2D1_RECT_F *src_rect, const D2D1_MATRIX_4X4_F *perspective_transform);
void STDMETHODCALLTYPE d2d_device_context_ID2D1DeviceContext_FillOpacityMask(ID2D1DeviceContext6 *iface,
        ID2D1Bitmap *mask, ID2D1Brush *brush, const D2D1_RECT_F *dst_rect, const D2D1_RECT_F *src_rect);
void STDMETHODCALLTYPE d2d_device_context_DrawGeometryRealization(ID2D1DeviceContext6 *iface,
        ID2D1GeometryRealization *realization, ID2D1Brush *brush);
HRESULT STDMETHODCALLTYPE d2d_device_context_CreateSvgGlyphStyle(ID2D1DeviceContext6 *iface,
        ID2D1SvgGlyphStyle **svg_glyph_style);
void STDMETHODCALLTYPE d2d_device_context_ID2D1DeviceContext4_DrawText(ID2D1DeviceContext6 *iface,
        const WCHAR *string, UINT32 string_length, IDWriteTextFormat *text_format, const D2D1_RECT_F *layout_rect,
        ID2D1Brush *default_fill_brush, ID2D1SvgGlyphStyle *svg_glyph_style, UINT32 color_palette_index,
        D2D1_DRAW_TEXT_OPTIONS options, DWRITE_MEASURING_MODE measuring_mode);
void STDMETHODCALLTYPE d2d_device_context_ID2D1DeviceContext4_DrawTextLayout(ID2D1DeviceContext6 *iface,
        D2D1_POINT_2F origin, IDWriteTextLayout *text_layout, ID2D1Brush *default_fill_brush,
        ID2D1SvgGlyphStyle *svg_glyph_style, UINT32 color_palette_index, D2D1_DRAW_TEXT_OPTIONS options);
void STDMETHODCALLTYPE d2d_device_context_DrawColorBitmapGlyphRun(ID2D1DeviceContext6 *iface,
        DWRITE_GLYPH_IMAGE_FORMATS glyph_image_format, D2D1_POINT_2F baseline_origin, const DWRITE_GLYPH_RUN *glyph_run,
        DWRITE_MEASURING_MODE measuring_mode, D2D1_COLOR_BITMAP_GLYPH_SNAP_OPTION bitmap_snap_option);
void STDMETHODCALLTYPE d2d_device_context_DrawSvgGlyphRun(ID2D1DeviceContext6 *iface,
        D2D1_POINT_2F baseline_origin, const DWRITE_GLYPH_RUN *glyph_run, ID2D1Brush *default_fill_brush,
        ID2D1SvgGlyphStyle *svg_glyph_style, UINT32 color_palette_index, DWRITE_MEASURING_MODE measuring_mode);
HRESULT STDMETHODCALLTYPE d2d_device_context_GetColorBitmapGlyphImage(ID2D1DeviceContext6 *iface,
        DWRITE_GLYPH_IMAGE_FORMATS glyph_image_format, D2D1_POINT_2F glyph_origin, IDWriteFontFace *font_face,
        FLOAT font_em_size, UINT16 glyph_index, BOOL is_sideways, const D2D1_MATRIX_3X2_F *world_transform,
        FLOAT dpi_x, FLOAT dpi_y, D2D1_MATRIX_3X2_F *glyph_transform, ID2D1Image **glyph_image);
HRESULT STDMETHODCALLTYPE d2d_device_context_GetSvgGlyphImage(ID2D1DeviceContext6 *iface,
        D2D1_POINT_2F glyph_origin, IDWriteFontFace *font_face, FLOAT font_em_size, UINT16 glyph_index,
        BOOL is_sideways, const D2D1_MATRIX_3X2_F *world_transform, ID2D1Brush *default_fill_brush,
        ID2D1SvgGlyphStyle *svg_glyph_style, UINT32 color_palette_index, D2D1_MATRIX_3X2_F *glyph_transform,
        ID2D1CommandList **glyph_image);
HRESULT d2d_device_context_init(struct d2d_device_context *render_target,
        struct d2d_device *device, IUnknown *outer_unknown, const struct d2d_device_context_ops *ops);
void d2d_device_indexed_objects_clear(struct d2d_indexed_objects *objects);
HRESULT d2d_shader_set_create(struct d2d_shader_set **out);
void d2d_shader_set_release(struct d2d_shader_set *set);

extern const struct IUnknownVtbl d2d_device_context_inner_unknown_vtbl;
extern const struct ID2D1DeviceContext6Vtbl d2d_device_context_vtbl;
extern const struct ID2D1GdiInteropRenderTargetVtbl d2d_gdi_interop_render_target_vtbl;
extern const struct IDWriteTextRendererVtbl d2d_text_renderer_vtbl;

#endif
