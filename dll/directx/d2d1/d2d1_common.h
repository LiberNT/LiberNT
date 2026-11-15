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

#ifndef LIBERNT_D2D1_COMMON_H
#define LIBERNT_D2D1_COMMON_H


#include "wine/debug.h"
#include "wine/list.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#define COBJMACROS
#include "d2d1_3.h"
#include "d2d1effectauthor.h"
#include "d3d11_1.h"
#ifdef D2D1_INIT_GUID
#include "initguid.h"
#endif
#include "dwrite_2.h"

enum d2d_brush_type
{
    D2D_BRUSH_TYPE_SOLID,
    D2D_BRUSH_TYPE_LINEAR,
    D2D_BRUSH_TYPE_RADIAL,
    D2D_BRUSH_TYPE_BITMAP,
    D2D_BRUSH_TYPE_IMAGE,
    D2D_BRUSH_TYPE_COUNT,
};

enum d2d_shape_type
{
    D2D_SHAPE_TYPE_OUTLINE,
    D2D_SHAPE_TYPE_BEZIER_OUTLINE,
    D2D_SHAPE_TYPE_ARC_OUTLINE,
    D2D_SHAPE_TYPE_TRIANGLE,
    D2D_SHAPE_TYPE_CURVE,
    D2D_SHAPE_TYPE_TRIANGLE_AA,
    D2D_SHAPE_TYPE_COUNT,
};

struct d2d_settings
{
    unsigned int max_version_factory;
};
extern struct d2d_settings d2d_settings;

struct d2d_bitmap;
struct d2d_brush;
struct d2d_brush_cb;
struct d2d_command_list;
struct d2d_device;
struct d2d_device_context;
struct d2d_effect;
struct d2d_effect_properties;
struct d2d_effect_property;
struct d2d_effect_registration;
struct d2d_factory;
struct d2d_shader_set;
struct d2d_state_block;
struct d2d_stroke_style;

static inline BOOL d2d_array_reserve(void **elements, size_t *capacity, size_t count, size_t size)
{
    size_t new_capacity, max_capacity;
    void *new_elements;

    if (count <= *capacity)
        return TRUE;

    max_capacity = ~(SIZE_T)0 / size;
    if (count > max_capacity)
        return FALSE;

    new_capacity = max(4, *capacity);
    while (new_capacity < count && new_capacity <= max_capacity / 2)
        new_capacity *= 2;
    if (new_capacity < count)
        new_capacity = max_capacity;

    if (!(new_elements = realloc(*elements, new_capacity * size)))
        return FALSE;

    *elements = new_elements;
    *capacity = new_capacity;
    return TRUE;
}

static inline void d2d_matrix_multiply(D2D_MATRIX_3X2_F *a, const D2D_MATRIX_3X2_F *b)
{
    D2D_MATRIX_3X2_F tmp = *a;

    a->_11 = tmp._11 * b->_11 + tmp._12 * b->_21;
    a->_12 = tmp._11 * b->_12 + tmp._12 * b->_22;
    a->_21 = tmp._21 * b->_11 + tmp._22 * b->_21;
    a->_22 = tmp._21 * b->_12 + tmp._22 * b->_22;
    a->_31 = tmp._31 * b->_11 + tmp._32 * b->_21 + b->_31;
    a->_32 = tmp._31 * b->_12 + tmp._32 * b->_22 + b->_32;
}

/* Dst must be different from src. */
static inline BOOL d2d_matrix_invert(D2D_MATRIX_3X2_F *dst, const D2D_MATRIX_3X2_F *src)
{
    float d = src->_11 * src->_22 - src->_21 * src->_12;

    if (d == 0.0f)
        return FALSE;
    dst->_11 = src->_22 / d;
    dst->_21 = -src->_21 / d;
    dst->_31 = (src->_21 * src->_32 - src->_31 * src->_22) / d;
    dst->_12 = -src->_12 / d;
    dst->_22 = src->_11 / d;
    dst->_32 = -(src->_11 * src->_32 - src->_31 * src->_12) / d;

    return TRUE;
}

static inline void d2d_point_set(D2D1_POINT_2F *dst, float x, float y)
{
    dst->x = x;
    dst->y = y;
}

static inline float d2d_point_dot(const D2D1_POINT_2F *p0, const D2D1_POINT_2F *p1)
{
    return p0->x * p1->x + p0->y * p1->y;
}

static inline void d2d_point_transform(D2D1_POINT_2F *dst, const D2D1_MATRIX_3X2_F *matrix, float x, float y)
{
    dst->x = x * matrix->_11 + y * matrix->_21 + matrix->_31;
    dst->y = x * matrix->_12 + y * matrix->_22 + matrix->_32;
}

static inline void d2d_rect_expand(D2D1_RECT_F *dst, const D2D1_POINT_2F *point)
{
    if (point->x < dst->left)
        dst->left = point->x;
    if (point->x > dst->right)
        dst->right = point->x;
    if (point->y < dst->top)
        dst->top = point->y;
    if (point->y > dst->bottom)
        dst->bottom = point->y;
}

static inline D2D1_INTERPOLATION_MODE d2d1_1_interp_mode_from_d2d1(D2D1_BITMAP_INTERPOLATION_MODE mode)
{
    return (D2D1_INTERPOLATION_MODE)mode;
}

static inline const char *debug_d2d_color_f(const D2D1_COLOR_F *colour)
{
    if (!colour)
        return "(null)";
    return wine_dbg_sprintf("{%.8e, %.8e, %.8e, %.8e}", colour->r, colour->g, colour->b, colour->a);
}

static inline const char *debug_d2d_point_2f(const D2D1_POINT_2F *point)
{
    if (!point)
        return "(null)";
    return wine_dbg_sprintf("{%.8e, %.8e}", point->x, point->y);
}

static inline const char *debug_d2d_point_2l(const D2D1_POINT_2L *point)
{
    if (!point)
        return "(null)";
    return wine_dbg_sprintf("{%ld, %ld}", point->x, point->y);
}

static inline const char *debug_d2d_point_2u(const D2D1_POINT_2U *point)
{
    if (!point)
        return "(null)";
    return wine_dbg_sprintf("{%u, %u}", point->x, point->y);
}

static inline const char *debug_d2d_rect_f(const D2D1_RECT_F *rect)
{
    if (!rect)
        return "(null)";
    return wine_dbg_sprintf("(%.8e, %.8e)-(%.8e, %.8e)", rect->left, rect->top, rect->right, rect->bottom);
}

static inline const char *debug_d2d_rect_l(const D2D1_RECT_L *rect)
{
    if (!rect)
        return "(null)";
    return wine_dbg_sprintf("(%ld, %ld)-(%ld, %ld)", rect->left, rect->top, rect->right, rect->bottom);
}

static inline const char *debug_d2d_rect_u(const D2D1_RECT_U *rect)
{
    if (!rect)
        return "(null)";
    return wine_dbg_sprintf("(%u, %u)-(%u, %u)", rect->left, rect->top, rect->right, rect->bottom);
}

static inline const char *debug_d2d_rounded_rect(const D2D1_ROUNDED_RECT *rounded_rect)
{
    if (!rounded_rect)
        return "(null)";
    return wine_dbg_sprintf("(%.8e, %.8e)-(%.8e, %.8e)[%.8e, %.8e]", rounded_rect->rect.left, rounded_rect->rect.top,
            rounded_rect->rect.right, rounded_rect->rect.bottom, rounded_rect->radiusX, rounded_rect->radiusY);
}

static inline const char *debug_d2d_ellipse(const D2D1_ELLIPSE *ellipse)
{
    if (!ellipse)
        return "(null)";
    return wine_dbg_sprintf("(%.8e, %.8e)[%.8e, %.8e]",
            ellipse->point.x, ellipse->point.y, ellipse->radiusX, ellipse->radiusY);
}

#endif
