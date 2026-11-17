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

#ifndef LIBERNT_D2D1_RESOURCES_H
#define LIBERNT_D2D1_RESOURCES_H

#include "d2d1_common.h"

struct d2d_wic_render_target
{
    IUnknown IUnknown_iface;
    LONG refcount;

    IDXGISurface *dxgi_surface;
    ID2D1RenderTarget *dxgi_target;
    IUnknown *dxgi_inner;
    ID3D10Texture2D *readback_texture;
    IWICBitmap *bitmap;

    unsigned int width;
    unsigned int height;
    unsigned int bpp;
};

HRESULT d2d_wic_render_target_init(struct d2d_wic_render_target *render_target, ID2D1Factory1 *factory,
        ID3D10Device1 *d3d_device, IWICBitmap *bitmap, const D2D1_RENDER_TARGET_PROPERTIES *desc);

struct d2d_dc_render_target
{
    ID2D1DCRenderTarget ID2D1DCRenderTarget_iface;
    LONG refcount;

    IDXGISurface1 *dxgi_surface;
    ID3D10Device1 *d3d_device;
    ID2D1RenderTarget *dxgi_target;
    IUnknown *dxgi_inner;

    RECT dst_rect;
    HDC hdc;

    D2D1_RENDER_TARGET_PROPERTIES desc;
};

HRESULT d2d_dc_render_target_init(struct d2d_dc_render_target *render_target, ID2D1Factory1 *factory,
        ID3D10Device1 *d3d_device, const D2D1_RENDER_TARGET_PROPERTIES *desc);

struct d2d_hwnd_render_target
{
    ID2D1HwndRenderTarget ID2D1HwndRenderTarget_iface;
    LONG refcount;

    ID2D1RenderTarget *dxgi_target;
    IUnknown *dxgi_inner;
    IDXGISwapChain *swapchain;
    UINT sync_interval;
    HWND hwnd;

    D2D1_RENDER_TARGET_PROPERTIES desc;
};

HRESULT d2d_hwnd_render_target_init(struct d2d_hwnd_render_target *render_target, ID2D1Factory1 *factory,
        ID3D10Device1 *d3d_device, const D2D1_RENDER_TARGET_PROPERTIES *desc,
        const D2D1_HWND_RENDER_TARGET_PROPERTIES *hwnd_desc);

struct d2d_bitmap_render_target
{
    ID2D1BitmapRenderTarget ID2D1BitmapRenderTarget_iface;
    LONG refcount;

    ID2D1RenderTarget *dxgi_target;
    IUnknown *dxgi_inner;
    ID2D1Bitmap *bitmap;
};

HRESULT d2d_bitmap_render_target_init(struct d2d_bitmap_render_target *render_target,
        const struct d2d_device_context *parent_target, const D2D1_SIZE_F *size,
        const D2D1_SIZE_U *pixel_size, const D2D1_PIXEL_FORMAT *format,
        D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS options);

struct d2d_gradient
{
    ID2D1GradientStopCollection ID2D1GradientStopCollection_iface;
    LONG refcount;

    ID2D1Factory *factory;
    ID3D11ShaderResourceView *view;
    D2D1_GRADIENT_STOP *stops;
    UINT32 stop_count;
    D2D1_GAMMA gamma;
    D2D1_EXTEND_MODE extend_mode;
};

HRESULT d2d_gradient_create(ID2D1Factory *factory, ID3D11Device1 *device, const D2D1_GRADIENT_STOP *stops,
        UINT32 stop_count, D2D1_GAMMA gamma, D2D1_EXTEND_MODE extend_mode,
        struct d2d_gradient **gradient);

struct d2d_brush
{
    ID2D1Brush ID2D1Brush_iface;
    LONG refcount;

    ID2D1Factory *factory;
    float opacity;
    D2D1_MATRIX_3X2_F transform;

    enum d2d_brush_type type;
    union
    {
        struct
        {
            D2D1_COLOR_F color;
        } solid;
        struct
        {
            struct d2d_gradient *gradient;
            D2D1_POINT_2F start;
            D2D1_POINT_2F end;
        } linear;
        struct
        {
            struct d2d_gradient *gradient;
            D2D1_POINT_2F centre;
            D2D1_POINT_2F offset;
            D2D1_POINT_2F radius;
        } radial;
        struct
        {
            struct d2d_bitmap *bitmap;
            D2D1_EXTEND_MODE extend_mode_x;
            D2D1_EXTEND_MODE extend_mode_y;
            D2D1_INTERPOLATION_MODE interpolation_mode;
        } bitmap;
        struct
        {
            ID2D1Image *image;
            D2D1_EXTEND_MODE extend_mode_x;
            D2D1_EXTEND_MODE extend_mode_y;
            D2D1_INTERPOLATION_MODE interpolation_mode;
            D2D1_RECT_F source_rect;
        } image;
    } u;
};

HRESULT d2d_solid_color_brush_create(ID2D1Factory *factory, const D2D1_COLOR_F *color,
        const D2D1_BRUSH_PROPERTIES *desc, struct d2d_brush **brush);
HRESULT d2d_linear_gradient_brush_create(ID2D1Factory *factory,
        const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES *gradient_desc, const D2D1_BRUSH_PROPERTIES *brush_desc,
        ID2D1GradientStopCollection *gradient, struct d2d_brush **brush);
HRESULT d2d_radial_gradient_brush_create(ID2D1Factory *factory,
        const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES *gradient_desc, const D2D1_BRUSH_PROPERTIES *brush_desc,
        ID2D1GradientStopCollection *gradient, struct d2d_brush **brush);
HRESULT d2d_bitmap_brush_create(ID2D1Factory *factory, ID2D1Bitmap *bitmap,
        const D2D1_BITMAP_BRUSH_PROPERTIES1 *bitmap_brush_desc, const D2D1_BRUSH_PROPERTIES *brush_desc,
        struct d2d_brush **brush);
HRESULT d2d_image_brush_create(ID2D1Factory *factory, ID2D1Image *image,
        const D2D1_IMAGE_BRUSH_PROPERTIES *image_brush_desc, const D2D1_BRUSH_PROPERTIES *brush_desc,
        struct d2d_brush **brush);
void d2d_brush_bind_resources(struct d2d_brush *brush, struct d2d_device_context *context,
        unsigned int brush_idx);
BOOL d2d_brush_fill_cb(const struct d2d_brush *brush, struct d2d_brush_cb *cb);
struct d2d_brush *unsafe_impl_from_ID2D1Brush(ID2D1Brush *iface);

struct d2d_stroke_style
{
    ID2D1StrokeStyle1 ID2D1StrokeStyle1_iface;
    LONG refcount;

    ID2D1Factory *factory;
    D2D1_STROKE_STYLE_PROPERTIES1 desc;
    float *dashes;
    UINT32 dash_count;
};

HRESULT d2d_stroke_style_init(struct d2d_stroke_style *style, ID2D1Factory *factory,
        const D2D1_STROKE_STYLE_PROPERTIES1 *desc, const float *dashes, UINT32 dash_count);
struct d2d_stroke_style *unsafe_impl_from_ID2D1StrokeStyle(ID2D1StrokeStyle *iface);

struct d2d_layer
{
    ID2D1Layer ID2D1Layer_iface;
    LONG refcount;

    ID2D1Factory *factory;
    D2D1_SIZE_F size;
};

HRESULT d2d_layer_create(ID2D1Factory *factory, const D2D1_SIZE_F *size, struct d2d_layer **layer);

enum d2d_mesh_state
{
    D2D_MESH_STATE_INITIAL = 0,
    D2D_MESH_STATE_OPEN,
    D2D_MESH_STATE_CLOSED,
};

struct d2d_mesh
{
    ID2D1Mesh ID2D1Mesh_iface;
    ID2D1TessellationSink ID2D1TessellationSink_iface;
    LONG refcount;

    ID2D1Factory *factory;
    enum d2d_mesh_state state;

    D2D1_TRIANGLE *triangles;
    size_t count;
    size_t size;
};

HRESULT d2d_mesh_create(ID2D1Factory *factory, struct d2d_mesh **mesh);
struct d2d_mesh *unsafe_impl_from_ID2D1Mesh(ID2D1Mesh *iface);

struct d2d_bitmap
{
    ID2D1Bitmap1 ID2D1Bitmap1_iface;
    LONG refcount;

    ID2D1Factory *factory;
    ID3D11ShaderResourceView *srv;
    ID3D11RenderTargetView *rtv;
    IDXGISurface *surface;
    ID3D11Resource *resource;
    D3D11_MAPPED_SUBRESOURCE mapped_resource;
    D2D1_SIZE_U pixel_size;
    D2D1_PIXEL_FORMAT format;
    float dpi_x;
    float dpi_y;
    D2D1_BITMAP_OPTIONS options;
};

HRESULT d2d_bitmap_create(struct d2d_device_context *context, D2D1_SIZE_U size, const void *src_data,
        UINT32 pitch, const D2D1_BITMAP_PROPERTIES1 *desc, struct d2d_bitmap **bitmap);
HRESULT d2d_bitmap_create_shared(struct d2d_device_context *context, REFIID iid, void *data,
        const D2D1_BITMAP_PROPERTIES1 *desc, struct d2d_bitmap **bitmap);
HRESULT d2d_bitmap_create_from_wic_bitmap(struct d2d_device_context *context, IWICBitmapSource *bitmap_source,
        const D2D1_BITMAP_PROPERTIES1 *desc, struct d2d_bitmap **bitmap);
unsigned int d2d_get_bitmap_options_for_surface(IDXGISurface *surface);
struct d2d_bitmap *unsafe_impl_from_ID2D1Bitmap(ID2D1Bitmap *iface);

struct d2d_state_block
{
    ID2D1DrawingStateBlock1 ID2D1DrawingStateBlock1_iface;
    LONG refcount;

    ID2D1Factory *factory;
    D2D1_DRAWING_STATE_DESCRIPTION1 drawing_state;
    IDWriteRenderingParams *text_rendering_params;
};

void d2d_state_block_init(struct d2d_state_block *state_block, ID2D1Factory *factory,
        const D2D1_DRAWING_STATE_DESCRIPTION1 *desc, IDWriteRenderingParams *text_rendering_params);
struct d2d_state_block *unsafe_impl_from_ID2D1DrawingStateBlock(ID2D1DrawingStateBlock *iface);

#endif
