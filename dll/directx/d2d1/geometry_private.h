/*
 * Copyright 2015 Henri Verbeet for CodeWeavers, 2026 Earzu Chan
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

#ifndef LIBERNT_D2D1_GEOMETRY_PRIVATE_H
#define LIBERNT_D2D1_GEOMETRY_PRIVATE_H

#include "d2d1_private.h"

enum d2d_vertex_type
{
    D2D_VERTEX_TYPE_NONE,
    D2D_VERTEX_TYPE_LINE,
    D2D_VERTEX_TYPE_BEZIER,
    D2D_VERTEX_TYPE_SPLIT_BEZIER,
    D2D_VERTEX_TYPE_END,
};

struct d2d_figure
{
    D2D1_POINT_2F *vertices;
    size_t vertices_size;
    enum d2d_vertex_type *vertex_types;
    size_t vertex_types_size;
    size_t vertex_count;

    D2D1_POINT_2F *bezier_controls;
    size_t bezier_controls_size;
    size_t bezier_control_count;

    D2D1_POINT_2F *original_bezier_controls;
    size_t original_bezier_controls_size;
    size_t original_bezier_control_count;

    D2D1_RECT_F bounds;
    unsigned int flags;

    struct
    {
        uint8_t *data;
        size_t size;
        size_t capacity;

        size_t current;
        size_t count;
    } segments;
};

bool d2d_figure_add_beziers(struct d2d_figure *figure, const D2D1_BEZIER_SEGMENT *beziers, UINT32 count);
bool d2d_figure_begin(struct d2d_figure *figure, D2D1_POINT_2F start_point, D2D1_FIGURE_BEGIN figure_begin);
void d2d_figure_end(struct d2d_figure *figure, D2D1_FIGURE_END figure_end);
void d2d_figure_cleanup(struct d2d_figure *figure);
void d2d_geometry_cleanup(struct d2d_geometry *geometry);
void d2d_geometry_init(struct d2d_geometry *geometry, ID2D1Factory *factory,
        const D2D1_MATRIX_3X2_F *transform, const struct ID2D1GeometryVtbl *vtbl,
        const struct d2d_geometry_ops *ops);
void d2d_geometry_stream(ID2D1Geometry *iface, const D2D_MATRIX_3X2_F *transform, ID2D1GeometrySink *sink);
unsigned char *d2d_path_cache_make_key(const struct d2d_geometry *geometry, size_t *key_size, UINT64 *hash);
BOOL d2d_path_cache_apply(struct d2d_geometry *geometry, const unsigned char *key, size_t key_size, UINT64 hash);
void d2d_path_cache_insert(const struct d2d_geometry *geometry, unsigned char *key, size_t key_size, UINT64 hash);

void d2d_figure_simplify(const struct d2d_figure *figure, D2D1_GEOMETRY_SIMPLIFICATION_OPTION option,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, ID2D1SimplifiedGeometrySink *sink);
HRESULT d2d_geometry_tessellate(ID2D1Geometry *geometry, const D2D1_MATRIX_3X2_F *transform,
        float tolerance, ID2D1TessellationSink *sink);
HRESULT d2d_geometry_get_widened_bounds(ID2D1Geometry *geometry, float stroke_width, ID2D1StrokeStyle *stroke_style,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, D2D1_RECT_F *bounds);
HRESULT d2d_geometry_widen(ID2D1Geometry *geometry, float stroke_width, ID2D1StrokeStyle *stroke_style,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, ID2D1SimplifiedGeometrySink *sink);
HRESULT d2d_geometry_combine(ID2D1Geometry *geometry, ID2D1Geometry *other, D2D1_COMBINE_MODE mode,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, ID2D1SimplifiedGeometrySink *sink);
HRESULT d2d_geometry_compute_area(ID2D1Geometry *geometry, const D2D1_MATRIX_3X2_F *transform,
        float tolerance, float *area);
extern const struct ID2D1EllipseGeometryVtbl d2d_ellipse_geometry_vtbl;
extern const struct ID2D1RectangleGeometryVtbl d2d_rectangle_geometry_vtbl;
extern const struct ID2D1RoundedRectangleGeometryVtbl d2d_rounded_rectangle_geometry_vtbl;
extern const struct ID2D1TransformedGeometryVtbl d2d_transformed_geometry_vtbl;

void d2d_face_set(struct d2d_face *f, UINT16 v0, UINT16 v1, UINT16 v2);
BOOL d2d_point_on_line_segment(const D2D1_POINT_2F *q, const D2D1_POINT_2F *p0,
        const D2D1_POINT_2F *p1, const D2D1_MATRIX_3X2_F *transform, float stroke_width, float tolerance);
bool d2d_figure_add_lines(struct d2d_figure *figure, const D2D1_POINT_2F *points, UINT32 count);
BOOL d2d_geometry_outline_add_join(struct d2d_geometry *geometry,
        const D2D1_POINT_2F *q_prev, const D2D1_POINT_2F *p, const D2D1_POINT_2F *q_next);
BOOL d2d_geometry_outline_add_line_segment(struct d2d_geometry *geometry,
        const D2D1_POINT_2F *p0, const D2D1_POINT_2F *p1);
BOOL d2d_geometry_outline_add_arc_quadrant(struct d2d_geometry *geometry,
        const D2D1_POINT_2F *p0, const D2D1_POINT_2F *p1, const D2D1_POINT_2F *p2);
BOOL d2d_geometry_fill_add_arc_triangle(struct d2d_geometry *geometry,
        const D2D1_POINT_2F *p0, const D2D1_POINT_2F *p1, const D2D1_POINT_2F *p2);
float d2d_triangle_area(const D2D1_TRIANGLE *triangle);
void d2d_arc_transform(D2D1_ARC_SEGMENT *arc, const D2D1_MATRIX_3X2_F *transform);

#endif
