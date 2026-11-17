/*
 * PROJECT:     LiberNT Direct2D
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Widening, widened bounds and boolean combination of flattened geometries
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "geometry_private.h"
#include <float.h>
#include <math.h>

/* in and out are the path tangents at the vertex; a smooth vertex lies inside a flattened curve. */
struct d2d_flat_point
{
    D2D1_POINT_2F p;
    D2D1_POINT_2F in, out;
    BOOL smooth;
};

struct d2d_flat_figure
{
    struct d2d_flat_point *points;
    size_t count;
    size_t capacity;
    BOOL closed;
    BOOL hollow;
};

struct d2d_flat_sink
{
    ID2D1SimplifiedGeometrySink ID2D1SimplifiedGeometrySink_iface;
    struct d2d_flat_figure *figures;
    size_t figure_count;
    size_t figure_capacity;
    D2D1_FILL_MODE fill_mode;
    float tolerance;
    HRESULT error;
};

static inline struct d2d_flat_sink *impl_from_ID2D1SimplifiedGeometrySink(ID2D1SimplifiedGeometrySink *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_flat_sink, ID2D1SimplifiedGeometrySink_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_flat_sink_QueryInterface(ID2D1SimplifiedGeometrySink *iface,
        REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_ID2D1SimplifiedGeometrySink) || IsEqualGUID(iid, &IID_IUnknown))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_flat_sink_AddRef(ID2D1SimplifiedGeometrySink *iface)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE d2d_flat_sink_Release(ID2D1SimplifiedGeometrySink *iface)
{
    return 1;
}

static void STDMETHODCALLTYPE d2d_flat_sink_SetFillMode(ID2D1SimplifiedGeometrySink *iface, D2D1_FILL_MODE mode)
{
    impl_from_ID2D1SimplifiedGeometrySink(iface)->fill_mode = mode;
}

static void STDMETHODCALLTYPE d2d_flat_sink_SetSegmentFlags(ID2D1SimplifiedGeometrySink *iface,
        D2D1_PATH_SEGMENT flags)
{
}

static BOOL d2d_point_equal(D2D1_POINT_2F a, D2D1_POINT_2F b)
{
    return a.x == b.x && a.y == b.y;
}

static D2D1_POINT_2F d2d_point_delta(D2D1_POINT_2F from, D2D1_POINT_2F to)
{
    D2D1_POINT_2F d;

    d2d_point_set(&d, to.x - from.x, to.y - from.y);
    return d;
}

static void d2d_flat_sink_add_point(struct d2d_flat_sink *sink, D2D1_POINT_2F p, D2D1_POINT_2F out,
        D2D1_POINT_2F in, BOOL smooth)
{
    struct d2d_flat_figure *figure;
    struct d2d_flat_point *last;

    if (FAILED(sink->error) || !sink->figure_count)
        return;
    figure = &sink->figures[sink->figure_count - 1];
    assert(figure->count);
    last = &figure->points[figure->count - 1];
    if (d2d_point_equal(last->p, p))
    {
        if (!smooth)
            last->smooth = FALSE;
        return;
    }
    if (last->out.x == 0.0f && last->out.y == 0.0f)
        last->out = out;
    if (!d2d_array_reserve((void **)&figure->points, &figure->capacity, figure->count + 1, sizeof(*figure->points)))
    {
        sink->error = E_OUTOFMEMORY;
        return;
    }
    last = &figure->points[figure->count++];
    last->p = p;
    last->in = in;
    d2d_point_set(&last->out, 0.0f, 0.0f);
    last->smooth = smooth;
}

static void STDMETHODCALLTYPE d2d_flat_sink_BeginFigure(ID2D1SimplifiedGeometrySink *iface,
        D2D1_POINT_2F start_point, D2D1_FIGURE_BEGIN begin)
{
    struct d2d_flat_sink *sink = impl_from_ID2D1SimplifiedGeometrySink(iface);
    struct d2d_flat_figure *figure;

    if (FAILED(sink->error))
        return;
    if (!d2d_array_reserve((void **)&sink->figures, &sink->figure_capacity, sink->figure_count + 1,
            sizeof(*sink->figures)))
    {
        sink->error = E_OUTOFMEMORY;
        return;
    }
    figure = &sink->figures[sink->figure_count++];
    memset(figure, 0, sizeof(*figure));
    figure->hollow = begin == D2D1_FIGURE_BEGIN_HOLLOW;
    if (!d2d_array_reserve((void **)&figure->points, &figure->capacity, 1, sizeof(*figure->points)))
    {
        sink->error = E_OUTOFMEMORY;
        return;
    }
    memset(&figure->points[0], 0, sizeof(figure->points[0]));
    figure->points[0].p = start_point;
    figure->count = 1;
}

static D2D1_POINT_2F d2d_flat_sink_last_point(const struct d2d_flat_sink *sink)
{
    const struct d2d_flat_figure *figure = &sink->figures[sink->figure_count - 1];

    return figure->points[figure->count - 1].p;
}

static void STDMETHODCALLTYPE d2d_flat_sink_AddLines(ID2D1SimplifiedGeometrySink *iface,
        const D2D1_POINT_2F *points, UINT32 count)
{
    struct d2d_flat_sink *sink = impl_from_ID2D1SimplifiedGeometrySink(iface);
    D2D1_POINT_2F d;
    UINT32 i;

    for (i = 0; i < count && SUCCEEDED(sink->error) && sink->figure_count; ++i)
    {
        d = d2d_point_delta(d2d_flat_sink_last_point(sink), points[i]);
        d2d_flat_sink_add_point(sink, points[i], d, d, FALSE);
    }
}

static D2D1_POINT_2F d2d_first_nonzero(D2D1_POINT_2F a, D2D1_POINT_2F b, D2D1_POINT_2F c)
{
    if (a.x != 0.0f || a.y != 0.0f)
        return a;
    if (b.x != 0.0f || b.y != 0.0f)
        return b;
    return c;
}

/* A cubic strays from its chord over a 1/n step by at most 3/4 of its largest second difference / n^2. */
static void d2d_flat_sink_add_bezier(struct d2d_flat_sink *sink, const D2D1_BEZIER_SEGMENT *b)
{
    D2D1_POINT_2F p0 = d2d_flat_sink_last_point(sink), q, prev, start_tangent, end_tangent, d;
    double d1x = (double)p0.x - 2.0 * b->point1.x + b->point2.x, d1y = (double)p0.y - 2.0 * b->point1.y + b->point2.y;
    double d2x = (double)b->point1.x - 2.0 * b->point2.x + b->point3.x;
    double d2y = (double)b->point1.y - 2.0 * b->point2.y + b->point3.y;
    double m = max(sqrt(d1x * d1x + d1y * d1y), sqrt(d2x * d2x + d2y * d2y)), t, u;
    unsigned int n, i;

    n = (unsigned int)min(ceil(sqrt(0.75 * m / sink->tolerance)), 1024.0);
    if (!n)
        n = 1;
    start_tangent = d2d_first_nonzero(d2d_point_delta(p0, b->point1), d2d_point_delta(p0, b->point2),
            d2d_point_delta(p0, b->point3));
    end_tangent = d2d_first_nonzero(d2d_point_delta(b->point2, b->point3), d2d_point_delta(b->point1, b->point3),
            d2d_point_delta(p0, b->point3));
    prev = p0;
    for (i = 1; i <= n && SUCCEEDED(sink->error); ++i)
    {
        t = (double)i / n;
        u = 1.0 - t;
        if (i == n)
        {
            q = b->point3;
        }
        else
        {
            d2d_point_set(&q, (float)(u * u * u * p0.x + 3.0 * u * u * t * b->point1.x + 3.0 * u * t * t * b->point2.x
                    + t * t * t * b->point3.x), (float)(u * u * u * p0.y + 3.0 * u * u * t * b->point1.y
                    + 3.0 * u * t * t * b->point2.y + t * t * t * b->point3.y));
        }
        d = d2d_point_delta(prev, q);
        d2d_flat_sink_add_point(sink, q, i == 1 ? start_tangent : d, i == n ? end_tangent : d, i != n);
        prev = q;
    }
}

static void STDMETHODCALLTYPE d2d_flat_sink_AddBeziers(ID2D1SimplifiedGeometrySink *iface,
        const D2D1_BEZIER_SEGMENT *beziers, UINT32 count)
{
    struct d2d_flat_sink *sink = impl_from_ID2D1SimplifiedGeometrySink(iface);
    UINT32 i;

    for (i = 0; i < count && SUCCEEDED(sink->error) && sink->figure_count; ++i)
        d2d_flat_sink_add_bezier(sink, &beziers[i]);
}

static void STDMETHODCALLTYPE d2d_flat_sink_EndFigure(ID2D1SimplifiedGeometrySink *iface, D2D1_FIGURE_END end)
{
    struct d2d_flat_sink *sink = impl_from_ID2D1SimplifiedGeometrySink(iface);
    struct d2d_flat_figure *figure;
    struct d2d_flat_point *first, *last;
    D2D1_POINT_2F d;

    if (FAILED(sink->error) || !sink->figure_count)
        return;
    figure = &sink->figures[sink->figure_count - 1];
    figure->closed = end == D2D1_FIGURE_END_CLOSED;
    if (!figure->closed || figure->count < 2)
        return;
    first = &figure->points[0];
    last = &figure->points[figure->count - 1];
    if (d2d_point_equal(first->p, last->p))
    {
        first->in = last->in;
        --figure->count;
        return;
    }
    d = d2d_point_delta(last->p, first->p);
    if (last->out.x == 0.0f && last->out.y == 0.0f)
        last->out = d;
    first->in = d;
}

static HRESULT STDMETHODCALLTYPE d2d_flat_sink_Close(ID2D1SimplifiedGeometrySink *iface)
{
    return impl_from_ID2D1SimplifiedGeometrySink(iface)->error;
}

static const struct ID2D1SimplifiedGeometrySinkVtbl d2d_flat_sink_vtbl =
{
    d2d_flat_sink_QueryInterface,
    d2d_flat_sink_AddRef,
    d2d_flat_sink_Release,
    d2d_flat_sink_SetFillMode,
    d2d_flat_sink_SetSegmentFlags,
    d2d_flat_sink_BeginFigure,
    d2d_flat_sink_AddLines,
    d2d_flat_sink_AddBeziers,
    d2d_flat_sink_EndFigure,
    d2d_flat_sink_Close,
};

static void d2d_flat_sink_cleanup(struct d2d_flat_sink *sink)
{
    size_t i;

    for (i = 0; i < sink->figure_count; ++i)
        free(sink->figures[i].points);
    free(sink->figures);
    memset(sink, 0, sizeof(*sink));
}

static HRESULT d2d_geometry_flatten(ID2D1Geometry *geometry, const D2D1_MATRIX_3X2_F *transform, float tolerance,
        struct d2d_flat_sink *sink)
{
    HRESULT hr;

    memset(sink, 0, sizeof(*sink));
    sink->ID2D1SimplifiedGeometrySink_iface.lpVtbl = &d2d_flat_sink_vtbl;
    sink->fill_mode = D2D1_FILL_MODE_ALTERNATE;
    sink->tolerance = tolerance > 0.0f ? tolerance : D2D1_DEFAULT_FLATTENING_TOLERANCE;
    hr = ID2D1Geometry_Simplify(geometry, D2D1_GEOMETRY_SIMPLIFICATION_OPTION_CUBICS_AND_LINES, transform,
            sink->tolerance, &sink->ID2D1SimplifiedGeometrySink_iface);
    if (SUCCEEDED(hr))
        hr = sink->error;
    if (FAILED(hr))
        d2d_flat_sink_cleanup(sink);
    return hr;
}

struct d2d_bounds_accumulator
{
    D2D1_MATRIX_3X2_F transform;
    D2D1_RECT_F bounds;
};

static void d2d_bounds_add_point(struct d2d_bounds_accumulator *acc, double x, double y)
{
    D2D1_POINT_2F p;

    d2d_point_transform(&p, &acc->transform, (float)x, (float)y);
    d2d_rect_expand(&acc->bounds, &p);
}

static void d2d_bounds_add_circle(struct d2d_bounds_accumulator *acc, double x, double y, double radius)
{
    const D2D1_MATRIX_3X2_F *m = &acc->transform;
    double rx = radius * sqrt((double)m->_11 * m->_11 + (double)m->_21 * m->_21);
    double ry = radius * sqrt((double)m->_12 * m->_12 + (double)m->_22 * m->_22);
    D2D1_POINT_2F c, p;

    d2d_point_transform(&c, m, (float)x, (float)y);
    d2d_point_set(&p, (float)(c.x - rx), (float)(c.y - ry));
    d2d_rect_expand(&acc->bounds, &p);
    d2d_point_set(&p, (float)(c.x + rx), (float)(c.y + ry));
    d2d_rect_expand(&acc->bounds, &p);
}

static BOOL d2d_unit(D2D1_POINT_2F v, double *x, double *y)
{
    double len = sqrt((double)v.x * v.x + (double)v.y * v.y);

    if (len == 0.0)
    {
        *x = 0.0;
        *y = 0.0;
        return FALSE;
    }
    *x = v.x / len;
    *y = v.y / len;
    return TRUE;
}

static void d2d_bounds_add_cap(struct d2d_bounds_accumulator *acc, D2D1_CAP_STYLE cap, D2D1_POINT_2F p,
        double dx, double dy, double half)
{
    double nx = -dy, ny = dx;

    d2d_bounds_add_point(acc, p.x + nx * half, p.y + ny * half);
    d2d_bounds_add_point(acc, p.x - nx * half, p.y - ny * half);
    switch (cap)
    {
        case D2D1_CAP_STYLE_SQUARE:
            d2d_bounds_add_point(acc, p.x + dx * half + nx * half, p.y + dy * half + ny * half);
            d2d_bounds_add_point(acc, p.x + dx * half - nx * half, p.y + dy * half - ny * half);
            break;
        case D2D1_CAP_STYLE_ROUND:
            d2d_bounds_add_circle(acc, p.x, p.y, half);
            break;
        case D2D1_CAP_STYLE_TRIANGLE:
            d2d_bounds_add_point(acc, p.x + dx * half, p.y + dy * half);
            break;
        case D2D1_CAP_STYLE_FLAT:
        default:
            break;
    }
}

static void d2d_bounds_add_join(struct d2d_bounds_accumulator *acc, D2D1_LINE_JOIN join, float miter_limit,
        D2D1_POINT_2F v, double d0x, double d0y, double d1x, double d1y, double half)
{
    double cross = d0x * d1y - d0y * d1x, dot = d0x * d1x + d0y * d1y;
    double side, o0x, o0y, o1x, o1y, c, ratio, ux, uy, len, du0, du1, t0, t1;

    d2d_bounds_add_point(acc, v.x - d0y * half, v.y + d0x * half);
    d2d_bounds_add_point(acc, v.x + d0y * half, v.y - d0x * half);
    d2d_bounds_add_point(acc, v.x - d1y * half, v.y + d1x * half);
    d2d_bounds_add_point(acc, v.x + d1y * half, v.y - d1x * half);
    if (fabs(cross) < 1e-9 && dot > 0.0)
        return;
    if (join == D2D1_LINE_JOIN_ROUND)
    {
        d2d_bounds_add_circle(acc, v.x, v.y, half);
        return;
    }
    if (join == D2D1_LINE_JOIN_BEVEL || fabs(cross) < 1e-9)
        return;
    side = cross > 0.0 ? -1.0 : 1.0;
    o0x = side * -d0y; o0y = side * d0x;
    o1x = side * -d1y; o1y = side * d1x;
    c = o0x * o1x + o0y * o1y;
    ratio = sqrt(2.0 / (1.0 + c));
    if (ratio <= miter_limit)
    {
        d2d_bounds_add_point(acc, v.x + (o0x + o1x) * half / (1.0 + c), v.y + (o0y + o1y) * half / (1.0 + c));
        return;
    }
    if (join == D2D1_LINE_JOIN_MITER_OR_BEVEL)
        return;
    len = sqrt((o0x + o1x) * (o0x + o1x) + (o0y + o1y) * (o0y + o1y));
    ux = (o0x + o1x) / len;
    uy = (o0y + o1y) / len;
    du0 = d0x * ux + d0y * uy;
    du1 = -(d1x * ux + d1y * uy);
    if (du0 > 1e-9)
    {
        t0 = (miter_limit * half - half * (o0x * ux + o0y * uy)) / du0;
        d2d_bounds_add_point(acc, v.x + o0x * half + t0 * d0x, v.y + o0y * half + t0 * d0y);
    }
    if (du1 > 1e-9)
    {
        t1 = (miter_limit * half - half * (o1x * ux + o1y * uy)) / du1;
        d2d_bounds_add_point(acc, v.x + o1x * half - t1 * d1x, v.y + o1y * half - t1 * d1y);
    }
}

static void d2d_bounds_add_figure(struct d2d_bounds_accumulator *acc, const struct d2d_flat_figure *figure,
        D2D1_CAP_STYLE start_cap, D2D1_CAP_STYLE end_cap, D2D1_LINE_JOIN join, float miter_limit, double half)
{
    size_t n = figure->count, j;
    double dx, dy, ex, ey;

    if (n == 1)
    {
        /* TODO: INFO_NEEDED: the widened bounds of a zero-length figure; caps are assumed axis aligned */
        if (start_cap == D2D1_CAP_STYLE_ROUND)
            d2d_bounds_add_circle(acc, figure->points[0].p.x, figure->points[0].p.y, half);
        else if (start_cap == D2D1_CAP_STYLE_SQUARE)
        {
            d2d_bounds_add_point(acc, figure->points[0].p.x - half, figure->points[0].p.y - half);
            d2d_bounds_add_point(acc, figure->points[0].p.x + half, figure->points[0].p.y + half);
        }
        return;
    }
    for (j = 0; j < n; ++j)
    {
        const struct d2d_flat_point *v = &figure->points[j];

        if (v->smooth)
        {
            /* The stroke of a curve covers the disc around each of its points. */
            d2d_bounds_add_circle(acc, v->p.x, v->p.y, half);
            continue;
        }
        if (!figure->closed && (!j || j == n - 1))
            continue;
        if (d2d_unit(v->in, &dx, &dy) && d2d_unit(v->out, &ex, &ey))
            d2d_bounds_add_join(acc, join, miter_limit, v->p, dx, dy, ex, ey, half);
    }
    if (figure->closed)
        return;
    if (d2d_unit(figure->points[0].out, &dx, &dy))
        d2d_bounds_add_cap(acc, start_cap, figure->points[0].p, -dx, -dy, half);
    if (d2d_unit(figure->points[n - 1].in, &dx, &dy))
        d2d_bounds_add_cap(acc, end_cap, figure->points[n - 1].p, dx, dy, half);
}

HRESULT d2d_geometry_get_widened_bounds(ID2D1Geometry *geometry, float stroke_width, ID2D1StrokeStyle *stroke_style,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, D2D1_RECT_F *bounds)
{
    static const D2D1_MATRIX_3X2_F identity = {{{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f}}};
    D2D1_CAP_STYLE start_cap = D2D1_CAP_STYLE_FLAT, end_cap = D2D1_CAP_STYLE_FLAT;
    D2D1_LINE_JOIN join = D2D1_LINE_JOIN_MITER;
    struct d2d_bounds_accumulator acc;
    struct d2d_flat_sink sink;
    float miter_limit = 10.0f;
    double half = fabsf(stroke_width) / 2.0;
    size_t i;
    HRESULT hr;

    if (!bounds)
        return E_INVALIDARG;
    if (stroke_style)
    {
        start_cap = ID2D1StrokeStyle_GetStartCap(stroke_style);
        end_cap = ID2D1StrokeStyle_GetEndCap(stroke_style);
        join = ID2D1StrokeStyle_GetLineJoin(stroke_style);
        miter_limit = max(ID2D1StrokeStyle_GetMiterLimit(stroke_style), 1.0f);
        /* TODO: dashes; a dashed stroke is bounded by the undashed one used here */
    }
    /* Windows widens in geometry space and transforms the widened outline. */
    if (FAILED(hr = d2d_geometry_flatten(geometry, NULL, tolerance, &sink)))
        return hr;

    acc.transform = transform ? *transform : identity;
    acc.bounds.left = FLT_MAX;
    acc.bounds.top = FLT_MAX;
    acc.bounds.right = -FLT_MAX;
    acc.bounds.bottom = -FLT_MAX;
    for (i = 0; i < sink.figure_count; ++i)
    {
        if (sink.figures[i].count)
            d2d_bounds_add_figure(&acc, &sink.figures[i], start_cap, end_cap, join, miter_limit, half);
    }
    d2d_flat_sink_cleanup(&sink);

    if (acc.bounds.left > acc.bounds.right)
    {
        acc.bounds.left = INFINITY;
        acc.bounds.right = FLT_MAX;
        acc.bounds.top = INFINITY;
        acc.bounds.bottom = FLT_MAX;
    }
    *bounds = acc.bounds;
    return S_OK;
}

struct d2d_widen_style
{
    D2D1_CAP_STYLE start_cap, end_cap;
    D2D1_LINE_JOIN join;
    float miter_limit;
    double half;
    double tolerance;
};

struct d2d_widen_chain
{
    D2D1_POINT_2F *points;
    size_t count;
    size_t capacity;
    BOOL error;
};

static void d2d_widen_add(struct d2d_widen_chain *chain, double x, double y)
{
    D2D1_POINT_2F p;

    d2d_point_set(&p, (float)x, (float)y);
    if (chain->count && d2d_point_equal(chain->points[chain->count - 1], p))
        return;
    if (!d2d_array_reserve((void **)&chain->points, &chain->capacity, chain->count + 1, sizeof(*chain->points)))
    {
        chain->error = TRUE;
        return;
    }
    chain->points[chain->count++] = p;
}

static void d2d_widen_add_arc(struct d2d_widen_chain *chain, const struct d2d_widen_style *style, double cx,
        double cy, double ux, double uy, double angle)
{
    double step, c, s, x, y, t;
    unsigned int n, i;

    step = style->tolerance < style->half ? 2.0 * acos(1.0 - style->tolerance / style->half) : M_PI / 2.0;
    n = (unsigned int)min(ceil(fabs(angle) / step), 256.0);
    for (i = 1; i < n; ++i)
    {
        t = angle * i / n;
        c = cos(t);
        s = sin(t);
        x = ux * c - uy * s;
        y = ux * s + uy * c;
        d2d_widen_add(chain, cx + x * style->half, cy + y * style->half);
    }
}

static void d2d_widen_add_join(struct d2d_widen_chain *chain, const struct d2d_widen_style *style, D2D1_POINT_2F v,
        double d0x, double d0y, double d1x, double d1y, double u0x, double u0y, double u1x, double u1y)
{
    double c = u0x * u1x + u0y * u1y, half = style->half, angle, len, ux, uy, du0, du1, t0, t1;

    switch (style->join)
    {
        case D2D1_LINE_JOIN_ROUND:
            angle = atan2(u0x * u1y - u0y * u1x, c);
            if (fabs(angle) > M_PI - 1e-6)
                angle = d0x * u0y - d0y * u0x > 0.0 ? -M_PI : M_PI;
            d2d_widen_add_arc(chain, style, v.x, v.y, u0x, u0y, angle);
            return;
        case D2D1_LINE_JOIN_BEVEL:
            return;
        case D2D1_LINE_JOIN_MITER:
        case D2D1_LINE_JOIN_MITER_OR_BEVEL:
        default:
            break;
    }
    if (c > -1.0 + 1e-9 && sqrt(2.0 / (1.0 + c)) <= style->miter_limit)
    {
        d2d_widen_add(chain, v.x + (u0x + u1x) * half / (1.0 + c), v.y + (u0y + u1y) * half / (1.0 + c));
        return;
    }
    if (style->join == D2D1_LINE_JOIN_MITER_OR_BEVEL)
        return;
    len = sqrt((u0x + u1x) * (u0x + u1x) + (u0y + u1y) * (u0y + u1y));
    if (len < 1e-9)
    {
        ux = d0x;
        uy = d0y;
    }
    else
    {
        ux = (u0x + u1x) / len;
        uy = (u0y + u1y) / len;
    }
    du0 = d0x * ux + d0y * uy;
    du1 = -(d1x * ux + d1y * uy);
    if (du0 > 1e-9)
    {
        t0 = (style->miter_limit * half - half * (u0x * ux + u0y * uy)) / du0;
        d2d_widen_add(chain, v.x + u0x * half + t0 * d0x, v.y + u0y * half + t0 * d0y);
    }
    if (du1 > 1e-9)
    {
        t1 = (style->miter_limit * half - half * (u1x * ux + u1y * uy)) / du1;
        d2d_widen_add(chain, v.x + u1x * half - t1 * d1x, v.y + u1y * half - t1 * d1y);
    }
}

static void d2d_widen_add_cap(struct d2d_widen_chain *chain, const struct d2d_widen_style *style, D2D1_CAP_STYLE cap,
        D2D1_POINT_2F v, double dx, double dy)
{
    double lx = dy, ly = -dx, half = style->half;

    d2d_widen_add(chain, v.x + lx * half, v.y + ly * half);
    switch (cap)
    {
        case D2D1_CAP_STYLE_SQUARE:
            d2d_widen_add(chain, v.x + (lx + dx) * half, v.y + (ly + dy) * half);
            d2d_widen_add(chain, v.x + (dx - lx) * half, v.y + (dy - ly) * half);
            break;
        case D2D1_CAP_STYLE_ROUND:
            d2d_widen_add_arc(chain, style, v.x, v.y, lx, ly, M_PI);
            break;
        case D2D1_CAP_STYLE_TRIANGLE:
            d2d_widen_add(chain, v.x + dx * half, v.y + dy * half);
            break;
        case D2D1_CAP_STYLE_FLAT:
        default:
            break;
    }
    d2d_widen_add(chain, v.x - lx * half, v.y - ly * half);
}

/* Joins go on the outer side of each turn and the inner side passes through the vertex, so every part winds alike. */
static void d2d_widen_add_side(struct d2d_widen_chain *chain, const struct d2d_widen_style *style,
        const struct d2d_flat_figure *figure, double side)
{
    size_t n = figure->count, segment_count = figure->closed ? n : n - 1, j;
    double half = style->half;

    for (j = 0; j < segment_count; ++j)
    {
        D2D1_POINT_2F a = figure->points[j].p, b = figure->points[(j + 1) % n].p, c;
        double dx, dy, ex, ey, cross;

        d2d_unit(d2d_point_delta(a, b), &dx, &dy);
        d2d_widen_add(chain, a.x + side * dy * half, a.y - side * dx * half);
        d2d_widen_add(chain, b.x + side * dy * half, b.y - side * dx * half);
        if (j + 1 == segment_count && !figure->closed)
            break;
        c = figure->points[(j + 2) % n].p;
        d2d_unit(d2d_point_delta(b, c), &ex, &ey);
        cross = dx * ey - dy * ex;
        if (fabs(cross) < 1e-9 && dx * ex + dy * ey > 0.0)
            continue;
        if (cross * side > 0.0 || (fabs(cross) < 1e-9 && side > 0.0))
        {
            if (figure->points[(j + 1) % n].smooth)
                continue;
            d2d_widen_add_join(chain, style, b, dx, dy, ex, ey, side * dy, -side * dx, side * ey, -side * ex);
        }
        else
        {
            d2d_widen_add(chain, b.x, b.y);
        }
    }
}

static void d2d_widen_emit(struct d2d_widen_chain *chain, const D2D1_MATRIX_3X2_F *transform, BOOL reverse,
        ID2D1SimplifiedGeometrySink *sink)
{
    size_t i;

    if (chain->error || chain->count < 3)
    {
        chain->count = 0;
        return;
    }
    if (reverse)
    {
        for (i = 0; i < chain->count / 2; ++i)
        {
            D2D1_POINT_2F t = chain->points[i];

            chain->points[i] = chain->points[chain->count - 1 - i];
            chain->points[chain->count - 1 - i] = t;
        }
    }
    if (transform)
    {
        for (i = 0; i < chain->count; ++i)
            d2d_point_transform(&chain->points[i], transform, chain->points[i].x, chain->points[i].y);
    }
    ID2D1SimplifiedGeometrySink_BeginFigure(sink, chain->points[0], D2D1_FIGURE_BEGIN_FILLED);
    ID2D1SimplifiedGeometrySink_AddLines(sink, chain->points + 1, (UINT32)(chain->count - 1));
    ID2D1SimplifiedGeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);
    chain->count = 0;
}

static void d2d_widen_append_reversed(struct d2d_widen_chain *dst, const struct d2d_widen_chain *src)
{
    size_t i;

    if (src->error)
        dst->error = TRUE;
    for (i = src->count; i-- > 0;)
        d2d_widen_add(dst, src->points[i].x, src->points[i].y);
}

static void d2d_widen_figure(struct d2d_widen_chain *chain, struct d2d_widen_chain *right,
        const struct d2d_widen_style *style, const struct d2d_flat_figure *figure, const D2D1_MATRIX_3X2_F *transform,
        ID2D1SimplifiedGeometrySink *sink)
{
    size_t n = figure->count;
    double dx, dy;

    if (n == 1)
    {
        /* TODO: INFO_NEEDED: the stroke of a zero-length figure; its caps are drawn facing +x */
        if (style->start_cap == D2D1_CAP_STYLE_FLAT && style->end_cap == D2D1_CAP_STYLE_FLAT)
            return;
        d2d_widen_add_cap(chain, style, style->start_cap, figure->points[0].p, -1.0, 0.0);
        d2d_widen_add_cap(chain, style, style->end_cap, figure->points[0].p, 1.0, 0.0);
        d2d_widen_emit(chain, transform, FALSE, sink);
        return;
    }
    if (figure->closed)
    {
        d2d_widen_add_side(chain, style, figure, 1.0);
        d2d_widen_emit(chain, transform, FALSE, sink);
        d2d_widen_add_side(chain, style, figure, -1.0);
        d2d_widen_emit(chain, transform, TRUE, sink);
        return;
    }
    d2d_widen_add_side(chain, style, figure, 1.0);
    d2d_unit(d2d_point_delta(figure->points[n - 2].p, figure->points[n - 1].p), &dx, &dy);
    d2d_widen_add_cap(chain, style, style->end_cap, figure->points[n - 1].p, dx, dy);
    right->count = 0;
    d2d_widen_add_side(right, style, figure, -1.0);
    d2d_widen_append_reversed(chain, right);
    d2d_unit(d2d_point_delta(figure->points[1].p, figure->points[0].p), &dx, &dy);
    d2d_widen_add_cap(chain, style, style->start_cap, figure->points[0].p, dx, dy);
    d2d_widen_emit(chain, transform, FALSE, sink);
}

HRESULT d2d_geometry_widen(ID2D1Geometry *geometry, float stroke_width, ID2D1StrokeStyle *stroke_style,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, ID2D1SimplifiedGeometrySink *sink)
{
    struct d2d_widen_chain chain = { 0 }, right = { 0 };
    struct d2d_widen_style style;
    struct d2d_flat_sink flat;
    BOOL error;
    size_t i;
    HRESULT hr;

    if (!sink)
        return E_INVALIDARG;
    style.start_cap = D2D1_CAP_STYLE_FLAT;
    style.end_cap = D2D1_CAP_STYLE_FLAT;
    style.join = D2D1_LINE_JOIN_MITER;
    style.miter_limit = 10.0f;
    style.half = fabsf(stroke_width) / 2.0;
    style.tolerance = tolerance > 0.0f ? tolerance : D2D1_DEFAULT_FLATTENING_TOLERANCE;
    if (stroke_style)
    {
        style.start_cap = ID2D1StrokeStyle_GetStartCap(stroke_style);
        style.end_cap = ID2D1StrokeStyle_GetEndCap(stroke_style);
        style.join = ID2D1StrokeStyle_GetLineJoin(stroke_style);
        style.miter_limit = max(ID2D1StrokeStyle_GetMiterLimit(stroke_style), 1.0f);
        /* TODO: dashes; the stroke is widened undashed */
    }
    if (FAILED(hr = d2d_geometry_flatten(geometry, NULL, tolerance, &flat)))
        return hr;

    ID2D1SimplifiedGeometrySink_SetFillMode(sink, D2D1_FILL_MODE_WINDING);
    for (i = 0; i < flat.figure_count && !chain.error && !right.error; ++i)
    {
        if (flat.figures[i].count)
            d2d_widen_figure(&chain, &right, &style, &flat.figures[i], transform, sink);
    }
    error = chain.error || right.error;
    free(chain.points);
    free(right.points);
    d2d_flat_sink_cleanup(&flat);
    return error ? E_OUTOFMEMORY : S_OK;
}

struct d2d_combine_edge
{
    double x0, y0, x1, y1;
    unsigned int owner;
    size_t split_count;
    size_t split_capacity;
    struct d2d_combine_split *splits;
};

struct d2d_combine_split
{
    double t;
    double x, y;
};

struct d2d_combine_piece
{
    double x0, y0, x1, y1;
    BOOL hollow;
    BOOL used;
};

enum d2d_combine_keep
{
    D2D_COMBINE_DROP,
    D2D_COMBINE_KEEP,
    D2D_COMBINE_KEEP_HOLLOW,
};

struct d2d_combine_context
{
    struct d2d_combine_edge *edges;
    size_t edge_count;
    size_t edge_capacity;
    struct d2d_combine_piece *pieces;
    size_t piece_count;
    size_t piece_capacity;
    D2D1_FILL_MODE fill_mode[2];
    double epsilon;
};

static BOOL d2d_combine_add_edges(struct d2d_combine_context *ctx, const struct d2d_flat_sink *sink,
        unsigned int owner)
{
    size_t i, j;

    for (i = 0; i < sink->figure_count; ++i)
    {
        const struct d2d_flat_figure *figure = &sink->figures[i];

        /* Hollow figures have no area; open ones are closed for filling. */
        if (figure->count < 2 || figure->hollow)
            continue;
        for (j = 0; j < figure->count; ++j)
        {
            D2D1_POINT_2F a = figure->points[j].p, b = figure->points[(j + 1) % figure->count].p;
            struct d2d_combine_edge *edge;

            if (a.x == b.x && a.y == b.y)
                continue;
            if (!d2d_array_reserve((void **)&ctx->edges, &ctx->edge_capacity, ctx->edge_count + 1,
                    sizeof(*ctx->edges)))
                return FALSE;
            edge = &ctx->edges[ctx->edge_count++];
            memset(edge, 0, sizeof(*edge));
            edge->x0 = a.x;
            edge->y0 = a.y;
            edge->x1 = b.x;
            edge->y1 = b.y;
            edge->owner = owner;
        }
    }
    return TRUE;
}

static BOOL d2d_combine_add_split(struct d2d_combine_edge *edge, double t, double x, double y)
{
    if (!d2d_array_reserve((void **)&edge->splits, &edge->split_capacity, edge->split_count + 1,
            sizeof(*edge->splits)))
        return FALSE;
    edge->splits[edge->split_count].t = t;
    edge->splits[edge->split_count].x = x;
    edge->splits[edge->split_count].y = y;
    ++edge->split_count;
    return TRUE;
}

static BOOL d2d_combine_split_at_point(struct d2d_combine_edge *edge, double x, double y)
{
    double ex = edge->x1 - edge->x0, ey = edge->y1 - edge->y0;
    double t = (ex * (x - edge->x0) + ey * (y - edge->y0)) / (ex * ex + ey * ey);
    const double eps = 1e-9;

    if (t <= eps || t >= 1.0 - eps)
        return TRUE;
    return d2d_combine_add_split(edge, t, x, y);
}

static BOOL d2d_combine_intersect(struct d2d_combine_edge *a, struct d2d_combine_edge *b, double epsilon)
{
    double ax = a->x1 - a->x0, ay = a->y1 - a->y0, bx = b->x1 - b->x0, by = b->y1 - b->y0;
    double denom = ax * by - ay * bx, s, t, x, y, la, lb;
    const double eps = 1e-9;

    la = sqrt(ax * ax + ay * ay);
    lb = sqrt(bx * bx + by * by);
    if (fabs(denom) <= 1e-9 * la * lb)
    {
        if (fabs(ax * (b->y0 - a->y0) - ay * (b->x0 - a->x0)) > epsilon * la)
            return TRUE;
        return d2d_combine_split_at_point(a, b->x0, b->y0) && d2d_combine_split_at_point(a, b->x1, b->y1)
                && d2d_combine_split_at_point(b, a->x0, a->y0) && d2d_combine_split_at_point(b, a->x1, a->y1);
    }
    s = ((b->x0 - a->x0) * by - (b->y0 - a->y0) * bx) / denom;
    t = ((b->x0 - a->x0) * ay - (b->y0 - a->y0) * ax) / denom;
    if (s < -eps || s > 1.0 + eps || t < -eps || t > 1.0 + eps)
        return TRUE;
    if (s <= eps)
    {
        x = a->x0;
        y = a->y0;
    }
    else if (s >= 1.0 - eps)
    {
        x = a->x1;
        y = a->y1;
    }
    else if (t <= eps)
    {
        x = b->x0;
        y = b->y0;
    }
    else if (t >= 1.0 - eps)
    {
        x = b->x1;
        y = b->y1;
    }
    else
    {
        x = a->x0 + s * ax;
        y = a->y0 + s * ay;
    }
    if (s > eps && s < 1.0 - eps && !d2d_combine_add_split(a, s, x, y))
        return FALSE;
    if (t > eps && t < 1.0 - eps && !d2d_combine_add_split(b, t, x, y))
        return FALSE;
    return TRUE;
}

static int __cdecl d2d_combine_split_compare(const void *a, const void *b)
{
    const struct d2d_combine_split *sa = a, *sb = b;

    return sa->t < sb->t ? -1 : sa->t > sb->t ? 1 : 0;
}

static BOOL d2d_combine_inside(const struct d2d_combine_context *ctx, unsigned int owner, double x, double y)
{
    int winding = 0;
    size_t i;

    for (i = 0; i < ctx->edge_count; ++i)
    {
        const struct d2d_combine_edge *edge = &ctx->edges[i];
        double cross;

        if (edge->owner != owner)
            continue;
        cross = (edge->x1 - edge->x0) * (y - edge->y0) - (x - edge->x0) * (edge->y1 - edge->y0);
        if (edge->y0 <= y)
        {
            if (edge->y1 > y && cross > 0.0)
                ++winding;
        }
        else if (edge->y1 <= y && cross < 0.0)
        {
            --winding;
        }
    }
    return ctx->fill_mode[owner] == D2D1_FILL_MODE_WINDING ? winding != 0 : (winding & 1) != 0;
}

static BOOL d2d_combine_on_other(const struct d2d_combine_context *ctx, unsigned int owner,
        const struct d2d_combine_piece *piece, BOOL *same)
{
    double mx = (piece->x0 + piece->x1) / 2.0, my = (piece->y0 + piece->y1) / 2.0;
    double px = piece->x1 - piece->x0, py = piece->y1 - piece->y0;
    size_t i;

    for (i = 0; i < ctx->edge_count; ++i)
    {
        const struct d2d_combine_edge *edge = &ctx->edges[i];
        double ex = edge->x1 - edge->x0, ey = edge->y1 - edge->y0, len, distance, along;

        if (edge->owner == owner)
            continue;
        len = sqrt(ex * ex + ey * ey);
        distance = fabs(ex * (my - edge->y0) - ey * (mx - edge->x0)) / len;
        along = (ex * (mx - edge->x0) + ey * (my - edge->y0)) / (len * len);
        if (distance <= ctx->epsilon && along >= 0.0 && along <= 1.0)
        {
            *same = ex * px + ey * py > 0.0;
            return TRUE;
        }
    }
    return FALSE;
}

static enum d2d_combine_keep d2d_combine_classify(const struct d2d_combine_context *ctx, D2D1_COMBINE_MODE mode,
        const struct d2d_combine_piece *piece, unsigned int owner)
{
    double mx = (piece->x0 + piece->x1) / 2.0, my = (piece->y0 + piece->y1) / 2.0;
    BOOL same, inside;

    if (d2d_combine_on_other(ctx, owner, piece, &same))
    {
        /* A shared boundary is kept once, from the first operand. */
        if (owner)
            return D2D_COMBINE_DROP;
        switch (mode)
        {
            case D2D1_COMBINE_MODE_UNION:
                return same ? D2D_COMBINE_KEEP : D2D_COMBINE_DROP;
            case D2D1_COMBINE_MODE_INTERSECT:
                /* Operands touching along an edge intersect in that edge, an open hollow figure. */
                return same ? D2D_COMBINE_KEEP : D2D_COMBINE_KEEP_HOLLOW;
            case D2D1_COMBINE_MODE_EXCLUDE:
                return same ? D2D_COMBINE_DROP : D2D_COMBINE_KEEP;
            case D2D1_COMBINE_MODE_XOR:
            default:
                return D2D_COMBINE_DROP;
        }
    }
    inside = d2d_combine_inside(ctx, !owner, mx, my);
    switch (mode)
    {
        case D2D1_COMBINE_MODE_UNION:
            return inside ? D2D_COMBINE_DROP : D2D_COMBINE_KEEP;
        case D2D1_COMBINE_MODE_INTERSECT:
            return inside ? D2D_COMBINE_KEEP : D2D_COMBINE_DROP;
        case D2D1_COMBINE_MODE_EXCLUDE:
            return inside == !!owner ? D2D_COMBINE_KEEP : D2D_COMBINE_DROP;
        case D2D1_COMBINE_MODE_XOR:
        default:
            return D2D_COMBINE_KEEP;
    }
}

static BOOL d2d_combine_result_inside(const struct d2d_combine_context *ctx, D2D1_COMBINE_MODE mode, double x, double y)
{
    BOOL a = d2d_combine_inside(ctx, 0, x, y), b = d2d_combine_inside(ctx, 1, x, y);

    switch (mode)
    {
        case D2D1_COMBINE_MODE_UNION:
            return a || b;
        case D2D1_COMBINE_MODE_INTERSECT:
            return a && b;
        case D2D1_COMBINE_MODE_EXCLUDE:
            return a && !b;
        case D2D1_COMBINE_MODE_XOR:
        default:
            return a != b;
    }
}

/* Result outlines run with the result on their right (clockwise on screen) and holes the other way. */
static BOOL d2d_combine_result_on_right(const struct d2d_combine_context *ctx, D2D1_COMBINE_MODE mode,
        const struct d2d_combine_piece *piece)
{
    double dx = piece->x1 - piece->x0, dy = piece->y1 - piece->y0, len = sqrt(dx * dx + dy * dy);
    double offset = min(ctx->epsilon * 10.0, len / 4.0);

    return d2d_combine_result_inside(ctx, mode, (piece->x0 + piece->x1) / 2.0 - dy / len * offset,
            (piece->y0 + piece->y1) / 2.0 + dx / len * offset);
}

static BOOL d2d_combine_add_piece(struct d2d_combine_context *ctx, double x0, double y0, double x1, double y1)
{
    struct d2d_combine_piece *piece;

    if (x0 == x1 && y0 == y1)
        return TRUE;
    if (!d2d_array_reserve((void **)&ctx->pieces, &ctx->piece_capacity, ctx->piece_count + 1, sizeof(*ctx->pieces)))
        return FALSE;
    piece = &ctx->pieces[ctx->piece_count++];
    piece->x0 = x0;
    piece->y0 = y0;
    piece->x1 = x1;
    piece->y1 = y1;
    piece->hollow = FALSE;
    piece->used = FALSE;
    return TRUE;
}

/* The sharpest right turn keeps regions that touch at a point in separate figures, as Windows does. */
static struct d2d_combine_piece *d2d_combine_next(struct d2d_combine_context *ctx, BOOL hollow, double x, double y,
        double dx, double dy, double *nx, double *ny)
{
    struct d2d_combine_piece *best = NULL, *reversed = NULL;
    double best_angle = -4.0, angle, ex, ey;
    size_t i;

    for (i = 0; i < ctx->piece_count; ++i)
    {
        struct d2d_combine_piece *piece = &ctx->pieces[i];

        if (piece->used || piece->hollow != hollow)
            continue;
        if (piece->x0 == x && piece->y0 == y)
        {
            ex = piece->x1 - piece->x0;
            ey = piece->y1 - piece->y0;
            angle = atan2(dx * ey - dy * ex, dx * ex + dy * ey);
            if (angle > 3.14159)
                angle = -angle;
            if (angle > best_angle)
            {
                best_angle = angle;
                best = piece;
            }
        }
        else if (!reversed && piece->x1 == x && piece->y1 == y)
        {
            reversed = piece;
        }
    }
    if (best)
    {
        *nx = best->x1;
        *ny = best->y1;
        return best;
    }
    if (reversed)
    {
        *nx = reversed->x0;
        *ny = reversed->y0;
    }
    return reversed;
}

static void d2d_combine_emit(struct d2d_combine_context *ctx, BOOL hollow, ID2D1SimplifiedGeometrySink *sink)
{
    D2D1_POINT_2F *points = NULL, start;
    size_t point_capacity = 0, count, i;
    struct d2d_combine_piece *next;
    double px, py, cx, cy;

    for (i = 0; i < ctx->piece_count; ++i)
    {
        if (ctx->pieces[i].used || ctx->pieces[i].hollow != hollow)
            continue;
        ctx->pieces[i].used = TRUE;
        d2d_point_set(&start, (float)ctx->pieces[i].x0, (float)ctx->pieces[i].y0);
        px = ctx->pieces[i].x0;
        py = ctx->pieces[i].y0;
        cx = ctx->pieces[i].x1;
        cy = ctx->pieces[i].y1;
        count = 0;
        for (;;)
        {
            double x = cx, y = cy;

            if (!d2d_array_reserve((void **)&points, &point_capacity, count + 1, sizeof(*points)))
                break;
            d2d_point_set(&points[count++], (float)cx, (float)cy);
            if (cx == ctx->pieces[i].x0 && cy == ctx->pieces[i].y0)
                break;
            if (!(next = d2d_combine_next(ctx, hollow, x, y, x - px, y - py, &cx, &cy)))
                break;
            next->used = TRUE;
            px = x;
            py = y;
        }
        if (!count)
            continue;
        /* FIXME: curves come out as their flattened lines, not as Bezier segments */
        ID2D1SimplifiedGeometrySink_BeginFigure(sink, start,
                hollow ? D2D1_FIGURE_BEGIN_HOLLOW : D2D1_FIGURE_BEGIN_FILLED);
        ID2D1SimplifiedGeometrySink_AddLines(sink, points, (UINT32)count);
        ID2D1SimplifiedGeometrySink_EndFigure(sink, hollow ? D2D1_FIGURE_END_OPEN : D2D1_FIGURE_END_CLOSED);
    }
    free(points);
}

HRESULT d2d_geometry_combine(ID2D1Geometry *geometry, ID2D1Geometry *other, D2D1_COMBINE_MODE mode,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, ID2D1SimplifiedGeometrySink *sink)
{
    struct d2d_combine_context ctx;
    struct d2d_flat_sink flat[2];
    double extent = 1.0;
    BOOL ok = TRUE;
    size_t i, j, kept;
    HRESULT hr;

    if (!other || !sink || mode > D2D1_COMBINE_MODE_EXCLUDE)
        return E_INVALIDARG;
    if (FAILED(hr = d2d_geometry_flatten(geometry, NULL, tolerance, &flat[0])))
        return hr;
    if (FAILED(hr = d2d_geometry_flatten(other, transform, tolerance, &flat[1])))
    {
        d2d_flat_sink_cleanup(&flat[0]);
        return hr;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.fill_mode[0] = flat[0].fill_mode;
    ctx.fill_mode[1] = flat[1].fill_mode;
    ok = d2d_combine_add_edges(&ctx, &flat[0], 0) && d2d_combine_add_edges(&ctx, &flat[1], 1);
    d2d_flat_sink_cleanup(&flat[0]);
    d2d_flat_sink_cleanup(&flat[1]);

    for (i = 0; ok && i < ctx.edge_count; ++i)
    {
        extent = max(extent, fabs(ctx.edges[i].x0));
        extent = max(extent, fabs(ctx.edges[i].y0));
    }
    ctx.epsilon = extent * 1e-5;
    for (i = 0; ok && i < ctx.edge_count; ++i)
    {
        for (j = i + 1; ok && j < ctx.edge_count; ++j)
            ok = d2d_combine_intersect(&ctx.edges[i], &ctx.edges[j], ctx.epsilon);
    }

    for (i = 0; ok && i < ctx.edge_count; ++i)
    {
        struct d2d_combine_edge *edge = &ctx.edges[i];
        double x = edge->x0, y = edge->y0;
        size_t first = ctx.piece_count;

        qsort(edge->splits, edge->split_count, sizeof(*edge->splits), d2d_combine_split_compare);
        for (j = 0; ok && j <= edge->split_count; ++j)
        {
            double nx = j < edge->split_count ? edge->splits[j].x : edge->x1;
            double ny = j < edge->split_count ? edge->splits[j].y : edge->y1;

            ok = d2d_combine_add_piece(&ctx, x, y, nx, ny);
            x = nx;
            y = ny;
        }
        for (j = first, kept = first; ok && j < ctx.piece_count; ++j)
        {
            struct d2d_combine_piece piece = ctx.pieces[j];
            enum d2d_combine_keep keep = d2d_combine_classify(&ctx, mode, &piece, edge->owner);

            if (keep == D2D_COMBINE_DROP)
                continue;
            piece.hollow = keep == D2D_COMBINE_KEEP_HOLLOW;
            if (piece.hollow || !d2d_combine_result_on_right(&ctx, mode, &piece))
            {
                piece.x0 = ctx.pieces[j].x1;
                piece.y0 = ctx.pieces[j].y1;
                piece.x1 = ctx.pieces[j].x0;
                piece.y1 = ctx.pieces[j].y0;
            }
            ctx.pieces[kept++] = piece;
        }
        ctx.piece_count = kept;
    }

    ID2D1SimplifiedGeometrySink_SetFillMode(sink, D2D1_FILL_MODE_ALTERNATE);
    if (ok)
    {
        d2d_combine_emit(&ctx, FALSE, sink);
        d2d_combine_emit(&ctx, TRUE, sink);
    }
    for (i = 0; i < ctx.edge_count; ++i)
        free(ctx.edges[i].splits);
    free(ctx.edges);
    free(ctx.pieces);
    return ok ? S_OK : E_OUTOFMEMORY;
}
