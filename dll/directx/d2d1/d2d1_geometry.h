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

#ifndef LIBERNT_D2D1_GEOMETRY_H
#define LIBERNT_D2D1_GEOMETRY_H

#include "d2d1_common.h"

#define D2D_COVERAGE_TILE_SHIFT 4
#define D2D_COVERAGE_TILE (1 << D2D_COVERAGE_TILE_SHIFT)

struct d2d_coverage_run
{
    int x, y, width, height;
    BOOL edge;
    size_t tile;
};

struct d2d_coverage
{
    float *accum;
    size_t accum_size;
    BYTE *flags;
    size_t flags_size;
    BYTE *tiles;
    size_t tiles_size;
    struct d2d_coverage_run *runs;
    size_t runs_size;
    size_t run_count;
    size_t edge_count;
    int width;
    int height;
    int tile_cols;
    int tile_rows;
    BYTE *rowflags;
    size_t rowflags_size;
    int rowflag_cols;
    BOOL dirty;
};

struct d2d_face;
struct d2d_flat
{
    D2D1_POINT_2F *vertices;
    size_t vertex_count;
    struct d2d_face *faces;
    size_t face_count;
    size_t *contours;
    size_t contour_count;
    float max_scale;
};

struct d2d_geometry;
void d2d_coverage_cleanup(struct d2d_coverage *coverage);
BOOL d2d_geometry_get_device_bounds(const struct d2d_geometry *geometry, BOOL outline,
        const D2D1_MATRIX_3X2_F *world, D2D1_RECT_F *bounds);
BOOL d2d_geometry_is_pixel_aligned_rect(const struct d2d_geometry *geometry, const D2D1_MATRIX_3X2_F *world);
BOOL d2d_coverage_clip_is_fractional(const D2D1_RECT_F *clip);
BOOL d2d_rect_is_pixel_aligned(const D2D1_RECT_F *rect, const D2D1_MATRIX_3X2_F *world);
struct d2d_face;
#define D2D_CONVEX_MAX_POINTS 136
#define D2D_CONVEX_MAX_VERTICES 544
#define D2D_CONVEX_MAX_FACES 816

struct d2d_aa_vertex
{
    float x, y, a;
};

size_t d2d_convex_aa_polygon(D2D1_POINT_2F *local, size_t count, const D2D1_MATRIX_3X2_F *geometry_transform,
        const D2D1_MATRIX_3X2_F *world, const D2D1_MATRIX_3X2_F *from_device, float stroke_width,
        struct d2d_aa_vertex *vertices, struct d2d_face *faces, size_t *face_count);
size_t d2d_geometry_convex_aa(const struct d2d_geometry *geometry, const D2D1_MATRIX_3X2_F *world,
        const D2D1_MATRIX_3X2_F *from_device, float stroke_width, struct d2d_aa_vertex *vertices,
        struct d2d_face *faces, size_t *face_count);
void d2d_path_cache_free(void);
BOOL d2d_geometry_polygon_aa(const struct d2d_geometry *geometry, const D2D1_MATRIX_3X2_F *world,
        const D2D1_MATRIX_3X2_F *from_device, struct d2d_aa_vertex **vertices, size_t *vertex_count,
        struct d2d_face **faces, size_t *face_count);
BOOL d2d_geometry_rasterize(const struct d2d_geometry *geometry, BOOL outline, float stroke_width,
        const D2D1_MATRIX_3X2_F *world, const RECT *area, const D2D1_RECT_F *clip, struct d2d_coverage *coverage);

enum d2d_geometry_state
{
    D2D_GEOMETRY_STATE_INITIAL = 0,
    D2D_GEOMETRY_STATE_ERROR,
    D2D_GEOMETRY_STATE_OPEN,
    D2D_GEOMETRY_STATE_CLOSED,
    D2D_GEOMETRY_STATE_FIGURE,
};

struct d2d_curve_vertex
{
    D2D1_POINT_2F position;
    struct
    {
        float u, v, sign;
    } texcoord;
};

struct d2d_face
{
    UINT16 v[3];
};

struct d2d_outline_vertex
{
    D2D1_POINT_2F position;
    D2D1_POINT_2F prev;
    D2D1_POINT_2F next;
};

struct d2d_curve_outline_vertex
{
    D2D1_POINT_2F position;
    D2D1_POINT_2F p0, p1, p2;
    D2D1_POINT_2F prev, next;
};

struct d2d_geometry;

struct d2d_geometry_ops
{
    void (*stream)(struct d2d_geometry *geometry, const D2D_MATRIX_3X2_F *transform,
            ID2D1GeometrySink *sink);
};

struct d2d_geometry
{
    ID2D1Geometry ID2D1Geometry_iface;
    LONG refcount;

    const struct d2d_geometry_ops *ops;

    ID2D1Factory *factory;

    D2D_MATRIX_3X2_F transform;

    struct
    {
        D2D1_POINT_2F *vertices;
        size_t vertex_count;

        struct d2d_face *faces;
        size_t faces_size;
        size_t face_count;

        struct d2d_curve_vertex *bezier_vertices;
        size_t bezier_vertices_size;
        size_t bezier_vertex_count;

        struct d2d_curve_vertex *arc_vertices;
        size_t arc_vertices_size;
        size_t arc_vertex_count;
    } fill;

    struct
    {
        BOOL valid;
        D2D1_RECT_F rect;
        float radius_x, radius_y;
    } convex;
    BOOL fill_polygon;
    size_t *fill_contours;
    size_t fill_contour_count;
    struct d2d_flat flat;

    struct
    {
        struct d2d_outline_vertex *vertices;
        size_t vertices_size;
        size_t vertex_count;

        struct d2d_face *faces;
        size_t faces_size;
        size_t face_count;

        struct d2d_curve_outline_vertex *beziers;
        size_t beziers_size;
        size_t bezier_count;

        struct d2d_face *bezier_faces;
        size_t bezier_faces_size;
        size_t bezier_face_count;

        struct d2d_curve_outline_vertex *arcs;
        size_t arcs_size;
        size_t arc_count;

        struct d2d_face *arc_faces;
        size_t arc_faces_size;
        size_t arc_face_count;
    } outline;

    union
    {
        struct
        {
            D2D1_ELLIPSE ellipse;
        } ellipse;
        struct
        {
            ID2D1GeometrySink ID2D1GeometrySink_iface;

            struct d2d_figure *figures;
            size_t figures_size;
            size_t figure_count;

            enum d2d_geometry_state state;
            HRESULT code;
            D2D1_FILL_MODE fill_mode;
            UINT32 segment_flags;
            UINT32 segment_count;

            D2D1_RECT_F bounds;
        } path;
        struct
        {
            D2D1_RECT_F rect;
        } rectangle;
        struct
        {
            D2D1_ROUNDED_RECT rounded_rect;
        } rounded_rectangle;
        struct
        {
            ID2D1Geometry *src_geometry;
            D2D_MATRIX_3X2_F transform;
        } transformed;
        struct
        {
            ID2D1Geometry **src_geometries;
            UINT32 geometry_count;
            D2D1_FILL_MODE fill_mode;
            ID2D1PathGeometry *path;
        } group;
    } u;
};

HRESULT d2d_ellipse_geometry_init(struct d2d_geometry *geometry,
        ID2D1Factory *factory, const D2D1_ELLIPSE *ellipse);
void d2d_path_geometry_init(struct d2d_geometry *geometry, ID2D1Factory *factory);
HRESULT d2d_rectangle_geometry_init(struct d2d_geometry *geometry,
        ID2D1Factory *factory, const D2D1_RECT_F *rect);
HRESULT d2d_rounded_rectangle_geometry_init(struct d2d_geometry *geometry,
        ID2D1Factory *factory, const D2D1_ROUNDED_RECT *rounded_rect);
void d2d_transformed_geometry_init(struct d2d_geometry *geometry, ID2D1Factory *factory,
        ID2D1Geometry *src_geometry, const D2D_MATRIX_3X2_F *transform);
HRESULT d2d_geometry_group_init(struct d2d_geometry *geometry, ID2D1Factory *factory,
        D2D1_FILL_MODE fill_mode, ID2D1Geometry **src_geometries, unsigned int geometry_count);
struct d2d_geometry *unsafe_impl_from_ID2D1Geometry(ID2D1Geometry *iface);

struct d2d_geometry_realization
{
    ID2D1GeometryRealization ID2D1GeometryRealization_iface;
    LONG refcount;

    ID2D1Factory *factory;
    ID2D1Geometry *geometry;
    bool filled;

    ID2D1StrokeStyle *stroke_style;
    float stroke_width;
};

HRESULT d2d_geometry_realization_init(struct d2d_geometry_realization *realization,
        ID2D1Factory *factory, ID2D1Geometry *geometry);
struct d2d_geometry_realization *unsafe_impl_from_ID2D1GeometryRealization(ID2D1GeometryRealization *iface);

#endif
