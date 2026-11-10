/*
 * PROJECT:     LiberNT Direct2D
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     CPU area coverage of tessellated fill geometry
 * COPYRIGHT:   Copyright 2026 Earzu Chan <contact@earzuchan.me>
 */

#include "d2d1_private.h"


#define D2D_COVERAGE_MAX_POINTS 160

void d2d_coverage_cleanup(struct d2d_coverage *coverage)
{
    free(coverage->accum);
    free(coverage->rowflags);
    free(coverage->flags);
    free(coverage->tiles);
    free(coverage->runs);
    memset(coverage, 0, sizeof(*coverage));
}

static BOOL d2d_coverage_reset(struct d2d_coverage *coverage, int width, int height)
{
    const int tile_cols = (width + 2 + D2D_COVERAGE_TILE - 1) >> D2D_COVERAGE_TILE_SHIFT;
    const int tile_rows = (height + D2D_COVERAGE_TILE - 1) >> D2D_COVERAGE_TILE_SHIFT;
    size_t accum_count = (size_t)(width + 2) * height;
    size_t flags_count = (size_t)tile_cols * tile_rows;
    size_t old_accum = coverage->accum_size, old_flags = coverage->flags_size;
    int height_i;

    if (!d2d_array_reserve((void **)&coverage->accum, &coverage->accum_size, accum_count, sizeof(float)))
        return FALSE;
    if (!d2d_array_reserve((void **)&coverage->flags, &coverage->flags_size, flags_count, sizeof(BYTE)))
        return FALSE;

    /* Only the used prefix of the retained buffers needs clearing: the previous
     * primitive already consumed all of it, so the clear stays proportional to the
     * current draw instead of to the largest one ever seen. */
    memset(coverage->accum, 0, accum_count * sizeof(float));
    memset(coverage->flags, 0, flags_count);
    (void)old_accum;
    (void)old_flags;

    if (!d2d_array_reserve((void **)&coverage->rowflags, &coverage->rowflags_size,
            (size_t)tile_cols * height, sizeof(BYTE)))
        return FALSE;
    memset(coverage->rowflags, 0, (size_t)tile_cols * height);

    coverage->dirty = TRUE;
    coverage->width = width;
    coverage->height = height;
    coverage->tile_cols = tile_cols;
    coverage->tile_rows = tile_rows;
    coverage->rowflag_cols = tile_cols;
    (void)height_i;
    coverage->run_count = 0;
    coverage->edge_count = 0;
    return TRUE;
}

static float d2d_coverage_clamp(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static void d2d_coverage_line(struct d2d_coverage *coverage, float x0, float y0, float x1, float y1)
{
    float dir = 1.0f, dxdy, x, xnext, dy, d, lo, hi, lo_floor, hi_ceil, s, f0, f1, a0, am, a1, a2, xmf, tmp;
    const int stride = coverage->width + 2;
    const float w = coverage->width;
    int y, y_end, i0, i1, i;
    BYTE *flags, *rowflags;
    float *row;

    if (y0 == y1)
        return;
    if (y0 > y1)
    {
        tmp = x0; x0 = x1; x1 = tmp;
        tmp = y0; y0 = y1; y1 = tmp;
        dir = -1.0f;
    }
    if (y1 <= 0.0f || y0 >= coverage->height)
        return;

    dxdy = (x1 - x0) / (y1 - y0);
    x = x0;
    if (y0 < 0.0f)
    {
        x -= y0 * dxdy;
        y0 = 0.0f;
    }

    y_end = (int)ceilf(y1);
    if (y_end > coverage->height)
        y_end = coverage->height;

    for (y = (int)y0; y < y_end; ++y)
    {
        dy = min((float)(y + 1), y1) - max((float)y, y0);
        xnext = x + dxdy * dy;
        d = dy * dir;

        lo = d2d_coverage_clamp(min(x, xnext), 0.0f, w);
        hi = d2d_coverage_clamp(max(x, xnext), 0.0f, w);
        x = xnext;

        row = &coverage->accum[(size_t)y * stride];
        lo_floor = floorf(lo);
        hi_ceil = ceilf(hi);
        i0 = (int)lo_floor;
        i1 = (int)hi_ceil;

        flags = &coverage->flags[(size_t)(y >> D2D_COVERAGE_TILE_SHIFT) * coverage->tile_cols];
        rowflags = &coverage->rowflags[(size_t)y * coverage->rowflag_cols];
        for (i = i0 >> D2D_COVERAGE_TILE_SHIFT; i <= max(i1, i0 + 1) >> D2D_COVERAGE_TILE_SHIFT; ++i)
            flags[i] = rowflags[i] = 1;

        if (i1 <= i0 + 1)
        {
            xmf = 0.5f * (lo + hi) - lo_floor;
            row[i0] += d - d * xmf;
            row[i0 + 1] += d * xmf;
            continue;
        }

        s = 1.0f / (hi - lo);
        f0 = lo - lo_floor;
        a0 = 0.5f * s * (1.0f - f0) * (1.0f - f0);
        f1 = hi - hi_ceil + 1.0f;
        am = 0.5f * s * f1 * f1;
        row[i0] += d * a0;
        if (i1 == i0 + 2)
        {
            row[i0 + 1] += d * (1.0f - a0 - am);
        }
        else
        {
            a1 = s * (1.5f - f0);
            row[i0 + 1] += d * (a1 - a0);
            for (i = i0 + 2; i < i1 - 1; ++i)
                row[i] += d * s;
            a2 = a1 + (i1 - i0 - 3) * s;
            row[i1 - 1] += d * (1.0f - a2 - am);
        }
        row[i1] += d * am;
    }
}

static void d2d_coverage_polygon(struct d2d_coverage *coverage, const D2D1_POINT_2F *p, size_t count)
{
    float area = 0.0f;
    size_t i, next;

    for (i = 0; i < count; ++i)
    {
        next = i + 1 == count ? 0 : i + 1;
        area += p[i].x * p[next].y - p[next].x * p[i].y;
    }
    if (area == 0.0f)
        return;

    for (i = 0; i < count; ++i)
    {
        next = i + 1 == count ? 0 : i + 1;
        if (area > 0.0f)
            d2d_coverage_line(coverage, p[i].x, p[i].y, p[next].x, p[next].y);
        else
            d2d_coverage_line(coverage, p[next].x, p[next].y, p[i].x, p[i].y);
    }
}

static void d2d_coverage_bezier(struct d2d_coverage *coverage, const D2D1_POINT_2F *p0, const D2D1_POINT_2F *p1,
        const D2D1_POINT_2F *p2, float sign)
{
    D2D1_POINT_2F points[D2D_COVERAGE_MAX_POINTS];
    float dx = p0->x - 2.0f * p1->x + p2->x, dy = p0->y - 2.0f * p1->y + p2->y;
    float t, a, b, c;
    size_t n, i, count = 0;

    n = (size_t)ceilf(sqrtf(sqrtf(dx * dx + dy * dy) * 5.0f));
    n = max(n, 2);
    n = min(n, 64);

    if (sign > 0.0f)
    {
        points[count++] = *p0;
        points[count++] = *p1;
        points[count++] = *p2;
        for (i = n - 1; i >= 1; --i)
        {
            t = (float)i / n;
            a = (1.0f - t) * (1.0f - t);
            b = 2.0f * t * (1.0f - t);
            c = t * t;
            points[count].x = a * p0->x + b * p1->x + c * p2->x;
            points[count].y = a * p0->y + b * p1->y + c * p2->y;
            ++count;
        }
    }
    else
    {
        for (i = 0; i <= n; ++i)
        {
            t = (float)i / n;
            a = (1.0f - t) * (1.0f - t);
            b = 2.0f * t * (1.0f - t);
            c = t * t;
            points[count].x = a * p0->x + b * p1->x + c * p2->x;
            points[count].y = a * p0->y + b * p1->y + c * p2->y;
            ++count;
        }
    }

    d2d_coverage_polygon(coverage, points, count);
}

static void d2d_coverage_arc(struct d2d_coverage *coverage, const D2D1_POINT_2F *p0, const D2D1_POINT_2F *p1,
        const D2D1_POINT_2F *p2)
{
    D2D1_POINT_2F points[D2D_COVERAGE_MAX_POINTS], a, b;
    float r, theta, u, v;
    size_t n, i;

    a.x = p1->x - p0->x;
    a.y = p1->y - p0->y;
    b.x = p1->x - p2->x;
    b.y = p1->y - p2->y;
    r = max(sqrtf(a.x * a.x + a.y * a.y), sqrtf(b.x * b.x + b.y * b.y));

    n = (size_t)ceilf(2.5f * sqrtf(r)) + 1;
    n = max(n, 4);
    n = min(n, 64);

    for (i = 0; i <= n; ++i)
    {
        theta = (float)M_PI_2 * (1.0f - (float)i / n);
        u = cosf(theta);
        v = sinf(theta);
        points[i].x = p1->x + (u - 1.0f) * a.x + (v - 1.0f) * b.x;
        points[i].y = p1->y + (u - 1.0f) * a.y + (v - 1.0f) * b.y;
    }

    d2d_coverage_polygon(coverage, points, n + 1);
}

static void d2d_coverage_bounds_add(D2D1_RECT_F *bounds, BOOL *valid, const D2D1_POINT_2F *p,
        const D2D1_MATRIX_3X2_F *m)
{
    D2D1_POINT_2F q;

    d2d_point_transform(&q, m, p->x, p->y);
    if (!*valid)
    {
        bounds->left = bounds->right = q.x;
        bounds->top = bounds->bottom = q.y;
        *valid = TRUE;
        return;
    }
    bounds->left = min(bounds->left, q.x);
    bounds->right = max(bounds->right, q.x);
    bounds->top = min(bounds->top, q.y);
    bounds->bottom = max(bounds->bottom, q.y);
}

BOOL d2d_geometry_get_device_bounds(const struct d2d_geometry *geometry, BOOL outline,
        const D2D1_MATRIX_3X2_F *world, D2D1_RECT_F *bounds)
{
    D2D1_MATRIX_3X2_F m = geometry->transform;
    BOOL valid = FALSE;
    size_t i;

    d2d_matrix_multiply(&m, world);
    if (outline)
    {
        for (i = 0; i < geometry->outline.vertex_count; ++i)
            d2d_coverage_bounds_add(bounds, &valid, &geometry->outline.vertices[i].position, &m);
        for (i = 0; i < geometry->outline.bezier_count; ++i)
            d2d_coverage_bounds_add(bounds, &valid, &geometry->outline.beziers[i].position, &m);
        for (i = 0; i < geometry->outline.arc_count; ++i)
            d2d_coverage_bounds_add(bounds, &valid, &geometry->outline.arcs[i].position, &m);
        return valid;
    }

    for (i = 0; i < geometry->fill.vertex_count; ++i)
        d2d_coverage_bounds_add(bounds, &valid, &geometry->fill.vertices[i], &m);
    for (i = 0; i < geometry->fill.bezier_vertex_count; ++i)
        d2d_coverage_bounds_add(bounds, &valid, &geometry->fill.bezier_vertices[i].position, &m);
    for (i = 0; i < geometry->fill.arc_vertex_count; ++i)
        d2d_coverage_bounds_add(bounds, &valid, &geometry->fill.arc_vertices[i].position, &m);

    return valid;
}

static D2D1_POINT_2F d2d_coverage_direction(const D2D1_MATRIX_3X2_F *g, const D2D1_POINT_2F *v)
{
    D2D1_POINT_2F r;
    float l;

    r.x = g->_11 * v->x + g->_21 * v->y;
    r.y = g->_12 * v->x + g->_22 * v->y;
    l = sqrtf(r.x * r.x + r.y * r.y);
    if (l > 0.0f)
    {
        r.x /= l;
        r.y /= l;
    }
    return r;
}

static BOOL d2d_coverage_outline_vertex(const D2D1_MATRIX_3X2_F *g, const struct d2d_outline_vertex *v,
        float half_width, const D2D1_MATRIX_3X2_F *world, const RECT *area, D2D1_POINT_2F *out)
{
    D2D1_POINT_2F q_prev = d2d_coverage_direction(g, &v->prev), q_next = d2d_coverage_direction(g, &v->next);
    D2D1_POINT_2F v_p, p, q_i;
    float denom, l;

    v_p.x = -q_prev.y;
    v_p.y = q_prev.x;
    denom = 1.0f + q_prev.x * q_next.x + q_prev.y * q_next.y;
    if (fabsf(denom) < 1e-6f)
        return FALSE;
    l = -(v_p.x * q_next.x + v_p.y * q_next.y) / denom;
    q_i.x = l * q_prev.x + v_p.x;
    q_i.y = l * q_prev.y + v_p.y;

    d2d_point_transform(&p, g, v->position.x, v->position.y);
    p.x += half_width * q_i.x;
    p.y += half_width * q_i.y;
    d2d_point_transform(out, world, p.x, p.y);
    out->x -= area->left;
    out->y -= area->top;
    return TRUE;
}

static void d2d_coverage_strip(struct d2d_coverage *coverage, const D2D1_POINT_2F *pts, size_t count,
        float half_width, const D2D1_MATRIX_3X2_F *world, const RECT *area)
{
    D2D1_POINT_2F left[D2D_COVERAGE_MAX_POINTS], right[D2D_COVERAGE_MAX_POINTS], n0, n1, off, poly[4];
    size_t i, j;
    float dx, dy, l, d;

    if (count < 2)
        return;

    n0.x = n0.y = n1.x = n1.y = 0.0f;

    for (i = 0; i < count; ++i)
    {
        if (i)
        {
            dx = pts[i].x - pts[i - 1].x;
            dy = pts[i].y - pts[i - 1].y;
            l = sqrtf(dx * dx + dy * dy);
            n0.x = l > 0.0f ? -dy / l : 0.0f;
            n0.y = l > 0.0f ? dx / l : 0.0f;
        }
        if (i + 1 < count)
        {
            dx = pts[i + 1].x - pts[i].x;
            dy = pts[i + 1].y - pts[i].y;
            l = sqrtf(dx * dx + dy * dy);
            n1.x = l > 0.0f ? -dy / l : 0.0f;
            n1.y = l > 0.0f ? dx / l : 0.0f;
        }
        if (!i)
            n0 = n1;
        if (i + 1 == count)
            n1 = n0;

        d = 1.0f + n0.x * n1.x + n0.y * n1.y;
        d = max(d, 0.25f);
        off.x = (n0.x + n1.x) / d;
        off.y = (n0.y + n1.y) / d;
        left[i].x = pts[i].x + half_width * off.x;
        left[i].y = pts[i].y + half_width * off.y;
        right[i].x = pts[i].x - half_width * off.x;
        right[i].y = pts[i].y - half_width * off.y;
    }

    for (i = 0; i + 1 < count; ++i)
    {
        d2d_point_transform(&poly[0], world, left[i].x, left[i].y);
        d2d_point_transform(&poly[1], world, left[i + 1].x, left[i + 1].y);
        d2d_point_transform(&poly[2], world, right[i + 1].x, right[i + 1].y);
        d2d_point_transform(&poly[3], world, right[i].x, right[i].y);
        for (j = 0; j < 4; ++j)
        {
            poly[j].x -= area->left;
            poly[j].y -= area->top;
        }
        d2d_coverage_polygon(coverage, poly, 4);
    }
}

static size_t d2d_coverage_curve_segments(const D2D1_POINT_2F *p0, const D2D1_POINT_2F *p1,
        const D2D1_POINT_2F *p2, const D2D1_MATRIX_3X2_F *m)
{
    D2D1_POINT_2F a, b, c;
    float dx, dy;
    size_t n;

    d2d_point_transform(&a, m, p0->x, p0->y);
    d2d_point_transform(&b, m, p1->x, p1->y);
    d2d_point_transform(&c, m, p2->x, p2->y);
    dx = a.x - 2.0f * b.x + c.x;
    dy = a.y - 2.0f * b.y + c.y;
    n = (size_t)ceilf(sqrtf(sqrtf(dx * dx + dy * dy) * 5.0f));
    n = max(n, 3);
    return min(n, 60);
}

static void d2d_coverage_stroke_bezier(struct d2d_coverage *coverage, const struct d2d_geometry *geometry,
        const struct d2d_curve_outline_vertex *v, float half_width, const D2D1_MATRIX_3X2_F *world,
        const D2D1_MATRIX_3X2_F *to_device, const RECT *area)
{
    D2D1_POINT_2F pts[D2D_COVERAGE_MAX_POINTS], p;
    size_t n = d2d_coverage_curve_segments(&v->p0, &v->p1, &v->p2, to_device), i;
    float t, a, b, c;

    for (i = 0; i <= n; ++i)
    {
        t = (float)i / n;
        a = (1.0f - t) * (1.0f - t);
        b = 2.0f * t * (1.0f - t);
        c = t * t;
        p.x = a * v->p0.x + b * v->p1.x + c * v->p2.x;
        p.y = a * v->p0.y + b * v->p1.y + c * v->p2.y;
        d2d_point_transform(&pts[i], &geometry->transform, p.x, p.y);
    }
    d2d_coverage_strip(coverage, pts, n + 1, half_width, world, area);
}

static void d2d_coverage_stroke_arc(struct d2d_coverage *coverage, const struct d2d_geometry *geometry,
        const struct d2d_curve_outline_vertex *v, float half_width, const D2D1_MATRIX_3X2_F *world,
        const D2D1_MATRIX_3X2_F *to_device, const RECT *area)
{
    D2D1_POINT_2F pts[D2D_COVERAGE_MAX_POINTS], p, a, b;
    size_t n = d2d_coverage_curve_segments(&v->p0, &v->p1, &v->p2, to_device), i;
    float theta;

    n = max(n, 6);
    a.x = v->p1.x - v->p0.x;
    a.y = v->p1.y - v->p0.y;
    b.x = v->p1.x - v->p2.x;
    b.y = v->p1.y - v->p2.y;
    for (i = 0; i <= n; ++i)
    {
        theta = (float)M_PI_2 * (1.0f - (float)i / n);
        p.x = v->p1.x + (cosf(theta) - 1.0f) * a.x + (sinf(theta) - 1.0f) * b.x;
        p.y = v->p1.y + (cosf(theta) - 1.0f) * a.y + (sinf(theta) - 1.0f) * b.y;
        d2d_point_transform(&pts[i], &geometry->transform, p.x, p.y);
    }
    d2d_coverage_strip(coverage, pts, n + 1, half_width, world, area);
}

static void d2d_coverage_fill(struct d2d_coverage *coverage, const struct d2d_geometry *geometry,
        const D2D1_MATRIX_3X2_F *to_device, const RECT *area)
{
    D2D1_POINT_2F tri[3], p[3];
    size_t i, j;

    for (i = 0; i < geometry->fill.face_count; ++i)
    {
        for (j = 0; j < 3; ++j)
        {
            const D2D1_POINT_2F *v = &geometry->fill.vertices[geometry->fill.faces[i].v[j]];

            d2d_point_transform(&tri[j], to_device, v->x, v->y);
            tri[j].x -= area->left;
            tri[j].y -= area->top;
        }
        d2d_coverage_polygon(coverage, tri, 3);
    }

    for (i = 0; i + 2 < geometry->fill.bezier_vertex_count; i += 3)
    {
        for (j = 0; j < 3; ++j)
        {
            d2d_point_transform(&p[j], to_device, geometry->fill.bezier_vertices[i + j].position.x,
                    geometry->fill.bezier_vertices[i + j].position.y);
            p[j].x -= area->left;
            p[j].y -= area->top;
        }
        d2d_coverage_bezier(coverage, &p[0], &p[1], &p[2], geometry->fill.bezier_vertices[i].texcoord.sign);
    }

    for (i = 0; i + 2 < geometry->fill.arc_vertex_count; i += 3)
    {
        for (j = 0; j < 3; ++j)
        {
            d2d_point_transform(&p[j], to_device, geometry->fill.arc_vertices[i + j].position.x,
                    geometry->fill.arc_vertices[i + j].position.y);
            p[j].x -= area->left;
            p[j].y -= area->top;
        }
        d2d_coverage_arc(coverage, &p[0], &p[1], &p[2]);
    }
}

static void d2d_coverage_stroke(struct d2d_coverage *coverage, const struct d2d_geometry *geometry,
        float stroke_width, const D2D1_MATRIX_3X2_F *world, const D2D1_MATRIX_3X2_F *to_device, const RECT *area)
{
    const float half_width = stroke_width * 0.5f;
    D2D1_POINT_2F tri[3];
    size_t i, j;

    for (i = 0; i < geometry->outline.face_count; ++i)
    {
        BOOL ok = TRUE;

        for (j = 0; j < 3 && ok; ++j)
            ok = d2d_coverage_outline_vertex(&geometry->transform,
                    &geometry->outline.vertices[geometry->outline.faces[i].v[j]], half_width, world, area, &tri[j]);
        if (ok)
            d2d_coverage_polygon(coverage, tri, 3);
    }

    for (i = 0; i + 6 < geometry->outline.bezier_count; i += 7)
        d2d_coverage_stroke_bezier(coverage, geometry, &geometry->outline.beziers[i], half_width, world,
                to_device, area);
    for (i = 0; i + 4 < geometry->outline.arc_count; i += 5)
        d2d_coverage_stroke_arc(coverage, geometry, &geometry->outline.arcs[i], half_width, world,
                to_device, area);
}

BOOL d2d_rect_is_pixel_aligned(const D2D1_RECT_F *rect, const D2D1_MATRIX_3X2_F *world)
{
    D2D1_POINT_2F p[4];
    float min_x, max_x, min_y, max_y;
    size_t i;

    d2d_point_transform(&p[0], world, rect->left, rect->top);
    d2d_point_transform(&p[1], world, rect->left, rect->bottom);
    d2d_point_transform(&p[2], world, rect->right, rect->top);
    d2d_point_transform(&p[3], world, rect->right, rect->bottom);

    min_x = max_x = p[0].x;
    min_y = max_y = p[0].y;
    for (i = 1; i < 4; ++i)
    {
        min_x = min(min_x, p[i].x);
        max_x = max(max_x, p[i].x);
        min_y = min(min_y, p[i].y);
        max_y = max(max_y, p[i].y);
    }

    if (fabsf(min_x - roundf(min_x)) > 1e-3f || fabsf(max_x - roundf(max_x)) > 1e-3f
            || fabsf(min_y - roundf(min_y)) > 1e-3f || fabsf(max_y - roundf(max_y)) > 1e-3f)
        return FALSE;

    for (i = 0; i < 4; ++i)
    {
        if (fabsf(p[i].x - min_x) > 1e-3f && fabsf(p[i].x - max_x) > 1e-3f)
            return FALSE;
        if (fabsf(p[i].y - min_y) > 1e-3f && fabsf(p[i].y - max_y) > 1e-3f)
            return FALSE;
    }

    return TRUE;
}

BOOL d2d_geometry_is_pixel_aligned_rect(const struct d2d_geometry *geometry, const D2D1_MATRIX_3X2_F *world)
{
    D2D1_MATRIX_3X2_F m = geometry->transform;
    D2D1_POINT_2F p[6];
    float min_x, max_x, min_y, max_y;
    size_t i;

    if (geometry->fill.face_count != 2 || geometry->fill.bezier_vertex_count || geometry->fill.arc_vertex_count)
        return FALSE;

    d2d_matrix_multiply(&m, world);
    for (i = 0; i < 6; ++i)
    {
        const D2D1_POINT_2F *v = &geometry->fill.vertices[geometry->fill.faces[i / 3].v[i % 3]];

        d2d_point_transform(&p[i], &m, v->x, v->y);
    }

    min_x = max_x = p[0].x;
    min_y = max_y = p[0].y;
    for (i = 1; i < 6; ++i)
    {
        min_x = min(min_x, p[i].x);
        max_x = max(max_x, p[i].x);
        min_y = min(min_y, p[i].y);
        max_y = max(max_y, p[i].y);
    }

    if (fabsf(min_x - roundf(min_x)) > 1e-3f || fabsf(max_x - roundf(max_x)) > 1e-3f
            || fabsf(min_y - roundf(min_y)) > 1e-3f || fabsf(max_y - roundf(max_y)) > 1e-3f)
        return FALSE;

    for (i = 0; i < 6; ++i)
    {
        if (fabsf(p[i].x - min_x) > 1e-3f && fabsf(p[i].x - max_x) > 1e-3f)
            return FALSE;
        if (fabsf(p[i].y - min_y) > 1e-3f && fabsf(p[i].y - max_y) > 1e-3f)
            return FALSE;
    }

    return TRUE;
}

static float d2d_coverage_overlap(float lo, float hi, float clip_lo, float clip_hi)
{
    return d2d_coverage_clamp(min(hi, clip_hi) - max(lo, clip_lo), 0.0f, 1.0f);
}

BOOL d2d_coverage_clip_is_fractional(const D2D1_RECT_F *clip)
{
    return fabsf(clip->left - roundf(clip->left)) > 1e-3f || fabsf(clip->right - roundf(clip->right)) > 1e-3f
            || fabsf(clip->top - roundf(clip->top)) > 1e-3f || fabsf(clip->bottom - roundf(clip->bottom)) > 1e-3f;
}

static BYTE d2d_coverage_byte(float acc)
{
    return (BYTE)(min(fabsf(acc), 1.0f) * 255.0f + 0.5f);
}

static int d2d_coverage_clip_state(const D2D1_RECT_F *clip, float x0, float y0, float x1, float y1)
{
    if (!clip)
        return 1;
    if (x1 <= clip->left || x0 >= clip->right || y1 <= clip->top || y0 >= clip->bottom)
        return 0;
    if (x0 >= clip->left && x1 <= clip->right && y0 >= clip->top && y1 <= clip->bottom)
        return 1;
    return 2;
}

static BOOL d2d_coverage_add_run(struct d2d_coverage *coverage, int x, int y, int width, int height, BOOL edge)
{
    struct d2d_coverage_run *run;

    if (coverage->run_count)
    {
        run = &coverage->runs[coverage->run_count - 1];
        if (run->edge == edge && run->y == y && run->height == height && run->x + run->width == x)
        {
            run->width += width;
            return TRUE;
        }
    }

    if (!d2d_array_reserve((void **)&coverage->runs, &coverage->runs_size, coverage->run_count + 1, sizeof(*run)))
        return FALSE;
    run = &coverage->runs[coverage->run_count++];
    run->x = x;
    run->y = y;
    run->width = width;
    run->height = height;
    run->edge = edge;
    run->tile = coverage->edge_count;
    return TRUE;
}

static BOOL d2d_coverage_resolve(struct d2d_coverage *coverage, const RECT *area, const D2D1_RECT_F *clip)
{
    const int width = coverage->width, height = coverage->height, stride = width + 2;
    const int visible_cols = (width + D2D_COVERAGE_TILE - 1) >> D2D_COVERAGE_TILE_SHIFT;
    BYTE tile[D2D_COVERAGE_TILE * D2D_COVERAGE_TILE], rows_value[D2D_COVERAGE_TILE];
    float acc[D2D_COVERAGE_TILE], cy, cx;
    int tx, ty, x0, y0, cols, rows, r, c, state;
    BOOL edge, uniform;
    float *cell;
    BYTE *flags;
    float a;

    for (ty = 0; ty < coverage->tile_rows; ++ty)
    {
        y0 = ty << D2D_COVERAGE_TILE_SHIFT;
        rows = min(D2D_COVERAGE_TILE, height - y0);
        flags = &coverage->flags[(size_t)ty * coverage->tile_cols];
        memset(acc, 0, sizeof(acc));

        for (tx = 0; tx < visible_cols; ++tx)
        {
            x0 = tx << D2D_COVERAGE_TILE_SHIFT;
            cols = min(D2D_COVERAGE_TILE, width - x0);

            /* A tile is only worth materialising when some edge span actually wrote into
             * it. Rows of a flagged tile that no span touches keep their accumulated
             * prefix value across the whole tile, so they can be filled with a single
             * memset instead of a load/add/convert per pixel. Tiles with no touched row
             * at all degrade to a plain run, which is the common interior case. */
            /* A tile is only worth materialising when some edge span actually wrote into
             * it. Inside such a tile, only the columns covered by this row's spans need a
             * per-pixel conversion; the rest of the row keeps its accumulated prefix
             * value and is filled with a single memset. Tiles with no touched row at all
             * degrade to a plain run, which is the common interior case. */
            if (flags[tx])
            {
                BOOL touched = FALSE;

                for (r = 0; r < rows; ++r)
                    if (coverage->rowflags[(size_t)(y0 + r) * coverage->rowflag_cols + tx])
                    {
                        touched = TRUE;
                        break;
                    }

                if (touched)
                {
                    for (r = 0; r < rows; ++r)
                    {
                        const int y = y0 + r;
                        float *line = &coverage->accum[(size_t)y * stride + x0];
                        int k;

                        a = acc[r];
                        if (!coverage->rowflags[(size_t)y * coverage->rowflag_cols + tx])
                        {
                            memset(&tile[r * D2D_COVERAGE_TILE], d2d_coverage_byte(a), cols);
                            acc[r] = a;
                            continue;
                        }
                        for (c = 0; c < cols; ++c)
                        {
                            a += line[c];
                            tile[r * D2D_COVERAGE_TILE + c] = d2d_coverage_byte(a);
                        }
                        acc[r] = a;
                        acc[r] = a;
                        k = min(D2D_COVERAGE_TILE, stride - x0);
                        memset(line, 0, k * sizeof(*line));
                    }
                    flags[tx] = 0;
                    edge = TRUE;
                }
                else
                {
                    uniform = TRUE;
                    for (r = 0; r < rows; ++r)
                    {
                        rows_value[r] = d2d_coverage_byte(acc[r]);
                        uniform &= rows_value[r] == rows_value[0];
                    }
                    state = d2d_coverage_clip_state(clip, area->left + x0, area->top + y0,
                            area->left + x0 + cols, area->top + y0 + rows);
                    if (uniform && rows_value[0] == 255 && state == 1)
                    {
                        if (!d2d_coverage_add_run(coverage, x0, y0, cols, rows, FALSE))
                            return FALSE;
                        continue;
                    }
                    for (r = 0; r < rows; ++r)
                        memset(&tile[r * D2D_COVERAGE_TILE], rows_value[r], cols);
                    edge = TRUE;
                    if (!state)
                        continue;
                    if (state == 2)
                    {
                        for (r = 0; r < rows; ++r)
                        {
                            cy = d2d_coverage_overlap(area->top + y0 + r, area->top + y0 + r + 1.0f,
                                    clip->top, clip->bottom);
                            for (c = 0; c < cols; ++c)
                            {
                                BYTE *p = &tile[r * D2D_COVERAGE_TILE + c];

                                if (!*p)
                                    continue;
                                cx = d2d_coverage_overlap(area->left + x0 + c, area->left + x0 + c + 1.0f,
                                        clip->left, clip->right);
                                *p = (BYTE)(*p * cx * cy + 0.5f);
                            }
                        }
                    }
                    if (!d2d_coverage_add_run(coverage, x0, y0, cols, rows, edge)
                            || !d2d_array_reserve((void **)&coverage->tiles, &coverage->tiles_size,
                            (coverage->edge_count + 1) * sizeof(tile), sizeof(BYTE)))
                        return FALSE;
                    memcpy(&coverage->tiles[coverage->edge_count++ * sizeof(tile)], tile, sizeof(tile));
                    continue;
                }
            }
            else
            {
                uniform = TRUE;
                for (r = 0; r < rows; ++r)
                {
                    rows_value[r] = d2d_coverage_byte(acc[r]);
                    uniform &= rows_value[r] == rows_value[0];
                }
                state = d2d_coverage_clip_state(clip, area->left + x0, area->top + y0,
                        area->left + x0 + cols, area->top + y0 + rows);
                if (uniform && !rows_value[0])
                    continue;
                if (uniform && rows_value[0] == 255 && state == 1)
                {
                    if (!d2d_coverage_add_run(coverage, x0, y0, cols, rows, FALSE))
                        return FALSE;
                    continue;
                }
                for (r = 0; r < rows; ++r)
                    memset(&tile[r * D2D_COVERAGE_TILE], rows_value[r], cols);
                edge = TRUE;
            }

            state = d2d_coverage_clip_state(clip, area->left + x0, area->top + y0,
                    area->left + x0 + cols, area->top + y0 + rows);
            if (!state)
                continue;
            if (state == 2)
            {
                for (r = 0; r < rows; ++r)
                {
                    cy = d2d_coverage_overlap(area->top + y0 + r, area->top + y0 + r + 1.0f,
                            clip->top, clip->bottom);
                    for (c = 0; c < cols; ++c)
                    {
                        BYTE *p = &tile[r * D2D_COVERAGE_TILE + c];

                        if (!*p)
                            continue;
                        cx = d2d_coverage_overlap(area->left + x0 + c, area->left + x0 + c + 1.0f,
                                clip->left, clip->right);
                        *p = (BYTE)(*p * cx * cy + 0.5f);
                    }
                }
            }

            if (!d2d_coverage_add_run(coverage, x0, y0, cols, rows, edge)
                    || !d2d_array_reserve((void **)&coverage->tiles, &coverage->tiles_size,
                    (coverage->edge_count + 1) * sizeof(tile), sizeof(BYTE)))
                return FALSE;
            memcpy(&coverage->tiles[coverage->edge_count++ * sizeof(tile)], tile, sizeof(tile));
        }

        for (tx = visible_cols; tx < coverage->tile_cols; ++tx)
        {
            if (!flags[tx])
                continue;
            x0 = tx << D2D_COVERAGE_TILE_SHIFT;
            for (r = 0; r < rows; ++r)
            {
                cell = &coverage->accum[(size_t)(y0 + r) * stride + x0];
                memset(cell, 0, min(D2D_COVERAGE_TILE, stride - x0) * sizeof(*cell));
            }
            flags[tx] = 0;
        }
    }

    coverage->dirty = FALSE;
    return TRUE;
}

BOOL d2d_geometry_rasterize(const struct d2d_geometry *geometry, BOOL outline, float stroke_width,
        const D2D1_MATRIX_3X2_F *world, const RECT *area, const D2D1_RECT_F *clip, struct d2d_coverage *coverage)
{
    const int width = area->right - area->left, height = area->bottom - area->top;
    D2D1_MATRIX_3X2_F to_device = geometry->transform;

    if (width <= 0 || height <= 0 || !d2d_coverage_reset(coverage, width, height))
        return FALSE;

    d2d_matrix_multiply(&to_device, world);
    if (outline)
        d2d_coverage_stroke(coverage, geometry, stroke_width, world, &to_device, area);
    else
        d2d_coverage_fill(coverage, geometry, &to_device, area);
    return d2d_coverage_resolve(coverage, area, clip);
}

static BOOL d2d_convex_add_point(D2D1_POINT_2F *pts, size_t *count, float x, float y)
{
    if (*count && fabsf(pts[*count - 1].x - x) < 1e-4f && fabsf(pts[*count - 1].y - y) < 1e-4f)
        return TRUE;
    if (*count >= D2D_CONVEX_MAX_POINTS)
        return FALSE;
    pts[*count].x = x;
    pts[*count].y = y;
    ++*count;
    return TRUE;
}

static BOOL d2d_convex_orient(D2D1_POINT_2F *a, D2D1_POINT_2F *b, size_t count)
{
    float area = 0.0f;
    D2D1_POINT_2F q;
    size_t i, j;

    for (i = 0; i < count; ++i)
    {
        j = i + 1 == count ? 0 : i + 1;
        area += a[i].x * a[j].y - a[j].x * a[i].y;
    }
    if (fabsf(area) < 1e-3f)
        return FALSE;
    if (area > 0.0f)
        return TRUE;

    for (i = 0; i < count / 2; ++i)
    {
        q = a[i];
        a[i] = a[count - 1 - i];
        a[count - 1 - i] = q;
        if (b)
        {
            q = b[i];
            b[i] = b[count - 1 - i];
            b[count - 1 - i] = q;
        }
    }
    return TRUE;
}

static BOOL d2d_convex_offset(const D2D1_POINT_2F *p, size_t count, float d,
        D2D1_POINT_2F *grow, D2D1_POINT_2F *shrink)
{
    D2D1_POINT_2F n[D2D_CONVEX_MAX_POINTS];
    float dx, dy, len, m;
    size_t i, j;

    for (i = 0; i < count; ++i)
    {
        j = i + 1 == count ? 0 : i + 1;
        dx = p[j].x - p[i].x;
        dy = p[j].y - p[i].y;
        len = sqrtf(dx * dx + dy * dy);
        if (len < 1e-4f)
            return FALSE;
        n[i].x = dy / len;
        n[i].y = -dx / len;
    }

    for (i = 0; i < count; ++i)
    {
        const D2D1_POINT_2F *a = &n[i ? i - 1 : count - 1], *b = &n[i];

        m = 1.0f + a->x * b->x + a->y * b->y;
        if (m < 0.1f)
            return FALSE;
        m = d / m;
        grow[i].x = p[i].x + (a->x + b->x) * m;
        grow[i].y = p[i].y + (a->y + b->y) * m;
        shrink[i].x = p[i].x - (a->x + b->x) * m;
        shrink[i].y = p[i].y - (a->y + b->y) * m;
    }

    for (i = 0; i < count; ++i)
    {
        j = i + 1 == count ? 0 : i + 1;
        if ((shrink[j].x - shrink[i].x) * (p[j].x - p[i].x) + (shrink[j].y - shrink[i].y) * (p[j].y - p[i].y) <= 0.0f)
            return FALSE;
    }
    return TRUE;
}

static size_t d2d_convex_axis_aligned(const D2D1_POINT_2F *p, size_t count, const D2D1_MATRIX_3X2_F *from_device,
        struct d2d_aa_vertex *vertices, struct d2d_face *faces, size_t *face_count)
{
    float x0, x1, y0, y1, xs[4], ys[4], cx, cy, a;
    size_t i, j, k, nx = 0, ny = 0, vc = 0, fc = 0;
    D2D1_POINT_2F q;

    if (count != 4)
        return 0;
    x0 = x1 = p[0].x;
    y0 = y1 = p[0].y;
    for (i = 1; i < 4; ++i)
    {
        x0 = min(x0, p[i].x);
        x1 = max(x1, p[i].x);
        y0 = min(y0, p[i].y);
        y1 = max(y1, p[i].y);
    }
    for (i = 0; i < 4; ++i)
    {
        if (fabsf(p[i].x - x0) > 1e-3f && fabsf(p[i].x - x1) > 1e-3f)
            return 0;
        if (fabsf(p[i].y - y0) > 1e-3f && fabsf(p[i].y - y1) > 1e-3f)
            return 0;
    }
    if (x1 - x0 < 1e-3f || y1 - y0 < 1e-3f)
        return 0;

    xs[nx++] = floorf(x0);
    xs[nx++] = ceilf(x0);
    xs[nx++] = floorf(x1);
    xs[nx++] = ceilf(x1);
    ys[ny++] = floorf(y0);
    ys[ny++] = ceilf(y0);
    ys[ny++] = floorf(y1);
    ys[ny++] = ceilf(y1);

    for (i = 0; i < 3; ++i)
    {
        float cw = xs[i + 1] - xs[i];

        if (cw <= 0.0f || (i == 2 && xs[2] == xs[0]))
            continue;
        cx = d2d_coverage_clamp(min(xs[i + 1], x1) - max(xs[i], x0), 0.0f, cw) / (cw > 1.0f ? cw : 1.0f);
        if (cx <= 0.0f)
            continue;
        for (j = 0; j < 3; ++j)
        {
            float ch = ys[j + 1] - ys[j];

            if (ch <= 0.0f || (j == 2 && ys[2] == ys[0]))
                continue;
            cy = d2d_coverage_clamp(min(ys[j + 1], y1) - max(ys[j], y0), 0.0f, ch) / (ch > 1.0f ? ch : 1.0f);
            if (cy <= 0.0f)
                continue;
            a = cx * cy;
            for (k = 0; k < 4; ++k)
            {
                d2d_point_transform(&q, from_device, xs[i + (k >> 1)], ys[j + (k & 1)]);
                vertices[vc + k].x = q.x;
                vertices[vc + k].y = q.y;
                vertices[vc + k].a = a;
            }
            faces[fc].v[0] = vc;
            faces[fc].v[1] = vc + 1;
            faces[fc++].v[2] = vc + 2;
            faces[fc].v[0] = vc + 2;
            faces[fc].v[1] = vc + 1;
            faces[fc++].v[2] = vc + 3;
            vc += 4;
        }
    }

    *face_count = fc;
    return vc;
}

size_t d2d_convex_aa_polygon(D2D1_POINT_2F *local, size_t count, const D2D1_MATRIX_3X2_F *geometry_transform,
        const D2D1_MATRIX_3X2_F *world, const D2D1_MATRIX_3X2_F *from_device, float stroke_width,
        struct d2d_aa_vertex *vertices, struct d2d_face *faces, size_t *face_count)
{
    D2D1_POINT_2F ring[2][D2D_CONVEX_MAX_POINTS], band[4][D2D_CONVEX_MAX_POINTS], q;
    D2D1_MATRIX_3X2_F to_device = *geometry_transform;
    size_t i, j, k, base, rings = 1;
    float alpha = 1.0f, width;

    d2d_matrix_multiply(&to_device, world);
    if (count < 3)
        return 0;

    if (stroke_width <= 0.0f)
    {
        for (i = 0; i < count; ++i)
            d2d_point_transform(&ring[0][i], &to_device, local[i].x, local[i].y);
        if (count == 4 && (j = d2d_convex_axis_aligned(ring[0], count, from_device, vertices, faces, face_count)))
            return j;
        if (!d2d_convex_orient(ring[0], NULL, count))
            return 0;
    }
    else
    {
        float a = world->_11, b = world->_12, c = world->_21, d = world->_22;
        float t = a * a + b * b + c * c + d * d, det = a * d - b * c;
        float s_min = sqrtf(max(0.5f * (t - sqrtf(max(t * t - 4.0f * det * det, 0.0f))), 0.0f));

        width = stroke_width * s_min;
        if (width < 0.05f)
            return 0;
        if (width < 1.0f)
        {
            alpha = width;
            stroke_width /= width;
        }

        for (i = 0; i < count; ++i)
            d2d_point_transform(&local[i], geometry_transform, local[i].x, local[i].y);
        if (!d2d_convex_orient(local, NULL, count))
            return 0;
        if (!d2d_convex_offset(local, count, stroke_width * 0.5f, band[0], band[1]))
            return 0;
        for (i = 0; i < count; ++i)
        {
            d2d_point_transform(&ring[0][i], world, band[0][i].x, band[0][i].y);
            d2d_point_transform(&ring[1][i], world, band[1][i].x, band[1][i].y);
        }
        if (!d2d_convex_orient(ring[0], ring[1], count))
            return 0;
        rings = 2;
    }

    for (k = 0; k < rings; ++k)
    {
        if (!d2d_convex_offset(ring[k], count, 0.5f, band[k * 2], band[k * 2 + 1]))
            return 0;
    }

    if (rings == 1)
    {
        for (i = 0; i < count; ++i)
        {
            d2d_point_transform(&q, from_device, band[1][i].x, band[1][i].y);
            vertices[i].x = q.x;
            vertices[i].y = q.y;
            vertices[i].a = 1.0f;
            d2d_point_transform(&q, from_device, band[0][i].x, band[0][i].y);
            vertices[count + i].x = q.x;
            vertices[count + i].y = q.y;
            vertices[count + i].a = 0.0f;
        }

        k = 0;
        for (i = 1; i + 1 < count; ++i)
        {
            faces[k].v[0] = 0;
            faces[k].v[1] = i;
            faces[k++].v[2] = i + 1;
        }
        base = 0;
    }
    else
    {
        static const unsigned int source[4] = {0, 1, 2, 3};
        static const float cover[4] = {0.0f, 1.0f, 1.0f, 0.0f};

        for (k = 0; k < 4; ++k)
        {
            const D2D1_POINT_2F *pts = band[source[k]];

            for (i = 0; i < count; ++i)
            {
                d2d_point_transform(&q, from_device, pts[i].x, pts[i].y);
                vertices[k * count + i].x = q.x;
                vertices[k * count + i].y = q.y;
                vertices[k * count + i].a = cover[k] * alpha;
            }
        }
        k = 0;
        base = 0;
    }

    for (i = 0; i < count; ++i)
    {
        j = i + 1 == count ? 0 : i + 1;
        for (base = 0; base < (rings == 1 ? 1u : 3u); ++base)
        {
            const size_t lo = base * count, hi = (base + 1) * count;

            faces[k].v[0] = lo + i;
            faces[k].v[1] = lo + j;
            faces[k++].v[2] = hi + j;
            faces[k].v[0] = lo + i;
            faces[k].v[1] = hi + j;
            faces[k++].v[2] = hi + i;
        }
    }
    *face_count = k;

    return count * (rings == 1 ? 2 : 4);
}

BOOL d2d_geometry_polygon_aa(const struct d2d_geometry *geometry, const D2D1_MATRIX_3X2_F *world,
        const D2D1_MATRIX_3X2_F *from_device, struct d2d_aa_vertex **out_vertices, size_t *vertex_count,
        struct d2d_face **out_faces, size_t *face_count)
{
    D2D1_MATRIX_3X2_F to_device = geometry->transform;
    D2D1_POINT_2F *p, *grow, *shrink, *normal, q;
    struct d2d_aa_vertex *vertices;
    struct d2d_face *faces;
    const D2D1_POINT_2F *src;
    const struct d2d_face *src_faces;
    const size_t *contour_sizes;
    size_t total, src_face_count, contour_count, c, base, n, i, j, k, band;

    if (geometry->flat.vertex_count)
    {
        total = geometry->flat.vertex_count;
        src = geometry->flat.vertices;
        src_faces = geometry->flat.faces;
        src_face_count = geometry->flat.face_count;
        contour_sizes = geometry->flat.contours;
        contour_count = geometry->flat.contour_count;
    }
    else
    {
        total = geometry->fill.vertex_count;
        src = geometry->fill.vertices;
        src_faces = geometry->fill.faces;
        src_face_count = geometry->fill.face_count;
        contour_sizes = geometry->fill_contours;
        contour_count = geometry->fill_contour_count;
        if (!geometry->fill_polygon)
            return FALSE;
    }
    if (!contour_sizes || total < 3)
        return FALSE;

    d2d_matrix_multiply(&to_device, world);
    if (geometry->flat.vertex_count)
    {
        float scale = sqrtf(to_device._11 * to_device._11 + to_device._12 * to_device._12
                + to_device._21 * to_device._21 + to_device._22 * to_device._22);

        if (scale > geometry->flat.max_scale)
            return FALSE;
    }
    if (!(p = malloc(4 * total * sizeof(*p))))
        return FALSE;
    grow = p + total;
    shrink = grow + total;
    normal = shrink + total;
    if (!(vertices = malloc(2 * total * sizeof(*vertices))))
    {
        free(p);
        return FALSE;
    }
    if (!(faces = malloc((src_face_count + 2 * total) * sizeof(*faces))))
    {
        free(vertices);
        free(p);
        return FALSE;
    }

    for (i = 0; i < total; ++i)
        d2d_point_transform(&p[i], &to_device, src[i].x, src[i].y);

    for (c = 0, base = 0; c < contour_count; base += n, ++c)
    {
        const D2D1_POINT_2F *pc = &p[base];
        D2D1_POINT_2F *gc = &grow[base], *sc = &shrink[base], *nc = &normal[base];
        double area = 0.0;
        float d;

        n = contour_sizes[c];
        if (n < 3)
            goto fail;
        for (i = 0; i < n; ++i)
        {
            j = i + 1 == n ? 0 : i + 1;
            area += (double)pc[i].x * pc[j].y - (double)pc[j].x * pc[i].y;
        }
        if (fabs(area) < 2e-3)
            goto fail;
        d = area > 0.0 ? 0.5f : -0.5f;

        for (i = 0; i < n; ++i)
        {
            float dx, dy, len;

            j = i + 1 == n ? 0 : i + 1;
            dx = pc[j].x - pc[i].x;
            dy = pc[j].y - pc[i].y;
            len = sqrtf(dx * dx + dy * dy);
            if (len < 1e-4f)
                goto fail;
            nc[i].x = dy / len;
            nc[i].y = -dx / len;
        }
        for (i = 0; i < n; ++i)
        {
            const D2D1_POINT_2F *a = &nc[i ? i - 1 : n - 1], *b = &nc[i];
            float m = 1.0f + a->x * b->x + a->y * b->y;

            if (m < 0.9f)
                goto fail;
            m = d / m;
            gc[i].x = pc[i].x + (a->x + b->x) * m;
            gc[i].y = pc[i].y + (a->y + b->y) * m;
            sc[i].x = pc[i].x - (a->x + b->x) * m;
            sc[i].y = pc[i].y - (a->y + b->y) * m;
        }
        for (i = 0; i < n; ++i)
        {
            float ex, ey;

            j = i + 1 == n ? 0 : i + 1;
            ex = pc[j].x - pc[i].x;
            ey = pc[j].y - pc[i].y;
            if ((sc[j].x - sc[i].x) * ex + (sc[j].y - sc[i].y) * ey <= 0.0f
                    || (gc[j].x - gc[i].x) * ex + (gc[j].y - gc[i].y) * ey <= 0.0f)
                goto fail;
        }
    }

    for (i = 0; i < total; ++i)
    {
        d2d_point_transform(&q, from_device, shrink[i].x, shrink[i].y);
        vertices[i].x = q.x;
        vertices[i].y = q.y;
        vertices[i].a = 1.0f;
        d2d_point_transform(&q, from_device, grow[i].x, grow[i].y);
        vertices[total + i].x = q.x;
        vertices[total + i].y = q.y;
        vertices[total + i].a = 0.0f;
    }

    for (k = 0; k < src_face_count; ++k)
        faces[k] = src_faces[k];
    for (c = 0, base = 0; c < contour_count; base += n, ++c)
    {
        n = contour_sizes[c];
        for (i = 0; i < n; ++i)
        {
            j = i + 1 == n ? 0 : i + 1;
            band = base + i;
            faces[k].v[0] = band;
            faces[k].v[1] = base + j;
            faces[k++].v[2] = total + base + j;
            faces[k].v[0] = band;
            faces[k].v[1] = total + base + j;
            faces[k++].v[2] = total + band;
        }
    }

    free(p);
    *out_vertices = vertices;
    *vertex_count = 2 * total;
    *out_faces = faces;
    *face_count = k;
    return TRUE;

fail:
    free(faces);
    free(vertices);
    free(p);
    return FALSE;
}

size_t d2d_geometry_convex_aa(const struct d2d_geometry *geometry, const D2D1_MATRIX_3X2_F *world,
        const D2D1_MATRIX_3X2_F *from_device, float stroke_width, struct d2d_aa_vertex *vertices,
        struct d2d_face *faces, size_t *face_count)
{
    static const float start[4] = {180.0f, 270.0f, 0.0f, 90.0f};
    const float rx = geometry->convex.radius_x, ry = geometry->convex.radius_y;
    const D2D1_RECT_F *r = &geometry->convex.rect;
    D2D1_POINT_2F local[D2D_CONVEX_MAX_POINTS];
    const float centre_x[4] = {r->left + rx, r->right - rx, r->right - rx, r->left + rx};
    const float centre_y[4] = {r->top + ry, r->top + ry, r->bottom - ry, r->bottom - ry};
    D2D1_MATRIX_3X2_F to_device = geometry->transform;
    size_t i, k, count = 0, segments = 1;
    float scale, theta;

    if (!geometry->convex.valid || r->right - r->left <= 0.0f || r->bottom - r->top <= 0.0f)
        return 0;
    d2d_matrix_multiply(&to_device, world);

    if (rx > 0.0f && ry > 0.0f)
    {
        scale = fabsf(to_device._11) + fabsf(to_device._12) + fabsf(to_device._21) + fabsf(to_device._22);
        scale *= max(rx, ry);
        segments = (size_t)ceilf(2.5f * sqrtf(scale));
        segments = min(max(segments, 2), 32);
    }

    for (k = 0; k < 4; ++k)
    {
        if (rx > 0.0f && ry > 0.0f)
        {
            for (i = 0; i <= segments; ++i)
            {
                theta = (start[k] + 90.0f * i / segments) * (float)M_PI / 180.0f;
                if (!d2d_convex_add_point(local, &count, centre_x[k] + rx * cosf(theta),
                        centre_y[k] + ry * sinf(theta)))
                    return 0;
            }
        }
        else if (!d2d_convex_add_point(local, &count, k == 0 || k == 3 ? r->left : r->right,
                k < 2 ? r->top : r->bottom))
        {
            return 0;
        }
    }
    while (count > 1 && fabsf(local[0].x - local[count - 1].x) < 1e-4f
            && fabsf(local[0].y - local[count - 1].y) < 1e-4f)
        --count;
    return d2d_convex_aa_polygon(local, count, &geometry->transform, world, from_device, stroke_width,
            vertices, faces, face_count);
}
