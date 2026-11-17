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

#include "geometry_private.h"
#include <float.h>

WINE_DEFAULT_DEBUG_CHANNEL(d2d);

static const D2D1_MATRIX_3X2_F identity =
{{{
    1.0f, 0.0f,
    0.0f, 1.0f,
    0.0f, 0.0f,
}}};

static inline struct d2d_geometry *impl_from_ID2D1EllipseGeometry(ID2D1EllipseGeometry *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_geometry, ID2D1Geometry_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_ellipse_geometry_QueryInterface(ID2D1EllipseGeometry *iface,
        REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1EllipseGeometry)
            || IsEqualGUID(iid, &IID_ID2D1Geometry)
            || IsEqualGUID(iid, &IID_ID2D1Resource)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID2D1EllipseGeometry_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_ellipse_geometry_AddRef(ID2D1EllipseGeometry *iface)
{
    struct d2d_geometry *geometry = impl_from_ID2D1EllipseGeometry(iface);
    ULONG refcount = InterlockedIncrement(&geometry->refcount);

    TRACE("%p increasing refcount to %lu.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_ellipse_geometry_Release(ID2D1EllipseGeometry *iface)
{
    struct d2d_geometry *geometry = impl_from_ID2D1EllipseGeometry(iface);
    ULONG refcount = InterlockedDecrement(&geometry->refcount);

    TRACE("%p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
    {
        d2d_geometry_cleanup(geometry);
        free(geometry);
    }

    return refcount;
}

static void STDMETHODCALLTYPE d2d_ellipse_geometry_GetFactory(ID2D1EllipseGeometry *iface, ID2D1Factory **factory)
{
    struct d2d_geometry *geometry = impl_from_ID2D1EllipseGeometry(iface);

    TRACE("iface %p, factory %p.\n", iface, factory);

    ID2D1Factory_AddRef(*factory = geometry->factory);
}

static HRESULT STDMETHODCALLTYPE d2d_ellipse_geometry_GetBounds(ID2D1EllipseGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, D2D1_RECT_F *bounds)
{
    FIXME("iface %p, transform %p, bounds %p stub!\n", iface, transform, bounds);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_ellipse_geometry_GetWidenedBounds(ID2D1EllipseGeometry *iface,
        float stroke_width, ID2D1StrokeStyle *stroke_style, const D2D1_MATRIX_3X2_F *transform,
        float tolerance, D2D1_RECT_F *bounds)
{
    struct d2d_geometry *geometry = impl_from_ID2D1EllipseGeometry(iface);

    TRACE("iface %p, stroke_width %.8e, stroke_style %p, transform %p, tolerance %.8e, bounds %p.\n",
            iface, stroke_width, stroke_style, transform, tolerance, bounds);

    return d2d_geometry_get_widened_bounds(&geometry->ID2D1Geometry_iface, stroke_width, stroke_style, transform,
            tolerance, bounds);
}

static HRESULT STDMETHODCALLTYPE d2d_ellipse_geometry_StrokeContainsPoint(ID2D1EllipseGeometry *iface,
        D2D1_POINT_2F point, float stroke_width, ID2D1StrokeStyle *stroke_style, const D2D1_MATRIX_3X2_F *transform,
        float tolerance, BOOL *contains)
{
    FIXME("iface %p, point %s, stroke_width %.8e, stroke_style %p, transform %p, tolerance %.8e, contains %p stub!\n",
            iface, debug_d2d_point_2f(&point), stroke_width, stroke_style, transform, tolerance, contains);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_ellipse_geometry_FillContainsPoint(ID2D1EllipseGeometry *iface,
        D2D1_POINT_2F point, const D2D1_MATRIX_3X2_F *transform, float tolerance, BOOL *contains)
{
    FIXME("iface %p, point %s, transform %p, tolerance %.8e, contains %p stub!\n",
            iface, debug_d2d_point_2f(&point), transform, tolerance, contains);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_ellipse_geometry_CompareWithGeometry(ID2D1EllipseGeometry *iface,
        ID2D1Geometry *geometry, const D2D1_MATRIX_3X2_F *transform, float tolerance, D2D1_GEOMETRY_RELATION *relation)
{
    FIXME("iface %p, geometry %p, transform %p, tolerance %.8e, relation %p stub!\n",
            iface, geometry, transform, tolerance, relation);

    return E_NOTIMPL;
}

static void d2d_ellipse_to_segments(const D2D1_ELLIPSE *ellipse, D2D1_POINT_2F *start_point,
        D2D1_BEZIER_SEGMENT *segments)
{
    const float coeff = 4.0f * (M_SQRT2 - 1.0f) / 3.0f;
    D2D1_MATRIX_3X2_F m;
    unsigned int i;

    /* Use four Bézier segments to approximate a unit circle.
       Endpoints tangents are tangential to the circle. Endpoints and the midpoint
       are lying on the circle. */

    d2d_point_set(start_point, -1.0f, 0.0f);

    d2d_point_set(&segments[0].point1, -1.0f, -coeff);
    d2d_point_set(&segments[0].point2, -coeff, -1.0f);
    d2d_point_set(&segments[0].point3, 0.0f, -1.0f);

    d2d_point_set(&segments[1].point1, coeff, -1.0f);
    d2d_point_set(&segments[1].point2, 1.0f, -coeff);
    d2d_point_set(&segments[1].point3, 1.0f, 0.0f);

    d2d_point_set(&segments[2].point1, 1.0f, coeff);
    d2d_point_set(&segments[2].point2, coeff, 1.0f);
    d2d_point_set(&segments[2].point3, 0.0f, 1.0f);

    d2d_point_set(&segments[3].point1, -coeff, 1.0f);
    d2d_point_set(&segments[3].point2, -1.0f, coeff);
    d2d_point_set(&segments[3].point3, start_point->x, start_point->y);

    m._11 = ellipse->radiusX;
    m._12 = 0.0f;
    m._21 = 0.0f;
    m._22 = ellipse->radiusY;
    m._31 = ellipse->point.x;
    m._32 = ellipse->point.y;

    d2d_point_transform(start_point, &m, start_point->x, start_point->y);
    for (i = 0; i < 4; ++i)
    {
        d2d_point_transform(&segments[i].point1, &m, segments[i].point1.x, segments[i].point1.y);
        d2d_point_transform(&segments[i].point2, &m, segments[i].point2.x, segments[i].point2.y);
        d2d_point_transform(&segments[i].point3, &m, segments[i].point3.x, segments[i].point3.y);
    }
}

static HRESULT STDMETHODCALLTYPE d2d_ellipse_geometry_Simplify(ID2D1EllipseGeometry *iface,
        D2D1_GEOMETRY_SIMPLIFICATION_OPTION option, const D2D1_MATRIX_3X2_F *transform, float tolerance,
        ID2D1SimplifiedGeometrySink *sink)
{
    struct d2d_geometry *geometry = impl_from_ID2D1EllipseGeometry(iface);
    struct d2d_figure figure = { 0 };
    D2D1_BEZIER_SEGMENT segments[4];
    D2D1_POINT_2F start_point;

    TRACE("iface %p, option %#x, transform %p, tolerance %.8e, sink %p.\n",
            iface, option, transform, tolerance, sink);

    d2d_ellipse_to_segments(&geometry->u.ellipse.ellipse, &start_point, segments);

    if (!d2d_figure_begin(&figure, start_point, D2D1_FIGURE_BEGIN_FILLED))
        return E_OUTOFMEMORY;
    if (!d2d_figure_add_beziers(&figure, segments, ARRAY_SIZE(segments)))
    {
        d2d_figure_cleanup(&figure);
        return E_OUTOFMEMORY;
    }
    d2d_figure_end(&figure, D2D1_FIGURE_END_CLOSED);

    d2d_figure_simplify(&figure, option, transform, tolerance, sink);
    d2d_figure_cleanup(&figure);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_ellipse_geometry_Tessellate(ID2D1EllipseGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, ID2D1TessellationSink *sink)
{
    struct d2d_geometry *geometry = impl_from_ID2D1EllipseGeometry(iface);

    TRACE("iface %p, transform %p, tolerance %.8e, sink %p.\n", iface, transform, tolerance, sink);

    return d2d_geometry_tessellate(&geometry->ID2D1Geometry_iface, transform, tolerance, sink);
}

static HRESULT STDMETHODCALLTYPE d2d_ellipse_geometry_CombineWithGeometry(ID2D1EllipseGeometry *iface,
        ID2D1Geometry *geometry, D2D1_COMBINE_MODE combine_mode, const D2D1_MATRIX_3X2_F *transform,
        float tolerance, ID2D1SimplifiedGeometrySink *sink)
{
    struct d2d_geometry *self = impl_from_ID2D1EllipseGeometry(iface);

    TRACE("iface %p, geometry %p, combine_mode %#x, transform %p, tolerance %.8e, sink %p.\n",
            iface, geometry, combine_mode, transform, tolerance, sink);

    return d2d_geometry_combine(&self->ID2D1Geometry_iface, geometry, combine_mode, transform, tolerance, sink);
}

static HRESULT STDMETHODCALLTYPE d2d_ellipse_geometry_Outline(ID2D1EllipseGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, ID2D1SimplifiedGeometrySink *sink)
{
    FIXME("iface %p, transform %p, tolerance %.8e, sink %p stub!\n", iface, transform, tolerance, sink);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_ellipse_geometry_ComputeArea(ID2D1EllipseGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, float *area)
{
    struct d2d_geometry *geometry = impl_from_ID2D1EllipseGeometry(iface);

    TRACE("iface %p, transform %p, tolerance %.8e, area %p.\n", iface, transform, tolerance, area);

    return d2d_geometry_compute_area(&geometry->ID2D1Geometry_iface, transform, tolerance, area);
}

static HRESULT STDMETHODCALLTYPE d2d_ellipse_geometry_ComputeLength(ID2D1EllipseGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, float *length)
{
    FIXME("iface %p, transform %p, tolerance %.8e, length %p stub!\n", iface, transform, tolerance, length);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_ellipse_geometry_ComputePointAtLength(ID2D1EllipseGeometry *iface,
        float length, const D2D1_MATRIX_3X2_F *transform, float tolerance, D2D1_POINT_2F *point,
        D2D1_POINT_2F *tangent)
{
    FIXME("iface %p, length %.8e, transform %p, tolerance %.8e, point %p, tangent %p stub!\n",
            iface, length, transform, tolerance, point, tangent);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_ellipse_geometry_Widen(ID2D1EllipseGeometry *iface, float stroke_width,
        ID2D1StrokeStyle *stroke_style, const D2D1_MATRIX_3X2_F *transform, float tolerance,
        ID2D1SimplifiedGeometrySink *sink)
{
    struct d2d_geometry *geometry = impl_from_ID2D1EllipseGeometry(iface);

    TRACE("iface %p, stroke_width %.8e, stroke_style %p, transform %p, tolerance %.8e, sink %p.\n",
            iface, stroke_width, stroke_style, transform, tolerance, sink);

    return d2d_geometry_widen(&geometry->ID2D1Geometry_iface, stroke_width, stroke_style, transform, tolerance, sink);
}

static void STDMETHODCALLTYPE d2d_ellipse_geometry_GetEllipse(ID2D1EllipseGeometry *iface, D2D1_ELLIPSE *ellipse)
{
    struct d2d_geometry *geometry = impl_from_ID2D1EllipseGeometry(iface);

    TRACE("iface %p, ellipse %p.\n", iface, ellipse);

    *ellipse = geometry->u.ellipse.ellipse;
}

const struct ID2D1EllipseGeometryVtbl d2d_ellipse_geometry_vtbl =
{
    d2d_ellipse_geometry_QueryInterface,
    d2d_ellipse_geometry_AddRef,
    d2d_ellipse_geometry_Release,
    d2d_ellipse_geometry_GetFactory,
    d2d_ellipse_geometry_GetBounds,
    d2d_ellipse_geometry_GetWidenedBounds,
    d2d_ellipse_geometry_StrokeContainsPoint,
    d2d_ellipse_geometry_FillContainsPoint,
    d2d_ellipse_geometry_CompareWithGeometry,
    d2d_ellipse_geometry_Simplify,
    d2d_ellipse_geometry_Tessellate,
    d2d_ellipse_geometry_CombineWithGeometry,
    d2d_ellipse_geometry_Outline,
    d2d_ellipse_geometry_ComputeArea,
    d2d_ellipse_geometry_ComputeLength,
    d2d_ellipse_geometry_ComputePointAtLength,
    d2d_ellipse_geometry_Widen,
    d2d_ellipse_geometry_GetEllipse,
};

static void d2d_ellipse_geometry_stream(struct d2d_geometry *geometry, const D2D_MATRIX_3X2_F *transform,
        ID2D1GeometrySink *sink)
{
    const D2D1_ELLIPSE *e = &geometry->u.ellipse.ellipse;
    D2D1_POINT_2F start_point;
    D2D1_ARC_SEGMENT arcs[4];

    arcs[0].size.width = e->radiusX;
    arcs[0].size.height = e->radiusY;
    arcs[0].rotationAngle = 0.0f;
    arcs[0].sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
    arcs[0].arcSize = D2D1_ARC_SIZE_SMALL;
    arcs[1] = arcs[2] = arcs[3] = arcs[0];

    d2d_point_set(&start_point, e->point.x - e->radiusX, e->point.y);
    d2d_point_set(&arcs[0].point, e->point.x, e->point.y - e->radiusY);
    d2d_point_set(&arcs[1].point, e->point.x + e->radiusX, e->point.y);
    d2d_point_set(&arcs[2].point, e->point.x, e->point.y + e->radiusY);
    d2d_point_set(&arcs[3].point, start_point.x, start_point.y);

    if (transform)
    {
        d2d_point_transform(&start_point, transform, start_point.x, start_point.y);
        for (int i = 0; i < ARRAYSIZE(arcs); ++i)
            d2d_arc_transform(&arcs[i], transform);
    }

    ID2D1GeometrySink_BeginFigure(sink, start_point, D2D1_FIGURE_BEGIN_FILLED);

    for (int i = 0; i < ARRAYSIZE(arcs); ++i)
        ID2D1GeometrySink_AddArc(sink, &arcs[i]);

    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);
}

static const struct d2d_geometry_ops d2d_ellipse_geometry_ops =
{
    .stream = d2d_ellipse_geometry_stream,
};

HRESULT d2d_ellipse_geometry_init(struct d2d_geometry *geometry, ID2D1Factory *factory, const D2D1_ELLIPSE *ellipse)
{
    D2D1_POINT_2F *v, v1, v2, v3, v4;
    struct d2d_face *f;
    float l, r, t, b;

    d2d_geometry_init(geometry, factory, &identity, (ID2D1GeometryVtbl *)&d2d_ellipse_geometry_vtbl,
            &d2d_ellipse_geometry_ops);
    geometry->u.ellipse.ellipse = *ellipse;
    geometry->convex.valid = TRUE;
    geometry->convex.rect.left = ellipse->point.x - fabsf(ellipse->radiusX);
    geometry->convex.rect.right = ellipse->point.x + fabsf(ellipse->radiusX);
    geometry->convex.rect.top = ellipse->point.y - fabsf(ellipse->radiusY);
    geometry->convex.rect.bottom = ellipse->point.y + fabsf(ellipse->radiusY);
    geometry->convex.radius_x = fabsf(ellipse->radiusX);
    geometry->convex.radius_y = fabsf(ellipse->radiusY);

    if (!(geometry->fill.vertices = malloc(4 * sizeof(*geometry->fill.vertices))))
        goto fail;
    if (!d2d_array_reserve((void **)&geometry->fill.faces,
            &geometry->fill.faces_size, 2, sizeof(*geometry->fill.faces)))
        goto fail;

    l = ellipse->point.x - ellipse->radiusX;
    r = ellipse->point.x + ellipse->radiusX;
    t = ellipse->point.y - ellipse->radiusY;
    b = ellipse->point.y + ellipse->radiusY;

    d2d_point_set(&v1, r, t);
    d2d_point_set(&v2, r, b);
    d2d_point_set(&v3, l, b);
    d2d_point_set(&v4, l, t);

    v = geometry->fill.vertices;
    d2d_point_set(&v[0], ellipse->point.x, t);
    d2d_point_set(&v[1], r, ellipse->point.y);
    d2d_point_set(&v[2], ellipse->point.x, b);
    d2d_point_set(&v[3], l, ellipse->point.y);
    geometry->fill.vertex_count = 4;

    f = geometry->fill.faces;
    d2d_face_set(&f[0], 0, 3, 2);
    d2d_face_set(&f[1], 0, 2, 1);
    geometry->fill.face_count = 2;

    if (!d2d_geometry_fill_add_arc_triangle(geometry, &v[0], &v1, &v[1]))
        goto fail;
    if (!d2d_geometry_fill_add_arc_triangle(geometry, &v[1], &v2, &v[2]))
        goto fail;
    if (!d2d_geometry_fill_add_arc_triangle(geometry, &v[2], &v3, &v[3]))
        goto fail;
    if (!d2d_geometry_fill_add_arc_triangle(geometry, &v[3], &v4, &v[0]))
        goto fail;

    if (!d2d_geometry_outline_add_arc_quadrant(geometry, &v[0], &v1, &v[1]))
        goto fail;
    if (!d2d_geometry_outline_add_arc_quadrant(geometry, &v[1], &v2, &v[2]))
        goto fail;
    if (!d2d_geometry_outline_add_arc_quadrant(geometry, &v[2], &v3, &v[3]))
        goto fail;
    if (!d2d_geometry_outline_add_arc_quadrant(geometry, &v[3], &v4, &v[0]))
        goto fail;

    return S_OK;

fail:
    d2d_geometry_cleanup(geometry);
    return E_OUTOFMEMORY;
}

static inline struct d2d_geometry *impl_from_ID2D1RectangleGeometry(ID2D1RectangleGeometry *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_geometry, ID2D1Geometry_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_rectangle_geometry_QueryInterface(ID2D1RectangleGeometry *iface,
        REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1RectangleGeometry)
            || IsEqualGUID(iid, &IID_ID2D1Geometry)
            || IsEqualGUID(iid, &IID_ID2D1Resource)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID2D1RectangleGeometry_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_rectangle_geometry_AddRef(ID2D1RectangleGeometry *iface)
{
    struct d2d_geometry *geometry = impl_from_ID2D1RectangleGeometry(iface);
    ULONG refcount = InterlockedIncrement(&geometry->refcount);

    TRACE("%p increasing refcount to %lu.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_rectangle_geometry_Release(ID2D1RectangleGeometry *iface)
{
    struct d2d_geometry *geometry = impl_from_ID2D1RectangleGeometry(iface);
    ULONG refcount = InterlockedDecrement(&geometry->refcount);

    TRACE("%p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
    {
        d2d_geometry_cleanup(geometry);
        free(geometry);
    }

    return refcount;
}

static void STDMETHODCALLTYPE d2d_rectangle_geometry_GetFactory(ID2D1RectangleGeometry *iface, ID2D1Factory **factory)
{
    struct d2d_geometry *geometry = impl_from_ID2D1RectangleGeometry(iface);

    TRACE("iface %p, factory %p.\n", iface, factory);

    ID2D1Factory_AddRef(*factory = geometry->factory);
}

static HRESULT STDMETHODCALLTYPE d2d_rectangle_geometry_GetBounds(ID2D1RectangleGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, D2D1_RECT_F *bounds)
{
    struct d2d_geometry *geometry = impl_from_ID2D1RectangleGeometry(iface);
    D2D1_RECT_F *rect;
    D2D1_POINT_2F p;

    TRACE("iface %p, transform %p, bounds %p.\n", iface, transform, bounds);

    rect = &geometry->u.rectangle.rect;
    if (!transform)
    {
        *bounds = *rect;
        return S_OK;
    }

    bounds->left = FLT_MAX;
    bounds->top = FLT_MAX;
    bounds->right = -FLT_MAX;
    bounds->bottom = -FLT_MAX;

    d2d_point_transform(&p, transform, rect->left, rect->top);
    d2d_rect_expand(bounds, &p);
    d2d_point_transform(&p, transform, rect->left, rect->bottom);
    d2d_rect_expand(bounds, &p);
    d2d_point_transform(&p, transform, rect->right, rect->bottom);
    d2d_rect_expand(bounds, &p);
    d2d_point_transform(&p, transform, rect->right, rect->top);
    d2d_rect_expand(bounds, &p);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_rectangle_geometry_GetWidenedBounds(ID2D1RectangleGeometry *iface,
        float stroke_width, ID2D1StrokeStyle *stroke_style, const D2D1_MATRIX_3X2_F *transform,
        float tolerance, D2D1_RECT_F *bounds)
{
    struct d2d_geometry *geometry = impl_from_ID2D1RectangleGeometry(iface);

    TRACE("iface %p, stroke_width %.8e, stroke_style %p, transform %p, tolerance %.8e, bounds %p.\n",
            iface, stroke_width, stroke_style, transform, tolerance, bounds);

    return d2d_geometry_get_widened_bounds(&geometry->ID2D1Geometry_iface, stroke_width, stroke_style, transform,
            tolerance, bounds);
}

static HRESULT STDMETHODCALLTYPE d2d_rectangle_geometry_StrokeContainsPoint(ID2D1RectangleGeometry *iface,
        D2D1_POINT_2F point, float stroke_width, ID2D1StrokeStyle *stroke_style, const D2D1_MATRIX_3X2_F *transform,
        float tolerance, BOOL *contains)
{
    const struct d2d_geometry *geometry = impl_from_ID2D1RectangleGeometry(iface);
    const D2D1_RECT_F *rect = &geometry->u.rectangle.rect;
    unsigned int i;
    struct
    {
        D2D1_POINT_2F s, e;
    }
    segments[4];

    TRACE("iface %p, point %s, stroke_width %.8e, stroke_style %p, transform %p, tolerance %.8e, contains %p.\n",
            iface, debug_d2d_point_2f(&point), stroke_width, stroke_style, transform, tolerance, contains);

    if (stroke_style)
        FIXME("Ignoring stroke style %p.\n", stroke_style);

    tolerance = fabsf(tolerance);

    if (!transform)
    {
        D2D1_POINT_2F d, s;

        s.x = rect->right - rect->left;
        s.y = rect->bottom - rect->top;
        d.x = fabsf((rect->right + rect->left) * 0.5f - point.x);
        d.y = fabsf((rect->bottom + rect->top) * 0.5f - point.y);

        /* Inside test. */
        if (d.x <= (s.x - stroke_width) * 0.5f - tolerance && d.y <= (s.y - stroke_width) * 0.5f - tolerance)
        {
            *contains = FALSE;
            return S_OK;
        }

        if (tolerance == 0.0f)
        {
            *contains = d.x < (s.x + stroke_width) * 0.5f && d.y < (s.y + stroke_width) * 0.5f;
        }
        else
        {
            d.x = max(d.x - (s.x + stroke_width) * 0.5f, 0.0f);
            d.y = max(d.y - (s.y + stroke_width) * 0.5f, 0.0f);

            *contains = d2d_point_dot(&d, &d) < tolerance * tolerance;
        }

        return S_OK;
    }

    stroke_width *= 0.5f;

    d2d_point_set(&segments[0].s, rect->left - stroke_width, rect->bottom);
    d2d_point_set(&segments[0].e, rect->right + stroke_width, rect->bottom);
    d2d_point_set(&segments[1].s, rect->right, rect->bottom + stroke_width);
    d2d_point_set(&segments[1].e, rect->right, rect->top - stroke_width);
    d2d_point_set(&segments[2].s, rect->right + stroke_width, rect->top);
    d2d_point_set(&segments[2].e, rect->left - stroke_width, rect->top);
    d2d_point_set(&segments[3].s, rect->left, rect->top - stroke_width);
    d2d_point_set(&segments[3].e, rect->left, rect->bottom + stroke_width);

    *contains = FALSE;
    for (i = 0; i < ARRAY_SIZE(segments); ++i)
    {
        if (d2d_point_on_line_segment(&point, &segments[i].s, &segments[i].e, transform, stroke_width, tolerance))
        {
            *contains = TRUE;
            break;
        }
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_rectangle_geometry_FillContainsPoint(ID2D1RectangleGeometry *iface,
        D2D1_POINT_2F point, const D2D1_MATRIX_3X2_F *transform, float tolerance, BOOL *contains)
{
    struct d2d_geometry *geometry = impl_from_ID2D1RectangleGeometry(iface);
    D2D1_RECT_F *rect = &geometry->u.rectangle.rect;
    float dx, dy;

    TRACE("iface %p, point %s, transform %p, tolerance %.8e, contains %p.\n",
            iface, debug_d2d_point_2f(&point), transform, tolerance, contains);

    if (transform)
    {
        D2D1_MATRIX_3X2_F g_i;

        if (!d2d_matrix_invert(&g_i, transform))
            return D2DERR_UNSUPPORTED_OPERATION;
        d2d_point_transform(&point, &g_i, point.x, point.y);
    }

    if (tolerance == 0.0f)
        tolerance = D2D1_DEFAULT_FLATTENING_TOLERANCE;

    dx = max(fabsf((rect->right  + rect->left) / 2.0f - point.x) - (rect->right  - rect->left) / 2.0f, 0.0f);
    dy = max(fabsf((rect->bottom + rect->top)  / 2.0f - point.y) - (rect->bottom - rect->top)  / 2.0f, 0.0f);

    *contains = tolerance * tolerance > (dx * dx + dy * dy);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_rectangle_geometry_CompareWithGeometry(ID2D1RectangleGeometry *iface,
        ID2D1Geometry *geometry, const D2D1_MATRIX_3X2_F *transform, float tolerance, D2D1_GEOMETRY_RELATION *relation)
{
    FIXME("iface %p, geometry %p, transform %p, tolerance %.8e, relation %p stub!\n",
            iface, geometry, transform, tolerance, relation);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_rectangle_geometry_Simplify(ID2D1RectangleGeometry *iface,
        D2D1_GEOMETRY_SIMPLIFICATION_OPTION option, const D2D1_MATRIX_3X2_F *transform, float tolerance,
        ID2D1SimplifiedGeometrySink *sink)
{
    struct d2d_geometry *geometry = impl_from_ID2D1RectangleGeometry(iface);
    D2D1_RECT_F *rect = &geometry->u.rectangle.rect;
    D2D1_POINT_2F p[4];
    unsigned int i;

    TRACE("iface %p, option %#x, transform %p, tolerance %.8e, sink %p.\n",
            iface, option, transform, tolerance, sink);

    d2d_point_set(&p[0], rect->left, rect->top);
    d2d_point_set(&p[1], rect->right, rect->top);
    d2d_point_set(&p[2], rect->right, rect->bottom);
    d2d_point_set(&p[3], rect->left, rect->bottom);

    if (transform)
    {
        for (i = 0; i < ARRAY_SIZE(p); ++i)
        {
            d2d_point_transform(&p[i], transform, p[i].x, p[i].y);
        }
    }

    ID2D1SimplifiedGeometrySink_SetFillMode(sink, D2D1_FILL_MODE_ALTERNATE);
    ID2D1SimplifiedGeometrySink_BeginFigure(sink, p[0], D2D1_FIGURE_BEGIN_FILLED);
    ID2D1SimplifiedGeometrySink_AddLines(sink, &p[1], 3);
    ID2D1SimplifiedGeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_rectangle_geometry_Tessellate(ID2D1RectangleGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, ID2D1TessellationSink *sink)
{
    struct d2d_geometry *geometry = impl_from_ID2D1RectangleGeometry(iface);

    TRACE("iface %p, transform %p, tolerance %.8e, sink %p.\n", iface, transform, tolerance, sink);

    return d2d_geometry_tessellate(&geometry->ID2D1Geometry_iface, transform, tolerance, sink);
}

static HRESULT STDMETHODCALLTYPE d2d_rectangle_geometry_CombineWithGeometry(ID2D1RectangleGeometry *iface,
        ID2D1Geometry *geometry, D2D1_COMBINE_MODE combine_mode, const D2D1_MATRIX_3X2_F *transform,
        float tolerance, ID2D1SimplifiedGeometrySink *sink)
{
    struct d2d_geometry *self = impl_from_ID2D1RectangleGeometry(iface);

    TRACE("iface %p, geometry %p, combine_mode %#x, transform %p, tolerance %.8e, sink %p.\n",
            iface, geometry, combine_mode, transform, tolerance, sink);

    return d2d_geometry_combine(&self->ID2D1Geometry_iface, geometry, combine_mode, transform, tolerance, sink);
}

static HRESULT STDMETHODCALLTYPE d2d_rectangle_geometry_Outline(ID2D1RectangleGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, ID2D1SimplifiedGeometrySink *sink)
{
    FIXME("iface %p, transform %p, tolerance %.8e, sink %p stub!\n", iface, transform, tolerance, sink);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_rectangle_geometry_ComputeArea(ID2D1RectangleGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, float *area)
{
    struct d2d_geometry *geometry = impl_from_ID2D1RectangleGeometry(iface);
    const D2D_RECT_F *rect = &geometry->u.rectangle.rect;
    D2D1_TRIANGLE triangle;
    D2D1_MATRIX_3X2_F m;

    TRACE("iface %p, transform %p, tolerance %.8e, area %p.\n", iface, transform, tolerance, area);

    if (transform)
    {
        m = *transform;
        m._31 = m._32 = 0.0f;

        d2d_point_transform(&triangle.point1, &m, rect->left, rect->bottom);
        d2d_point_transform(&triangle.point2, &m, rect->left, rect->top);
        d2d_point_transform(&triangle.point3, &m, rect->right, rect->top);

        *area = 2 * d2d_triangle_area(&triangle);
    }
    else
    {
        *area = fabsf((rect->right - rect->left) * (rect->bottom - rect->top));
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_rectangle_geometry_ComputeLength(ID2D1RectangleGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, float *length)
{
    FIXME("iface %p, transform %p, tolerance %.8e, length %p stub!\n", iface, transform, tolerance, length);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_rectangle_geometry_ComputePointAtLength(ID2D1RectangleGeometry *iface,
        float length, const D2D1_MATRIX_3X2_F *transform, float tolerance, D2D1_POINT_2F *point,
        D2D1_POINT_2F *tangent)
{
    FIXME("iface %p, length %.8e, transform %p, tolerance %.8e, point %p, tangent %p stub!\n",
            iface, length, transform, tolerance, point, tangent);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_rectangle_geometry_Widen(ID2D1RectangleGeometry *iface, float stroke_width,
        ID2D1StrokeStyle *stroke_style, const D2D1_MATRIX_3X2_F *transform, float tolerance,
        ID2D1SimplifiedGeometrySink *sink)
{
    struct d2d_geometry *geometry = impl_from_ID2D1RectangleGeometry(iface);

    TRACE("iface %p, stroke_width %.8e, stroke_style %p, transform %p, tolerance %.8e, sink %p.\n",
            iface, stroke_width, stroke_style, transform, tolerance, sink);

    return d2d_geometry_widen(&geometry->ID2D1Geometry_iface, stroke_width, stroke_style, transform, tolerance, sink);
}

static void STDMETHODCALLTYPE d2d_rectangle_geometry_GetRect(ID2D1RectangleGeometry *iface, D2D1_RECT_F *rect)
{
    struct d2d_geometry *geometry = impl_from_ID2D1RectangleGeometry(iface);

    TRACE("iface %p, rect %p.\n", iface, rect);

    *rect = geometry->u.rectangle.rect;
}

const struct ID2D1RectangleGeometryVtbl d2d_rectangle_geometry_vtbl =
{
    d2d_rectangle_geometry_QueryInterface,
    d2d_rectangle_geometry_AddRef,
    d2d_rectangle_geometry_Release,
    d2d_rectangle_geometry_GetFactory,
    d2d_rectangle_geometry_GetBounds,
    d2d_rectangle_geometry_GetWidenedBounds,
    d2d_rectangle_geometry_StrokeContainsPoint,
    d2d_rectangle_geometry_FillContainsPoint,
    d2d_rectangle_geometry_CompareWithGeometry,
    d2d_rectangle_geometry_Simplify,
    d2d_rectangle_geometry_Tessellate,
    d2d_rectangle_geometry_CombineWithGeometry,
    d2d_rectangle_geometry_Outline,
    d2d_rectangle_geometry_ComputeArea,
    d2d_rectangle_geometry_ComputeLength,
    d2d_rectangle_geometry_ComputePointAtLength,
    d2d_rectangle_geometry_Widen,
    d2d_rectangle_geometry_GetRect,
};

static void d2d_rectangle_stream(const D2D1_RECT_F *rect, const D2D1_MATRIX_3X2_F *transform,
        ID2D1GeometrySink *sink)
{
    D2D1_POINT_2F p[4];
    unsigned int i;

    d2d_point_set(&p[0], rect->left, rect->top);
    d2d_point_set(&p[1], rect->right, rect->top);
    d2d_point_set(&p[2], rect->right, rect->bottom);
    d2d_point_set(&p[3], rect->left, rect->bottom);

    if (transform)
    {
        for (i = 0; i < ARRAY_SIZE(p); ++i)
        {
            d2d_point_transform(&p[i], transform, p[i].x, p[i].y);
        }
    }

    ID2D1GeometrySink_BeginFigure(sink, p[0], D2D1_FIGURE_BEGIN_FILLED);
    ID2D1GeometrySink_AddLines(sink, &p[1], 3);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);
}

static void d2d_rectangle_geometry_stream(struct d2d_geometry *geometry,
        const D2D_MATRIX_3X2_F *transform, ID2D1GeometrySink *sink)
{
    d2d_rectangle_stream(&geometry->u.rectangle.rect, transform, sink);
}

static const struct d2d_geometry_ops d2d_rectangle_geometry_ops =
{
    .stream = d2d_rectangle_geometry_stream,
};

HRESULT d2d_rectangle_geometry_init(struct d2d_geometry *geometry, ID2D1Factory *factory, const D2D1_RECT_F *rect)
{
    struct d2d_face *f;
    D2D1_POINT_2F *v;
    float l, r, t, b;

    static const D2D1_POINT_2F prev[] =
    {
        { 1.0f,  0.0f},
        { 0.0f, -1.0f},
        {-1.0f,  0.0f},
        { 0.0f,  1.0f},
    };
    static const D2D1_POINT_2F next[] =
    {
        { 0.0f,  1.0f},
        { 1.0f,  0.0f},
        { 0.0f, -1.0f},
        {-1.0f,  0.0f},
    };

    d2d_geometry_init(geometry, factory, &identity, (ID2D1GeometryVtbl *)&d2d_rectangle_geometry_vtbl,
            &d2d_rectangle_geometry_ops);
    geometry->u.rectangle.rect = *rect;
    geometry->convex.valid = TRUE;
    geometry->convex.rect.left = min(rect->left, rect->right);
    geometry->convex.rect.right = max(rect->left, rect->right);
    geometry->convex.rect.top = min(rect->top, rect->bottom);
    geometry->convex.rect.bottom = max(rect->top, rect->bottom);

    if (!(geometry->fill.vertices = malloc(4 * sizeof(*geometry->fill.vertices))))
        goto fail;
    if (!d2d_array_reserve((void **)&geometry->fill.faces,
            &geometry->fill.faces_size, 2, sizeof(*geometry->fill.faces)))
        goto fail;

    l = min(rect->left, rect->right);
    r = max(rect->left, rect->right);
    t = min(rect->top, rect->bottom);
    b = max(rect->top, rect->bottom);

    v = geometry->fill.vertices;
    d2d_point_set(&v[0], l, t);
    d2d_point_set(&v[1], l, b);
    d2d_point_set(&v[2], r, b);
    d2d_point_set(&v[3], r, t);
    geometry->fill.vertex_count = 4;

    f = geometry->fill.faces;
    d2d_face_set(&f[0], 1, 2, 0);
    d2d_face_set(&f[1], 0, 2, 3);
    geometry->fill.face_count = 2;

    if (!d2d_geometry_outline_add_line_segment(geometry, &v[0], &v[1]))
        goto fail;
    if (!d2d_geometry_outline_add_line_segment(geometry, &v[1], &v[2]))
        goto fail;
    if (!d2d_geometry_outline_add_line_segment(geometry, &v[2], &v[3]))
        goto fail;
    if (!d2d_geometry_outline_add_line_segment(geometry, &v[3], &v[0]))
        goto fail;

    if (!d2d_geometry_outline_add_join(geometry, &prev[0], &v[0], &next[0]))
        goto fail;
    if (!d2d_geometry_outline_add_join(geometry, &prev[1], &v[1], &next[1]))
        goto fail;
    if (!d2d_geometry_outline_add_join(geometry, &prev[2], &v[2], &next[2]))
        goto fail;
    if (!d2d_geometry_outline_add_join(geometry, &prev[3], &v[3], &next[3]))
        goto fail;

    return S_OK;

fail:
    d2d_geometry_cleanup(geometry);
    return E_OUTOFMEMORY;
}

static inline struct d2d_geometry *impl_from_ID2D1RoundedRectangleGeometry(ID2D1RoundedRectangleGeometry *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_geometry, ID2D1Geometry_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_rounded_rectangle_geometry_QueryInterface(ID2D1RoundedRectangleGeometry *iface,
        REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1RoundedRectangleGeometry)
            || IsEqualGUID(iid, &IID_ID2D1Geometry)
            || IsEqualGUID(iid, &IID_ID2D1Resource)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID2D1RoundedRectangleGeometry_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_rounded_rectangle_geometry_AddRef(ID2D1RoundedRectangleGeometry *iface)
{
    struct d2d_geometry *geometry = impl_from_ID2D1RoundedRectangleGeometry(iface);
    ULONG refcount = InterlockedIncrement(&geometry->refcount);

    TRACE("%p increasing refcount to %lu.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_rounded_rectangle_geometry_Release(ID2D1RoundedRectangleGeometry *iface)
{
    struct d2d_geometry *geometry = impl_from_ID2D1RoundedRectangleGeometry(iface);
    ULONG refcount = InterlockedDecrement(&geometry->refcount);

    TRACE("%p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
    {
        d2d_geometry_cleanup(geometry);
        free(geometry);
    }

    return refcount;
}

static void STDMETHODCALLTYPE d2d_rounded_rectangle_geometry_GetFactory(ID2D1RoundedRectangleGeometry *iface,
        ID2D1Factory **factory)
{
    struct d2d_geometry *geometry = impl_from_ID2D1RoundedRectangleGeometry(iface);

    TRACE("iface %p, factory %p.\n", iface, factory);

    ID2D1Factory_AddRef(*factory = geometry->factory);
}

static HRESULT STDMETHODCALLTYPE d2d_rounded_rectangle_geometry_GetBounds(ID2D1RoundedRectangleGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, D2D1_RECT_F *bounds)
{
    FIXME("iface %p, transform %p, bounds %p stub!\n", iface, transform, bounds);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_rounded_rectangle_geometry_GetWidenedBounds(ID2D1RoundedRectangleGeometry *iface,
        float stroke_width, ID2D1StrokeStyle *stroke_style, const D2D1_MATRIX_3X2_F *transform,
        float tolerance, D2D1_RECT_F *bounds)
{
    struct d2d_geometry *geometry = impl_from_ID2D1RoundedRectangleGeometry(iface);

    TRACE("iface %p, stroke_width %.8e, stroke_style %p, transform %p, tolerance %.8e, bounds %p.\n",
            iface, stroke_width, stroke_style, transform, tolerance, bounds);

    return d2d_geometry_get_widened_bounds(&geometry->ID2D1Geometry_iface, stroke_width, stroke_style, transform,
            tolerance, bounds);
}

static HRESULT STDMETHODCALLTYPE d2d_rounded_rectangle_geometry_StrokeContainsPoint(
        ID2D1RoundedRectangleGeometry *iface, D2D1_POINT_2F point, float stroke_width,
        ID2D1StrokeStyle *stroke_style, const D2D1_MATRIX_3X2_F *transform, float tolerance, BOOL *contains)
{
    FIXME("iface %p, point %s, stroke_width %.8e, stroke_style %p, transform %p, tolerance %.8e, contains %p stub!\n",
            iface, debug_d2d_point_2f(&point), stroke_width, stroke_style, transform, tolerance, contains);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_rounded_rectangle_geometry_FillContainsPoint(ID2D1RoundedRectangleGeometry *iface,
        D2D1_POINT_2F point, const D2D1_MATRIX_3X2_F *transform, float tolerance, BOOL *contains)
{
    FIXME("iface %p, point %s, transform %p, tolerance %.8e, contains %p stub!\n",
            iface, debug_d2d_point_2f(&point), transform, tolerance, contains);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_rounded_rectangle_geometry_CompareWithGeometry(
        ID2D1RoundedRectangleGeometry *iface, ID2D1Geometry *geometry,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, D2D1_GEOMETRY_RELATION *relation)
{
    FIXME("iface %p, geometry %p, transform %p, tolerance %.8e, relation %p stub!\n",
            iface, geometry, transform, tolerance, relation);

    return E_NOTIMPL;
}

static inline void d2d_point_translate(D2D1_POINT_2F *point, float x, float y)
{
    point->x += x;
    point->y += y;
}

static inline void d2d_bezier_segment_translate(D2D1_BEZIER_SEGMENT *segment, float x, float y)
{
    d2d_point_translate(&segment->point1, x, y);
    d2d_point_translate(&segment->point2, x, y);
    d2d_point_translate(&segment->point3, x, y);
}

static HRESULT STDMETHODCALLTYPE d2d_rounded_rectangle_geometry_Simplify(ID2D1RoundedRectangleGeometry *iface,
        D2D1_GEOMETRY_SIMPLIFICATION_OPTION option, const D2D1_MATRIX_3X2_F *transform, float tolerance,
        ID2D1SimplifiedGeometrySink *sink)
{
    struct d2d_geometry *geometry = impl_from_ID2D1RoundedRectangleGeometry(iface);
    const D2D1_ROUNDED_RECT *r = &geometry->u.rounded_rectangle.rounded_rect;
    struct d2d_figure figure = { 0 };
    D2D1_BEZIER_SEGMENT segments[4];
    D2D1_POINT_2F start_point, p;
    D2D1_ELLIPSE ellipse;
    bool ret;

    TRACE("iface %p, option %#x, transform %p, tolerance %.8e, sink %p.\n",
            iface, option, transform, tolerance, sink);

    d2d_point_set(&ellipse.point, 0.0f, 0.0f);
    ellipse.radiusX = r->radiusX;
    ellipse.radiusY = r->radiusY;

    d2d_ellipse_to_segments(&ellipse, &start_point, segments);

    d2d_point_set(&p, r->rect.left + r->radiusX, r->rect.bottom + r->radiusY);
    d2d_point_translate(&start_point, p.x, p.y);
    d2d_bezier_segment_translate(&segments[0], p.x, p.y);
    d2d_point_set(&p, r->rect.right - r->radiusX, r->rect.bottom + r->radiusY);
    d2d_bezier_segment_translate(&segments[1], p.x, p.y);
    d2d_point_set(&p, r->rect.right - r->radiusX, r->rect.top - r->radiusY);
    d2d_bezier_segment_translate(&segments[2], p.x, p.y);
    d2d_point_set(&p, r->rect.left + r->radiusX, r->rect.top - r->radiusY);
    d2d_bezier_segment_translate(&segments[3], p.x, p.y);

    ret = d2d_figure_begin(&figure, start_point, D2D1_FIGURE_BEGIN_FILLED);
    ret = ret && d2d_figure_add_beziers(&figure, &segments[0], 1);
    d2d_point_set(&p, r->rect.right - r->radiusX, r->rect.bottom);
    ret = ret && d2d_figure_add_lines(&figure, &p, 1);
    ret = ret && d2d_figure_add_beziers(&figure, &segments[1], 1);
    d2d_point_set(&p, r->rect.right, r->rect.top - r->radiusY);
    ret = ret && d2d_figure_add_lines(&figure, &p, 1);
    ret = ret && d2d_figure_add_beziers(&figure, &segments[2], 1);
    d2d_point_set(&p, r->rect.left + r->radiusX, r->rect.top);
    ret = ret && d2d_figure_add_lines(&figure, &p, 1);
    ret = ret && d2d_figure_add_beziers(&figure, &segments[3], 1);
    if (!ret)
    {
        d2d_figure_cleanup(&figure);
        return E_OUTOFMEMORY;
    }

    d2d_figure_end(&figure, D2D1_FIGURE_END_CLOSED);

    d2d_figure_simplify(&figure, option, transform, tolerance, sink);
    d2d_figure_cleanup(&figure);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_rounded_rectangle_geometry_Tessellate(ID2D1RoundedRectangleGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, ID2D1TessellationSink *sink)
{
    struct d2d_geometry *geometry = impl_from_ID2D1RoundedRectangleGeometry(iface);

    TRACE("iface %p, transform %p, tolerance %.8e, sink %p.\n", iface, transform, tolerance, sink);

    return d2d_geometry_tessellate(&geometry->ID2D1Geometry_iface, transform, tolerance, sink);
}

static HRESULT STDMETHODCALLTYPE d2d_rounded_rectangle_geometry_CombineWithGeometry(
        ID2D1RoundedRectangleGeometry *iface, ID2D1Geometry *geometry, D2D1_COMBINE_MODE combine_mode,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, ID2D1SimplifiedGeometrySink *sink)
{
    struct d2d_geometry *self = impl_from_ID2D1RoundedRectangleGeometry(iface);

    TRACE("iface %p, geometry %p, combine_mode %#x, transform %p, tolerance %.8e, sink %p.\n",
            iface, geometry, combine_mode, transform, tolerance, sink);

    return d2d_geometry_combine(&self->ID2D1Geometry_iface, geometry, combine_mode, transform, tolerance, sink);
}

static HRESULT STDMETHODCALLTYPE d2d_rounded_rectangle_geometry_Outline(ID2D1RoundedRectangleGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, ID2D1SimplifiedGeometrySink *sink)
{
    FIXME("iface %p, transform %p, tolerance %.8e, sink %p stub!\n", iface, transform, tolerance, sink);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_rounded_rectangle_geometry_ComputeArea(ID2D1RoundedRectangleGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, float *area)
{
    struct d2d_geometry *geometry = impl_from_ID2D1RoundedRectangleGeometry(iface);

    TRACE("iface %p, transform %p, tolerance %.8e, area %p.\n", iface, transform, tolerance, area);

    return d2d_geometry_compute_area(&geometry->ID2D1Geometry_iface, transform, tolerance, area);
}

static HRESULT STDMETHODCALLTYPE d2d_rounded_rectangle_geometry_ComputeLength(ID2D1RoundedRectangleGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, float *length)
{
    FIXME("iface %p, transform %p, tolerance %.8e, length %p stub!\n", iface, transform, tolerance, length);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_rounded_rectangle_geometry_ComputePointAtLength(
        ID2D1RoundedRectangleGeometry *iface, float length, const D2D1_MATRIX_3X2_F *transform,
        float tolerance, D2D1_POINT_2F *point, D2D1_POINT_2F *tangent)
{
    FIXME("iface %p, length %.8e, transform %p, tolerance %.8e, point %p, tangent %p stub!\n",
            iface, length, transform, tolerance, point, tangent);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_rounded_rectangle_geometry_Widen(ID2D1RoundedRectangleGeometry *iface,
        float stroke_width, ID2D1StrokeStyle *stroke_style, const D2D1_MATRIX_3X2_F *transform,
        float tolerance, ID2D1SimplifiedGeometrySink *sink)
{
    struct d2d_geometry *geometry = impl_from_ID2D1RoundedRectangleGeometry(iface);

    TRACE("iface %p, stroke_width %.8e, stroke_style %p, transform %p, tolerance %.8e, sink %p.\n",
            iface, stroke_width, stroke_style, transform, tolerance, sink);

    return d2d_geometry_widen(&geometry->ID2D1Geometry_iface, stroke_width, stroke_style, transform, tolerance, sink);
}

static void STDMETHODCALLTYPE d2d_rounded_rectangle_geometry_GetRoundedRect(ID2D1RoundedRectangleGeometry *iface,
        D2D1_ROUNDED_RECT *rounded_rect)
{
    struct d2d_geometry *geometry = impl_from_ID2D1RoundedRectangleGeometry(iface);

    TRACE("iface %p, rounded_rect %p.\n", iface, rounded_rect);

    *rounded_rect = geometry->u.rounded_rectangle.rounded_rect;
}

const struct ID2D1RoundedRectangleGeometryVtbl d2d_rounded_rectangle_geometry_vtbl =
{
    d2d_rounded_rectangle_geometry_QueryInterface,
    d2d_rounded_rectangle_geometry_AddRef,
    d2d_rounded_rectangle_geometry_Release,
    d2d_rounded_rectangle_geometry_GetFactory,
    d2d_rounded_rectangle_geometry_GetBounds,
    d2d_rounded_rectangle_geometry_GetWidenedBounds,
    d2d_rounded_rectangle_geometry_StrokeContainsPoint,
    d2d_rounded_rectangle_geometry_FillContainsPoint,
    d2d_rounded_rectangle_geometry_CompareWithGeometry,
    d2d_rounded_rectangle_geometry_Simplify,
    d2d_rounded_rectangle_geometry_Tessellate,
    d2d_rounded_rectangle_geometry_CombineWithGeometry,
    d2d_rounded_rectangle_geometry_Outline,
    d2d_rounded_rectangle_geometry_ComputeArea,
    d2d_rounded_rectangle_geometry_ComputeLength,
    d2d_rounded_rectangle_geometry_ComputePointAtLength,
    d2d_rounded_rectangle_geometry_Widen,
    d2d_rounded_rectangle_geometry_GetRoundedRect,
};

static void d2d_rounded_rectangle_geometry_stream(struct d2d_geometry *geometry,
        const D2D_MATRIX_3X2_F *transform, ID2D1GeometrySink *sink)
{
    const D2D1_ROUNDED_RECT *r = &geometry->u.rounded_rectangle.rounded_rect;
    D2D1_ARC_SEGMENT arcs[4];
    D2D1_POINT_2F points[4];

    if (r->radiusX == 0.0f || r->radiusY == 0.0f)
        return d2d_rectangle_stream(&r->rect, transform, sink);

    arcs[0].size.width = r->radiusX;
    arcs[0].size.height = r->radiusY;
    arcs[0].rotationAngle = 90.0f;
    arcs[0].sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
    arcs[0].arcSize = D2D1_ARC_SIZE_SMALL;
    arcs[1] = arcs[2] = arcs[3] = arcs[0];

    d2d_point_set(&points[0], r->rect.left, r->rect.top - r->radiusY);
    d2d_point_set(&points[1], r->rect.right - r->radiusX, r->rect.top);
    d2d_point_set(&points[2], r->rect.right, r->rect.bottom - r->radiusY);
    d2d_point_set(&points[3], r->rect.left - r->radiusX, r->rect.bottom);

    d2d_point_set(&arcs[0].point, r->rect.left - r->radiusX, r->rect.top);
    d2d_point_set(&arcs[1].point, r->rect.right, r->rect.top - r->radiusY);
    d2d_point_set(&arcs[2].point, r->rect.right - r->radiusX, r->rect.bottom);
    d2d_point_set(&arcs[3].point, r->rect.left, r->rect.bottom - r->radiusY);

    if (transform)
    {
        for (int i = 0; i < ARRAYSIZE(points); ++i)
            d2d_point_transform(&points[i], transform, points[i].x, points[i].y);
        for (int i = 0; i < ARRAYSIZE(arcs); ++i)
            d2d_arc_transform(&arcs[i], transform);
    }

    ID2D1GeometrySink_BeginFigure(sink, points[0], D2D1_FIGURE_BEGIN_FILLED);

    ID2D1GeometrySink_AddArc(sink, &arcs[0]);
    ID2D1GeometrySink_AddLine(sink, points[1]);
    ID2D1GeometrySink_AddArc(sink, &arcs[1]);
    ID2D1GeometrySink_AddLine(sink, points[2]);
    ID2D1GeometrySink_AddArc(sink, &arcs[2]);
    ID2D1GeometrySink_AddLine(sink, points[3]);
    ID2D1GeometrySink_AddArc(sink, &arcs[3]);

    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);
}

static const struct d2d_geometry_ops d2d_rounded_rectangle_geometry_ops =
{
    .stream = d2d_rounded_rectangle_geometry_stream,
};

HRESULT d2d_rounded_rectangle_geometry_init(struct d2d_geometry *geometry,
        ID2D1Factory *factory, const D2D1_ROUNDED_RECT *rounded_rect)
{
    D2D1_POINT_2F *v, v1, v2, v3, v4;
    struct d2d_face *f;
    float l, r, t, b;
    float rx, ry;

    d2d_geometry_init(geometry, factory, &identity, (ID2D1GeometryVtbl *)&d2d_rounded_rectangle_geometry_vtbl,
            &d2d_rounded_rectangle_geometry_ops);
    geometry->u.rounded_rectangle.rounded_rect = *rounded_rect;
    geometry->convex.valid = TRUE;
    geometry->convex.rect.left = min(rounded_rect->rect.left, rounded_rect->rect.right);
    geometry->convex.rect.right = max(rounded_rect->rect.left, rounded_rect->rect.right);
    geometry->convex.rect.top = min(rounded_rect->rect.top, rounded_rect->rect.bottom);
    geometry->convex.rect.bottom = max(rounded_rect->rect.top, rounded_rect->rect.bottom);
    geometry->convex.radius_x = min(fabsf(rounded_rect->radiusX),
            0.5f * (geometry->convex.rect.right - geometry->convex.rect.left));
    geometry->convex.radius_y = min(fabsf(rounded_rect->radiusY),
            0.5f * (geometry->convex.rect.bottom - geometry->convex.rect.top));

    if (!(geometry->fill.vertices = malloc(8 * sizeof(*geometry->fill.vertices))))
        goto fail;
    if (!d2d_array_reserve((void **)&geometry->fill.faces,
            &geometry->fill.faces_size, 6, sizeof(*geometry->fill.faces)))
        goto fail;

    l = min(rounded_rect->rect.left, rounded_rect->rect.right);
    r = max(rounded_rect->rect.left, rounded_rect->rect.right);
    t = min(rounded_rect->rect.top, rounded_rect->rect.bottom);
    b = max(rounded_rect->rect.top, rounded_rect->rect.bottom);

    rx = min(rounded_rect->radiusX, 0.5f * (r - l));
    ry = min(rounded_rect->radiusY, 0.5f * (b - t));

    d2d_point_set(&v1, r, t);
    d2d_point_set(&v2, r, b);
    d2d_point_set(&v3, l, b);
    d2d_point_set(&v4, l, t);

    v = geometry->fill.vertices;
    d2d_point_set(&v[0], l + rx, t);
    d2d_point_set(&v[1], r - rx, t);
    d2d_point_set(&v[2], r, t + ry);
    d2d_point_set(&v[3], r, b - ry);
    d2d_point_set(&v[4], r - rx, b);
    d2d_point_set(&v[5], l + rx, b);
    d2d_point_set(&v[6], l, b - ry);
    d2d_point_set(&v[7], l, t + ry);
    geometry->fill.vertex_count = 8;

    f = geometry->fill.faces;
    d2d_face_set(&f[0], 0, 7, 6);
    d2d_face_set(&f[1], 0, 6, 5);
    d2d_face_set(&f[2], 0, 5, 4);
    d2d_face_set(&f[3], 0, 4, 1);
    d2d_face_set(&f[4], 1, 4, 3);
    d2d_face_set(&f[5], 1, 3, 2);
    geometry->fill.face_count = 6;

    if (!d2d_geometry_fill_add_arc_triangle(geometry, &v[1], &v1, &v[2]))
        goto fail;
    if (!d2d_geometry_fill_add_arc_triangle(geometry, &v[3], &v2, &v[4]))
        goto fail;
    if (!d2d_geometry_fill_add_arc_triangle(geometry, &v[5], &v3, &v[6]))
        goto fail;
    if (!d2d_geometry_fill_add_arc_triangle(geometry, &v[7], &v4, &v[0]))
        goto fail;

    if (!d2d_geometry_outline_add_line_segment(geometry, &v[0], &v[1]))
        goto fail;
    if (!d2d_geometry_outline_add_arc_quadrant(geometry, &v[1], &v1, &v[2]))
        goto fail;
    if (!d2d_geometry_outline_add_line_segment(geometry, &v[2], &v[3]))
        goto fail;
    if (!d2d_geometry_outline_add_arc_quadrant(geometry, &v[3], &v2, &v[4]))
        goto fail;
    if (!d2d_geometry_outline_add_line_segment(geometry, &v[4], &v[5]))
        goto fail;
    if (!d2d_geometry_outline_add_arc_quadrant(geometry, &v[5], &v3, &v[6]))
        goto fail;
    if (!d2d_geometry_outline_add_line_segment(geometry, &v[6], &v[7]))
        goto fail;
    if (!d2d_geometry_outline_add_arc_quadrant(geometry, &v[7], &v4, &v[0]))
        goto fail;

    return S_OK;

fail:
    d2d_geometry_cleanup(geometry);
    return E_OUTOFMEMORY;
}

static inline struct d2d_geometry *impl_from_ID2D1TransformedGeometry(ID2D1TransformedGeometry *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_geometry, ID2D1Geometry_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_transformed_geometry_QueryInterface(ID2D1TransformedGeometry *iface,
        REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1TransformedGeometry)
            || IsEqualGUID(iid, &IID_ID2D1Geometry)
            || IsEqualGUID(iid, &IID_ID2D1Resource)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID2D1TransformedGeometry_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_transformed_geometry_AddRef(ID2D1TransformedGeometry *iface)
{
    struct d2d_geometry *geometry = impl_from_ID2D1TransformedGeometry(iface);
    ULONG refcount = InterlockedIncrement(&geometry->refcount);

    TRACE("%p increasing refcount to %lu.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_transformed_geometry_Release(ID2D1TransformedGeometry *iface)
{
    struct d2d_geometry *geometry = impl_from_ID2D1TransformedGeometry(iface);
    ULONG refcount = InterlockedDecrement(&geometry->refcount);

    TRACE("%p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
    {
        geometry->outline.arc_faces = NULL;
        geometry->outline.arcs = NULL;
        geometry->outline.bezier_faces = NULL;
        geometry->outline.beziers = NULL;
        geometry->outline.faces = NULL;
        geometry->outline.vertices = NULL;
        geometry->fill.arc_vertices = NULL;
        geometry->fill.bezier_vertices = NULL;
        geometry->fill.faces = NULL;
        geometry->fill.vertices = NULL;
        ID2D1Geometry_Release(geometry->u.transformed.src_geometry);
        d2d_geometry_cleanup(geometry);
        free(geometry);
    }

    return refcount;
}

static void STDMETHODCALLTYPE d2d_transformed_geometry_GetFactory(ID2D1TransformedGeometry *iface,
        ID2D1Factory **factory)
{
    struct d2d_geometry *geometry = impl_from_ID2D1TransformedGeometry(iface);

    TRACE("iface %p, factory %p.\n", iface, factory);

    ID2D1Factory_AddRef(*factory = geometry->factory);
}

static HRESULT STDMETHODCALLTYPE d2d_transformed_geometry_GetBounds(ID2D1TransformedGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, D2D1_RECT_F *bounds)
{
    struct d2d_geometry *geometry = impl_from_ID2D1TransformedGeometry(iface);
    D2D1_MATRIX_3X2_F g;

    TRACE("iface %p, transform %p, bounds %p.\n", iface, transform, bounds);

    g = geometry->u.transformed.transform;
    if (transform)
        d2d_matrix_multiply(&g, transform);

    return ID2D1Geometry_GetBounds(geometry->u.transformed.src_geometry, &g, bounds);
}

static HRESULT STDMETHODCALLTYPE d2d_transformed_geometry_GetWidenedBounds(ID2D1TransformedGeometry *iface,
        float stroke_width, ID2D1StrokeStyle *stroke_style, const D2D1_MATRIX_3X2_F *transform,
        float tolerance, D2D1_RECT_F *bounds)
{
    struct d2d_geometry *geometry = impl_from_ID2D1TransformedGeometry(iface);

    TRACE("iface %p, stroke_width %.8e, stroke_style %p, transform %p, tolerance %.8e, bounds %p.\n",
            iface, stroke_width, stroke_style, transform, tolerance, bounds);

    return d2d_geometry_get_widened_bounds(&geometry->ID2D1Geometry_iface, stroke_width, stroke_style, transform,
            tolerance, bounds);
}

static HRESULT STDMETHODCALLTYPE d2d_transformed_geometry_StrokeContainsPoint(ID2D1TransformedGeometry *iface,
        D2D1_POINT_2F point, float stroke_width, ID2D1StrokeStyle *stroke_style, const D2D1_MATRIX_3X2_F *transform,
        float tolerance, BOOL *contains)
{
    struct d2d_geometry *geometry = impl_from_ID2D1TransformedGeometry(iface);
    D2D1_MATRIX_3X2_F g;

    TRACE("iface %p, point %s, stroke_width %.8e, stroke_style %p, transform %p, tolerance %.8e, contains %p.\n",
            iface, debug_d2d_point_2f(&point), stroke_width, stroke_style, transform, tolerance, contains);

    g = geometry->u.transformed.transform;
    stroke_width /= g.m11;
    if (transform)
        d2d_matrix_multiply(&g, transform);

    if (tolerance <= 0.0f)
        tolerance = D2D1_DEFAULT_FLATTENING_TOLERANCE;

    return ID2D1Geometry_StrokeContainsPoint(geometry->u.transformed.src_geometry, point, stroke_width, stroke_style,
            &g, tolerance, contains);
}

static HRESULT STDMETHODCALLTYPE d2d_transformed_geometry_FillContainsPoint(ID2D1TransformedGeometry *iface,
        D2D1_POINT_2F point, const D2D1_MATRIX_3X2_F *transform, float tolerance, BOOL *contains)
{
    struct d2d_geometry *geometry = impl_from_ID2D1TransformedGeometry(iface);
    D2D1_MATRIX_3X2_F g;

    TRACE("iface %p, point %s, transform %p, tolerance %.8e, contains %p.\n",
            iface, debug_d2d_point_2f(&point), transform, tolerance, contains);

    g = geometry->u.transformed.transform;
    if (transform)
        d2d_matrix_multiply(&g, transform);

    return ID2D1Geometry_FillContainsPoint(geometry->u.transformed.src_geometry, point, &g, tolerance, contains);
}

static HRESULT STDMETHODCALLTYPE d2d_transformed_geometry_CompareWithGeometry(ID2D1TransformedGeometry *iface,
        ID2D1Geometry *geometry, const D2D1_MATRIX_3X2_F *transform, float tolerance, D2D1_GEOMETRY_RELATION *relation)
{
    FIXME("iface %p, geometry %p, transform %p, tolerance %.8e, relation %p stub!\n",
            iface, geometry, transform, tolerance, relation);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_transformed_geometry_Simplify(ID2D1TransformedGeometry *iface,
        D2D1_GEOMETRY_SIMPLIFICATION_OPTION option, const D2D1_MATRIX_3X2_F *transform, float tolerance,
        ID2D1SimplifiedGeometrySink *sink)
{
    struct d2d_geometry *geometry = impl_from_ID2D1TransformedGeometry(iface);
    D2D1_MATRIX_3X2_F g;

    TRACE("iface %p, option %#x, transform %p, tolerance %.8e, sink %p.\n",
            iface, option, transform, tolerance, sink);

    g = geometry->u.transformed.transform;
    if (transform)
        d2d_matrix_multiply(&g, transform);

    return ID2D1Geometry_Simplify(geometry->u.transformed.src_geometry, option, &g, tolerance, sink);
}

static HRESULT STDMETHODCALLTYPE d2d_transformed_geometry_Tessellate(ID2D1TransformedGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, ID2D1TessellationSink *sink)
{
    struct d2d_geometry *geometry = impl_from_ID2D1TransformedGeometry(iface);
    D2D1_MATRIX_3X2_F g;

    TRACE("iface %p, transform %p, tolerance %.8e, sink %p.\n", iface, transform, tolerance, sink);

    g = geometry->u.transformed.transform;
    if (transform)
        d2d_matrix_multiply(&g, transform);

    return ID2D1Geometry_Tessellate(geometry->u.transformed.src_geometry, transform, tolerance, sink);
}

static HRESULT STDMETHODCALLTYPE d2d_transformed_geometry_CombineWithGeometry(ID2D1TransformedGeometry *iface,
        ID2D1Geometry *geometry, D2D1_COMBINE_MODE combine_mode, const D2D1_MATRIX_3X2_F *transform,
        float tolerance, ID2D1SimplifiedGeometrySink *sink)
{
    struct d2d_geometry *self = impl_from_ID2D1TransformedGeometry(iface);

    TRACE("iface %p, geometry %p, combine_mode %#x, transform %p, tolerance %.8e, sink %p.\n",
            iface, geometry, combine_mode, transform, tolerance, sink);

    return d2d_geometry_combine(&self->ID2D1Geometry_iface, geometry, combine_mode, transform, tolerance, sink);
}

static HRESULT STDMETHODCALLTYPE d2d_transformed_geometry_Outline(ID2D1TransformedGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, ID2D1SimplifiedGeometrySink *sink)
{
    FIXME("iface %p, transform %p, tolerance %.8e, sink %p stub!\n", iface, transform, tolerance, sink);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_transformed_geometry_ComputeArea(ID2D1TransformedGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, float *area)
{
    struct d2d_geometry *geometry = impl_from_ID2D1TransformedGeometry(iface);
    D2D1_MATRIX_3X2_F g;

    TRACE("iface %p, transform %p, tolerance %.8e, area %p.\n", iface, transform, tolerance, area);

    g = geometry->u.transformed.transform;
    if (transform)
        d2d_matrix_multiply(&g, transform);

    return ID2D1Geometry_ComputeArea(geometry->u.transformed.src_geometry, &g, tolerance, area);
}

static HRESULT STDMETHODCALLTYPE d2d_transformed_geometry_ComputeLength(ID2D1TransformedGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, float *length)
{
    FIXME("iface %p, transform %p, tolerance %.8e, length %p stub!\n", iface, transform, tolerance, length);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_transformed_geometry_ComputePointAtLength(ID2D1TransformedGeometry *iface,
        float length, const D2D1_MATRIX_3X2_F *transform, float tolerance, D2D1_POINT_2F *point,
        D2D1_POINT_2F *tangent)
{
    FIXME("iface %p, length %.8e, transform %p, tolerance %.8e, point %p, tangent %p stub!\n",
            iface, length, transform, tolerance, point, tangent);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_transformed_geometry_Widen(ID2D1TransformedGeometry *iface, float stroke_width,
        ID2D1StrokeStyle *stroke_style, const D2D1_MATRIX_3X2_F *transform, float tolerance,
        ID2D1SimplifiedGeometrySink *sink)
{
    struct d2d_geometry *geometry = impl_from_ID2D1TransformedGeometry(iface);

    TRACE("iface %p, stroke_width %.8e, stroke_style %p, transform %p, tolerance %.8e, sink %p.\n",
            iface, stroke_width, stroke_style, transform, tolerance, sink);

    return d2d_geometry_widen(&geometry->ID2D1Geometry_iface, stroke_width, stroke_style, transform, tolerance, sink);
}

static void STDMETHODCALLTYPE d2d_transformed_geometry_GetSourceGeometry(ID2D1TransformedGeometry *iface,
        ID2D1Geometry **src_geometry)
{
    struct d2d_geometry *geometry = impl_from_ID2D1TransformedGeometry(iface);

    TRACE("iface %p, src_geometry %p.\n", iface, src_geometry);

    ID2D1Geometry_AddRef(*src_geometry = geometry->u.transformed.src_geometry);
}

static void STDMETHODCALLTYPE d2d_transformed_geometry_GetTransform(ID2D1TransformedGeometry *iface,
        D2D1_MATRIX_3X2_F *transform)
{
    struct d2d_geometry *geometry = impl_from_ID2D1TransformedGeometry(iface);

    TRACE("iface %p, transform %p.\n", iface, transform);

    *transform = geometry->u.transformed.transform;
}

const struct ID2D1TransformedGeometryVtbl d2d_transformed_geometry_vtbl =
{
    d2d_transformed_geometry_QueryInterface,
    d2d_transformed_geometry_AddRef,
    d2d_transformed_geometry_Release,
    d2d_transformed_geometry_GetFactory,
    d2d_transformed_geometry_GetBounds,
    d2d_transformed_geometry_GetWidenedBounds,
    d2d_transformed_geometry_StrokeContainsPoint,
    d2d_transformed_geometry_FillContainsPoint,
    d2d_transformed_geometry_CompareWithGeometry,
    d2d_transformed_geometry_Simplify,
    d2d_transformed_geometry_Tessellate,
    d2d_transformed_geometry_CombineWithGeometry,
    d2d_transformed_geometry_Outline,
    d2d_transformed_geometry_ComputeArea,
    d2d_transformed_geometry_ComputeLength,
    d2d_transformed_geometry_ComputePointAtLength,
    d2d_transformed_geometry_Widen,
    d2d_transformed_geometry_GetSourceGeometry,
    d2d_transformed_geometry_GetTransform,
};

void d2d_geometry_stream(ID2D1Geometry *iface, const D2D_MATRIX_3X2_F *transform,
        ID2D1GeometrySink *sink)
{
    struct d2d_geometry *geometry = unsafe_impl_from_ID2D1Geometry(iface);
    geometry->ops->stream(geometry, transform, sink);
}

static void d2d_transformed_geometry_stream(struct d2d_geometry *geometry,
        const D2D_MATRIX_3X2_F *transform, ID2D1GeometrySink *sink)
{
    D2D_MATRIX_3X2_F m = geometry->transform;

    if (transform)
        d2d_matrix_multiply(&m, transform);
    d2d_geometry_stream(geometry->u.transformed.src_geometry, &m, sink);
}

static const struct d2d_geometry_ops d2d_transformed_geometry_ops =
{
    .stream = d2d_transformed_geometry_stream,
};

void d2d_transformed_geometry_init(struct d2d_geometry *geometry, ID2D1Factory *factory,
        ID2D1Geometry *src_geometry, const D2D_MATRIX_3X2_F *transform)
{
    struct d2d_geometry *src_impl;
    D2D_MATRIX_3X2_F g;

    src_impl = unsafe_impl_from_ID2D1Geometry(src_geometry);

    g = src_impl->transform;
    d2d_matrix_multiply(&g, transform);
    d2d_geometry_init(geometry, factory, &g, (ID2D1GeometryVtbl *)&d2d_transformed_geometry_vtbl,
            &d2d_transformed_geometry_ops);
    ID2D1Geometry_AddRef(geometry->u.transformed.src_geometry = src_geometry);
    geometry->u.transformed.transform = *transform;
    geometry->fill = src_impl->fill;
    geometry->outline = src_impl->outline;
    geometry->convex = src_impl->convex;
}
