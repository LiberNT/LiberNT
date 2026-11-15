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

#define D2D_PATH_CACHE_SETS 256
#define D2D_PATH_CACHE_MAX_KEY (256 * 1024)

struct d2d_path_cache_figure
{
    D2D1_POINT_2F *vertices;
    enum d2d_vertex_type *types;
    size_t vertex_count;
    D2D1_POINT_2F *controls;
    size_t control_count;
    D2D1_POINT_2F *original;
    size_t original_count;
    D2D1_RECT_F bounds;
};

struct d2d_path_cache_entry
{
    UINT64 hash;
    unsigned char *key;
    size_t key_size;
    ULONG stamp;
    size_t figure_count;
    struct d2d_path_cache_figure *figures;
    D2D1_POINT_2F *fill_vertices;
    size_t fill_vertex_count;
    struct d2d_face *faces;
    size_t face_count;
    struct d2d_curve_vertex *beziers;
    size_t bezier_count;
    BOOL fill_polygon;
    size_t *contours;
    size_t contour_count;
    struct d2d_flat flat;
};

static struct d2d_path_cache_entry d2d_path_cache[D2D_PATH_CACHE_SETS][2];
static SRWLOCK d2d_path_cache_lock = SRWLOCK_INIT;
static ULONG d2d_path_cache_clock;

static void d2d_path_cache_entry_clear(struct d2d_path_cache_entry *entry)
{
    size_t i;

    for (i = 0; i < entry->figure_count; ++i)
    {
        free(entry->figures[i].vertices);
        free(entry->figures[i].types);
        free(entry->figures[i].controls);
        free(entry->figures[i].original);
    }
    free(entry->figures);
    free(entry->fill_vertices);
    free(entry->faces);
    free(entry->beziers);
    free(entry->contours);
    free(entry->flat.vertices);
    free(entry->flat.faces);
    free(entry->flat.contours);
    free(entry->key);
    memset(entry, 0, sizeof(*entry));
}

void d2d_path_cache_free(void)
{
    size_t i, j;

    AcquireSRWLockExclusive(&d2d_path_cache_lock);
    for (i = 0; i < D2D_PATH_CACHE_SETS; ++i)
        for (j = 0; j < 2; ++j)
            d2d_path_cache_entry_clear(&d2d_path_cache[i][j]);
    ReleaseSRWLockExclusive(&d2d_path_cache_lock);
}

static void *d2d_dup(const void *src, size_t size)
{
    void *p;

    if (!size)
        return NULL;
    if ((p = malloc(size)))
        memcpy(p, src, size);
    return p;
}

unsigned char *d2d_path_cache_make_key(const struct d2d_geometry *geometry, size_t *key_size, UINT64 *hash)
{
    size_t size = 2 * sizeof(UINT32), i, pos = 0;
    unsigned char *key;
    UINT64 h = 1469598103934665603ull;
    UINT32 v;

    for (i = 0; i < geometry->u.path.figure_count; ++i)
    {
        const struct d2d_figure *f = &geometry->u.path.figures[i];

        size += 4 * sizeof(UINT32) + sizeof(f->bounds) + f->vertex_count * (sizeof(*f->vertices) + sizeof(*f->vertex_types))
                + (f->bezier_control_count + f->original_bezier_control_count) * sizeof(*f->bezier_controls);
        if (size > D2D_PATH_CACHE_MAX_KEY)
            return NULL;
    }
    if (!(key = malloc(size)))
        return NULL;

#define PUT(ptr, len) do { memcpy(key + pos, (ptr), (len)); pos += (len); } while (0)
    v = geometry->u.path.fill_mode;
    PUT(&v, 4);
    v = geometry->u.path.figure_count;
    PUT(&v, 4);
    for (i = 0; i < geometry->u.path.figure_count; ++i)
    {
        const struct d2d_figure *f = &geometry->u.path.figures[i];

        v = f->flags;
        PUT(&v, 4);
        v = f->vertex_count;
        PUT(&v, 4);
        v = f->bezier_control_count;
        PUT(&v, 4);
        v = f->original_bezier_control_count;
        PUT(&v, 4);
        PUT(&f->bounds, sizeof(f->bounds));
        PUT(f->vertices, f->vertex_count * sizeof(*f->vertices));
        PUT(f->vertex_types, f->vertex_count * sizeof(*f->vertex_types));
        PUT(f->bezier_controls, f->bezier_control_count * sizeof(*f->bezier_controls));
        PUT(f->original_bezier_controls, f->original_bezier_control_count * sizeof(*f->original_bezier_controls));
    }
#undef PUT

    for (i = 0; i + 8 <= size; i += 8)
    {
        UINT64 w;

        memcpy(&w, key + i, 8);
        h = (h ^ w) * 1099511628211ull;
        h ^= h >> 29;
    }
    for (; i < size; ++i)
        h = (h ^ key[i]) * 1099511628211ull;
    *key_size = size;
    *hash = h;
    return key;
}

static BOOL d2d_flat_dup(struct d2d_flat *dst, const struct d2d_flat *src)
{
    *dst = *src;
    dst->vertices = d2d_dup(src->vertices, src->vertex_count * sizeof(*src->vertices));
    dst->faces = d2d_dup(src->faces, src->face_count * sizeof(*src->faces));
    dst->contours = d2d_dup(src->contours, src->contour_count * sizeof(*src->contours));
    if ((src->vertex_count && !dst->vertices) || (src->face_count && !dst->faces)
            || (src->contour_count && !dst->contours))
    {
        free(dst->vertices);
        free(dst->faces);
        free(dst->contours);
        memset(dst, 0, sizeof(*dst));
        return FALSE;
    }
    return TRUE;
}

BOOL d2d_path_cache_apply(struct d2d_geometry *geometry, const unsigned char *key, size_t key_size, UINT64 hash)
{
    struct d2d_path_cache_entry *set = d2d_path_cache[hash % D2D_PATH_CACHE_SETS], *e = NULL;
    D2D1_POINT_2F *fv = NULL;
    struct d2d_face *faces = NULL;
    struct d2d_curve_vertex *bez = NULL;
    size_t *contours = NULL, i, w;
    struct d2d_flat flat = {0};
    struct { D2D1_POINT_2F *v; enum d2d_vertex_type *t; D2D1_POINT_2F *c; D2D1_POINT_2F *o; } *tmp = NULL;
    BOOL ok = FALSE;

    AcquireSRWLockExclusive(&d2d_path_cache_lock);
    for (w = 0; w < 2; ++w)
        if (set[w].key && set[w].hash == hash && set[w].key_size == key_size
                && !memcmp(set[w].key, key, key_size))
        {
            e = &set[w];
            break;
        }
    if (!e)
        goto out;
    e->stamp = ++d2d_path_cache_clock;
    if (e->figure_count != geometry->u.path.figure_count)
        goto out;

    if (!(tmp = calloc(e->figure_count ? e->figure_count : 1, sizeof(*tmp))))
        goto out;
    for (i = 0; i < e->figure_count; ++i)
    {
        const struct d2d_path_cache_figure *cf = &e->figures[i];

        tmp[i].v = d2d_dup(cf->vertices, cf->vertex_count * sizeof(*cf->vertices));
        tmp[i].t = d2d_dup(cf->types, cf->vertex_count * sizeof(*cf->types));
        tmp[i].c = d2d_dup(cf->controls, cf->control_count * sizeof(*cf->controls));
        tmp[i].o = d2d_dup(cf->original, cf->original_count * sizeof(*cf->original));
        if ((cf->vertex_count && (!tmp[i].v || !tmp[i].t)) || (cf->control_count && !tmp[i].c)
                || (cf->original_count && !tmp[i].o))
            goto out;
    }
    fv = d2d_dup(e->fill_vertices, e->fill_vertex_count * sizeof(*fv));
    faces = d2d_dup(e->faces, e->face_count * sizeof(*faces));
    bez = d2d_dup(e->beziers, e->bezier_count * sizeof(*bez));
    contours = d2d_dup(e->contours, e->contour_count * sizeof(*contours));
    if ((e->fill_vertex_count && !fv) || (e->face_count && !faces) || (e->bezier_count && !bez)
            || (e->contour_count && !contours))
        goto out;
    if (!d2d_flat_dup(&flat, &e->flat))
        goto out;

    for (i = 0; i < e->figure_count; ++i)
    {
        struct d2d_figure *f = &geometry->u.path.figures[i];
        const struct d2d_path_cache_figure *cf = &e->figures[i];

        free(f->vertices);
        free(f->vertex_types);
        free(f->bezier_controls);
        free(f->original_bezier_controls);
        f->vertices = tmp[i].v;
        f->vertices_size = cf->vertex_count;
        f->vertex_types = tmp[i].t;
        f->vertex_types_size = cf->vertex_count;
        f->vertex_count = cf->vertex_count;
        f->bezier_controls = tmp[i].c;
        f->bezier_controls_size = cf->control_count;
        f->bezier_control_count = cf->control_count;
        f->original_bezier_controls = tmp[i].o;
        f->original_bezier_controls_size = cf->original_count;
        f->original_bezier_control_count = cf->original_count;
        f->bounds = cf->bounds;
        tmp[i].v = NULL;
        tmp[i].t = NULL;
        tmp[i].c = NULL;
        tmp[i].o = NULL;
    }
    free(geometry->fill.vertices);
    free(geometry->fill.faces);
    free(geometry->fill.bezier_vertices);
    free(geometry->fill_contours);
    free(geometry->flat.vertices);
    free(geometry->flat.faces);
    free(geometry->flat.contours);
    geometry->flat = flat;
    memset(&flat, 0, sizeof(flat));
    geometry->fill.vertices = fv;
    geometry->fill.vertex_count = e->fill_vertex_count;
    geometry->fill.faces = faces;
    geometry->fill.faces_size = e->face_count;
    geometry->fill.face_count = e->face_count;
    geometry->fill.bezier_vertices = bez;
    geometry->fill.bezier_vertices_size = e->bezier_count;
    geometry->fill.bezier_vertex_count = e->bezier_count;
    geometry->fill_polygon = e->fill_polygon;
    geometry->fill_contours = contours;
    geometry->fill_contour_count = e->contour_count;
    fv = NULL;
    faces = NULL;
    bez = NULL;
    contours = NULL;
    ok = TRUE;

out:
    if (tmp)
    {
        for (i = 0; i < geometry->u.path.figure_count && i < (e ? e->figure_count : 0); ++i)
        {
            free(tmp[i].v);
            free(tmp[i].t);
            free(tmp[i].c);
            free(tmp[i].o);
        }
        free(tmp);
    }
    free(fv);
    free(faces);
    free(bez);
    free(contours);
    free(flat.vertices);
    free(flat.faces);
    free(flat.contours);
    ReleaseSRWLockExclusive(&d2d_path_cache_lock);
    return ok;
}

void d2d_path_cache_insert(const struct d2d_geometry *geometry, unsigned char *key, size_t key_size, UINT64 hash)
{
    struct d2d_path_cache_entry *set = d2d_path_cache[hash % D2D_PATH_CACHE_SETS], ne = {0}, *victim;
    size_t i;

    ne.hash = hash;
    ne.key = key;
    ne.key_size = key_size;
    ne.figure_count = geometry->u.path.figure_count;
    if (ne.figure_count && !(ne.figures = calloc(ne.figure_count, sizeof(*ne.figures))))
        goto fail;
    for (i = 0; i < ne.figure_count; ++i)
    {
        const struct d2d_figure *f = &geometry->u.path.figures[i];
        struct d2d_path_cache_figure *cf = &ne.figures[i];

        cf->vertex_count = f->vertex_count;
        cf->control_count = f->bezier_control_count;
        cf->original_count = f->original_bezier_control_count;
        cf->bounds = f->bounds;
        cf->vertices = d2d_dup(f->vertices, f->vertex_count * sizeof(*f->vertices));
        cf->types = d2d_dup(f->vertex_types, f->vertex_count * sizeof(*f->vertex_types));
        cf->controls = d2d_dup(f->bezier_controls, f->bezier_control_count * sizeof(*f->bezier_controls));
        cf->original = d2d_dup(f->original_bezier_controls, f->original_bezier_control_count
                * sizeof(*f->original_bezier_controls));
        if ((f->vertex_count && (!cf->vertices || !cf->types)) || (f->bezier_control_count && !cf->controls)
                || (f->original_bezier_control_count && !cf->original))
            goto fail;
    }
    ne.fill_vertex_count = geometry->fill.vertex_count;
    ne.fill_vertices = d2d_dup(geometry->fill.vertices, ne.fill_vertex_count * sizeof(*ne.fill_vertices));
    ne.face_count = geometry->fill.face_count;
    ne.faces = d2d_dup(geometry->fill.faces, ne.face_count * sizeof(*ne.faces));
    ne.bezier_count = geometry->fill.bezier_vertex_count;
    ne.beziers = d2d_dup(geometry->fill.bezier_vertices, ne.bezier_count * sizeof(*ne.beziers));
    ne.fill_polygon = geometry->fill_polygon;
    ne.contour_count = geometry->fill_contour_count;
    ne.contours = d2d_dup(geometry->fill_contours, ne.contour_count * sizeof(*ne.contours));
    if ((ne.fill_vertex_count && !ne.fill_vertices) || (ne.face_count && !ne.faces)
            || (ne.bezier_count && !ne.beziers) || (ne.contour_count && !ne.contours))
        goto fail;
    if (!d2d_flat_dup(&ne.flat, &geometry->flat))
        goto fail;
    if (geometry->fill.arc_vertex_count)
        goto fail;

    AcquireSRWLockExclusive(&d2d_path_cache_lock);
    victim = &set[0];
    if (set[0].key && (!set[1].key || (LONG)(set[1].stamp - set[0].stamp) < 0))
        victim = &set[1];
    d2d_path_cache_entry_clear(victim);
    ne.stamp = ++d2d_path_cache_clock;
    *victim = ne;
    ReleaseSRWLockExclusive(&d2d_path_cache_lock);
    return;

fail:
    ne.key = NULL;
    d2d_path_cache_entry_clear(&ne);
    free(key);
}
