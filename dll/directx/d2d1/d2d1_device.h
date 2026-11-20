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

#ifndef LIBERNT_D2D1_DEVICE_H
#define LIBERNT_D2D1_DEVICE_H

#include "d2d1_geometry.h"

struct d2d_clip_stack
{
    D2D1_RECT_F *stack;
    size_t size;
    size_t count;
};

struct d2d_device_context_layer
{
    struct d2d_bitmap *bitmap;
    struct d2d_bitmap *target;
    D2D1_LAYER_PARAMETERS1 parameters;
    D2D1_MATRIX_3X2_F transform;
    BOOL active;
    BOOL clip;
};

struct d2d_layer_stack
{
    struct d2d_device_context_layer *stack;
    size_t size;
    size_t count;
    size_t initialized;
};

struct d2d_error_state
{
    HRESULT code;
    D2D1_TAG tag1, tag2;
};

struct d2d_shape_resources
{
    ID3D11InputLayout *il;
    ID3D11VertexShader *vs;
};

struct d2d_brush_cb
{
    enum d2d_brush_type type;
    float opacity;
    unsigned int pad[2];
    union
    {
        struct
        {
            D2D1_COLOR_F colour;
        } solid;
        struct
        {
            D2D1_POINT_2F start;
            D2D1_POINT_2F end;
            unsigned int stop_count;
            unsigned int extend_mode;
            unsigned int gamma;
        } linear;
        struct
        {
            D2D1_POINT_2F centre;
            D2D1_POINT_2F offset;
            D2D1_POINT_2F ra;
            D2D1_POINT_2F rb;
            unsigned int stop_count;
            unsigned int extend_mode;
            unsigned int gamma;
            float pad;
        } radial;
        struct
        {
            float _11, _21, _31, pad;
            float _12, _22, _32;
            BOOL ignore_alpha;
        } bitmap;
    } u;
};

struct d2d_ps_cb
{
    BOOL outline;
    BOOL is_arc;
    BOOL mask;
    BOOL pad;
    INT mask_origin[2];
    INT pad2[2];
    float clip[4];
    BOOL clip_on;
    BOOL blend_min;
    BOOL pad3[2];
    struct d2d_brush_cb colour_brush;
    struct d2d_brush_cb opacity_brush;
};

struct d2d_vec4
{
    float x, y, z, w;
};

struct d2d_vs_cb
{
    struct
    {
        float _11, _21, _31, pad0;
        float _12, _22, _32, stroke_width;
    } transform_geometry;
    struct d2d_vec4 transform_rtx;
    struct d2d_vec4 transform_rty;
};

struct d2d_device_context_ops
{
    HRESULT (*device_context_present)(IUnknown *outer_unknown);
};

enum d2d_device_context_sampler_limits
{
    D2D_SAMPLER_INTERPOLATION_MODE_COUNT = 2,
    D2D_SAMPLER_EXTEND_MODE_COUNT = 3,
};

enum d2d_device_context_target_type
{
    D2D_TARGET_UNKNOWN = 0,
    D2D_TARGET_BITMAP,
    D2D_TARGET_COMMAND_LIST,
};

struct d2d_indexed_objects
{
    struct
    {
        GUID id;
        IUnknown *object;
    } *elements;
    size_t size;
    size_t count;
};

struct d2d_glyph_atlas;

struct d2d_stream
{
    ID3D11Buffer *buffer;
    UINT size, pos, bind;
};

struct d2d_device_context
{
    ID2D1DeviceContext6 ID2D1DeviceContext6_iface;
    ID2D1GdiInteropRenderTarget ID2D1GdiInteropRenderTarget_iface;
    IDWriteTextRenderer IDWriteTextRenderer_iface;
    IUnknown IUnknown_iface;
    LONG refcount;

    IUnknown *outer_unknown;
    const struct d2d_device_context_ops *ops;

    ID2D1Factory *factory;
    CRITICAL_SECTION *cs;
    struct d2d_device *device;
    ID3D11Device1 *d3d_device;
    ID3DDeviceContextState *d3d_state;
    ID3DDeviceContextState *prev_state;
    BOOL state_in;
    struct d2d_shadow_state
    {
        BOOL valid;
        ID3D11InputLayout *il;
        ID3D11VertexShader *vs;
        ID3D11PixelShader *ps;
        ID3D11Buffer *vs_cb, *ps_cb, *ib, *vb;
        ID3D11RasterizerState *rs;
        ID3D11BlendState *bs;
        ID3D11RenderTargetView *rtv;
        UINT ib_offset, vb_offset, vb_stride;
        D3D11_VIEWPORT vp;
        D3D11_RECT scissor;
    } sh;
    struct
    {
        ID2D1Image *object;
        enum d2d_device_context_target_type type;
        union
        {
            struct d2d_bitmap *bitmap;
            struct d2d_command_list *command_list;
        };
        HDC hdc;
    } target;
    struct d2d_shape_resources shape_resources[D2D_SHAPE_TYPE_COUNT];
    ID3D11Buffer *vs_cb;
    ID3D11PixelShader *ps;
    ID3D11Buffer *ps_cb;
    ID3D11Buffer *ib;
    unsigned int vb_stride;
    ID3D11Buffer *vb;
    ID3D11RasterizerState *rs;
    ID3D11BlendState *bs[D2D1_PRIMITIVE_BLEND_MAX + 1];
    ID3D11SamplerState *sampler_states
            [D2D_SAMPLER_INTERPOLATION_MODE_COUNT]
            [D2D_SAMPLER_EXTEND_MODE_COUNT]
            [D2D_SAMPLER_EXTEND_MODE_COUNT];

    struct d2d_error_state error;
    D2D1_DRAWING_STATE_DESCRIPTION1 drawing_state;
    IDWriteRenderingParams *text_rendering_params;
    IDWriteRenderingParams *default_text_rendering_params;

    D2D1_RENDER_TARGET_PROPERTIES desc;
    D2D1_SIZE_U pixel_size;
    struct d2d_clip_stack clip_stack;
    struct d2d_layer_stack layer_stack;
    struct d2d_coverage coverage;
    ID3D11Texture2D *mask_texture;
    ID3D11ShaderResourceView *mask_srv;
    BYTE *mask_staging;
    size_t mask_staging_size;
    UINT mask_width, mask_height;
    BOOL mask_active;
    BOOL ps_clip_on;
    int mask_x, mask_y;
    struct d2d_stream vb_stream, ib_stream;
    struct d2d_glyph_atlas *glyph_atlas[2];
    UINT draw_vb_offset, draw_ib_offset;
    struct d2d_vs_cb vs_shadow;
    struct d2d_ps_cb ps_shadow;
    BOOL vs_shadow_valid, ps_shadow_valid;

    struct d2d_indexed_objects vertex_buffers;
};

HRESULT d2d_d3d_create_render_target(struct d2d_device *device, IDXGISurface *surface, IUnknown *outer_unknown,
        const struct d2d_device_context_ops *ops, const D2D1_RENDER_TARGET_PROPERTIES *desc,
        void **render_target);

static inline BOOL d2d_device_context_is_dxgi_target(const struct d2d_device_context *context)
{
    return !context->ops;
}

struct d2d_shader_set
{
    LONG refcount;
    CRITICAL_SECTION cs;
    ID3D10Blob *precompiled_shape_vs[D2D_SHAPE_TYPE_COUNT];
    ID3D10Blob *precompiled_shape_ps;
    ID3D11InputLayout *il[D2D_SHAPE_TYPE_COUNT];
    ID3D11VertexShader *vs[D2D_SHAPE_TYPE_COUNT];
    ID3D11PixelShader *ps;
};

HRESULT d2d_shader_set_create(struct d2d_shader_set **set);
void d2d_shader_set_release(struct d2d_shader_set *set);
struct d2d_shader_set *d2d_factory_get_shader_set(ID2D1Factory1 *factory, IDXGIDevice *dxgi_device);

struct d2d_device
{
    ID2D1Device6 ID2D1Device6_iface;
    LONG refcount;
    ID2D1Factory1 *factory;
    IDXGIDevice *dxgi_device;
    bool allow_get_dxgi_device;

    struct d2d_shader_set *shader_set;

    struct d2d_indexed_objects shaders;
};

struct d2d_device *unsafe_impl_from_ID2D1Device(ID2D1Device1 *iface);
HRESULT d2d_device_add_indexed_object(struct d2d_indexed_objects *objects, const GUID *id,
        IUnknown *object);
BOOL d2d_device_get_indexed_object(struct d2d_indexed_objects *objects, const GUID *id,
        IUnknown **object);
void d2d_device_indexed_objects_clear(struct d2d_indexed_objects *objects);

void d2d_device_context_state_out(struct d2d_device_context *context);
HRESULT d2d_device_init(struct d2d_device *device, ID2D1Factory1 *factory, IDXGIDevice *dxgi_device,
        bool allow_get_dxgi_device);

#endif
