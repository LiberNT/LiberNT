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

#ifndef LIBERNT_D2D1_COMMAND_LIST_H
#define LIBERNT_D2D1_COMMAND_LIST_H

#include "d2d1_common.h"

enum d2d_command_list_state
{
    D2D_COMMAND_LIST_STATE_INITIAL = 0,
    D2D_COMMAND_LIST_STATE_ERROR,
    D2D_COMMAND_LIST_STATE_OPEN,
    D2D_COMMAND_LIST_STATE_CLOSED,
};

struct d2d_command_list
{
    ID2D1CommandList ID2D1CommandList_iface;
    LONG refcount;

    ID2D1Factory *factory;
    enum d2d_command_list_state state;
    unsigned int flags;

    size_t size;
    size_t capacity;
    void *data;

    size_t objects_count;
    size_t objects_capacity;
    IUnknown **objects;
};

HRESULT d2d_command_list_create(ID2D1Factory *factory, struct d2d_command_list **command_list);
struct d2d_command_list *unsafe_impl_from_ID2D1CommandList(ID2D1CommandList *iface);
void d2d_command_list_begin_draw(struct d2d_command_list *command_list, const struct d2d_device_context *context);
void d2d_command_list_set_antialias_mode(struct d2d_command_list *command_list, D2D1_ANTIALIAS_MODE mode);
void d2d_command_list_set_primitive_blend(struct d2d_command_list *command_list,
        D2D1_PRIMITIVE_BLEND primitive_blend);
void d2d_command_list_set_unit_mode(struct d2d_command_list *command_list, D2D1_UNIT_MODE mode);
void d2d_command_list_set_text_antialias_mode(struct d2d_command_list *command_list,
        D2D1_TEXT_ANTIALIAS_MODE mode);
void d2d_command_list_set_tags(struct d2d_command_list *command_list, D2D1_TAG tag1, D2D1_TAG tag2);
void d2d_command_list_set_transform(struct d2d_command_list *command_list,
        const D2D1_MATRIX_3X2_F *transform);
void d2d_command_list_push_clip(struct d2d_command_list *command_list, const D2D1_RECT_F *rect,
        D2D1_ANTIALIAS_MODE antialias_mode);
void d2d_command_list_pop_clip(struct d2d_command_list *command_list);
void d2d_command_list_clear(struct d2d_command_list *command_list, const D2D1_COLOR_F *color);
void d2d_command_list_draw_line(struct d2d_command_list *command_list, const struct d2d_device_context *context,
        D2D1_POINT_2F p0, D2D1_POINT_2F p1, ID2D1Brush *orig_brush, float stroke_width,
        ID2D1StrokeStyle *stroke_style);
void d2d_command_list_draw_geometry(struct d2d_command_list *command_list,
        const struct d2d_device_context *context, ID2D1Geometry *geometry, ID2D1Brush *orig_brush,
        float stroke_width, ID2D1StrokeStyle *stroke_style);
void d2d_command_list_draw_rectangle(struct d2d_command_list *command_list, const struct d2d_device_context *context,
        const D2D1_RECT_F *rect, ID2D1Brush *orig_brush, float stroke_width, ID2D1StrokeStyle *stroke_style);
void d2d_command_list_fill_geometry(struct d2d_command_list *command_list,
        const struct d2d_device_context *context, ID2D1Geometry *geometry, ID2D1Brush *orig_brush,
        ID2D1Brush *orig_opacity_brush);
void d2d_command_list_fill_rectangle(struct d2d_command_list *command_list,
        const struct d2d_device_context *context, const D2D1_RECT_F *rect, ID2D1Brush *orig_brush);
void d2d_command_list_set_text_rendering_params(struct d2d_command_list *command_list,
        IDWriteRenderingParams *params);
void d2d_command_list_draw_glyph_run(struct d2d_command_list *command_list,
        const struct d2d_device_context *context, D2D1_POINT_2F origin, const DWRITE_GLYPH_RUN *run,
        const DWRITE_GLYPH_RUN_DESCRIPTION *run_desc, ID2D1Brush *orig_brush,
        DWRITE_MEASURING_MODE measuring_mode);
void d2d_command_list_draw_bitmap(struct d2d_command_list *command_list, ID2D1Bitmap *bitmap,
        const D2D1_RECT_F *dst_rect, float opacity, D2D1_INTERPOLATION_MODE interpolation_mode,
        const D2D1_RECT_F *src_rect, const D2D1_MATRIX_4X4_F *perspective_transform);
void d2d_command_list_draw_image(struct d2d_command_list *command_list, ID2D1Image *image,
        const D2D1_POINT_2F *target_offset, const D2D1_RECT_F *image_rect, D2D1_INTERPOLATION_MODE interpolation_mode,
        D2D1_COMPOSITE_MODE composite_mode);
void d2d_command_list_fill_mesh(struct d2d_command_list *command_list, const struct d2d_device_context *context,
        ID2D1Mesh *mesh, ID2D1Brush *orig_brush);
void d2d_command_list_fill_opacity_mask(struct d2d_command_list *command_list, const struct d2d_device_context *context,
        ID2D1Bitmap *bitmap, ID2D1Brush *orig_brush, const D2D1_RECT_F *dst_rect, const D2D1_RECT_F *src_rect);
void d2d_command_list_push_layer(struct d2d_command_list *command_list, const struct d2d_device_context *context,
        const D2D1_LAYER_PARAMETERS1 *params);
void d2d_command_list_pop_layer(struct d2d_command_list *command_list);

#endif
