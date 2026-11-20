/*
 * PROJECT:     LiberNT Direct2D
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     SVG documents: parsing the documented SVG 1.1 subset and drawing it
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "d2d1_private.h"
#include <xmllite.h>
#include <float.h>
#include <math.h>
#include <wchar.h>

WINE_DEFAULT_DEBUG_CHANNEL(d2d);

enum d2d_svg_tag
{
    SVG_TAG_UNSUPPORTED,
    SVG_TAG_SVG,
    SVG_TAG_G,
    SVG_TAG_DEFS,
    SVG_TAG_USE,
    SVG_TAG_PATH,
    SVG_TAG_RECT,
    SVG_TAG_CIRCLE,
    SVG_TAG_ELLIPSE,
    SVG_TAG_LINE,
    SVG_TAG_POLYLINE,
    SVG_TAG_POLYGON,
    SVG_TAG_LINEAR_GRADIENT,
    SVG_TAG_RADIAL_GRADIENT,
    SVG_TAG_STOP,
    SVG_TAG_CLIP_PATH,
    SVG_TAG_TITLE,
    SVG_TAG_DESC,
    SVG_TAG_IMAGE,
};

static const struct
{
    const WCHAR *name;
    enum d2d_svg_tag tag;
}
d2d_svg_tags[] =
{
    {L"circle", SVG_TAG_CIRCLE},
    {L"clipPath", SVG_TAG_CLIP_PATH},
    {L"defs", SVG_TAG_DEFS},
    {L"desc", SVG_TAG_DESC},
    {L"ellipse", SVG_TAG_ELLIPSE},
    {L"g", SVG_TAG_G},
    {L"image", SVG_TAG_IMAGE},
    {L"line", SVG_TAG_LINE},
    {L"linearGradient", SVG_TAG_LINEAR_GRADIENT},
    {L"path", SVG_TAG_PATH},
    {L"polygon", SVG_TAG_POLYGON},
    {L"polyline", SVG_TAG_POLYLINE},
    {L"radialGradient", SVG_TAG_RADIAL_GRADIENT},
    {L"rect", SVG_TAG_RECT},
    {L"stop", SVG_TAG_STOP},
    {L"svg", SVG_TAG_SVG},
    {L"title", SVG_TAG_TITLE},
    {L"use", SVG_TAG_USE},
};

enum
{
    SVG_PROP_FILL = 0x1,
    SVG_PROP_FILL_OPACITY = 0x2,
    SVG_PROP_FILL_RULE = 0x4,
    SVG_PROP_STROKE = 0x8,
    SVG_PROP_STROKE_WIDTH = 0x10,
    SVG_PROP_STROKE_OPACITY = 0x20,
    SVG_PROP_LINE_CAP = 0x40,
    SVG_PROP_LINE_JOIN = 0x80,
    SVG_PROP_MITER_LIMIT = 0x100,
    SVG_PROP_DASH_ARRAY = 0x200,
    SVG_PROP_DASH_OFFSET = 0x400,
    SVG_PROP_OPACITY = 0x800,
    SVG_PROP_COLOR = 0x1000,
    SVG_PROP_DISPLAY = 0x2000,
    SVG_PROP_VISIBILITY = 0x4000,
    SVG_PROP_CLIP_PATH = 0x8000,
    SVG_PROP_CLIP_RULE = 0x10000,
    SVG_PROP_OVERFLOW = 0x20000,
    SVG_PROP_STOP_COLOR = 0x40000,
    SVG_PROP_STOP_OPACITY = 0x80000,
};

enum d2d_svg_paint_kind
{
    SVG_PAINT_NONE,
    SVG_PAINT_COLOR,
    SVG_PAINT_CURRENT_COLOR,
    SVG_PAINT_URL,
};

struct d2d_svg_element;

struct d2d_svg_paint
{
    enum d2d_svg_paint_kind kind;
    D2D1_COLOR_F color;
    const WCHAR *url;
    struct d2d_svg_element *target;
    BOOL target_resolved;
    enum d2d_svg_paint_kind fallback;
};

struct d2d_svg_length
{
    float value;
    BOOL percent;
};

struct d2d_svg_props
{
    unsigned int set;
    struct d2d_svg_paint fill, stroke;
    float fill_opacity, stroke_opacity, stroke_width, miter_limit, dash_offset, opacity, stop_opacity;
    D2D1_COLOR_F color, stop_color;
    D2D1_FILL_MODE fill_rule, clip_rule;
    D2D1_CAP_STYLE cap;
    D2D1_LINE_JOIN join;
    float *dashes;
    UINT32 dash_count;
    BOOL display_none, hidden, overflow_hidden;
    const WCHAR *clip_path;
    struct d2d_svg_element *clip_target;
    BOOL clip_resolved;
};

struct d2d_svg_attr
{
    WCHAR *name;
    WCHAR *value;
};

struct d2d_svg_brush_cache
{
    ID2D1Brush *brush;
    ID2D1DeviceContext *context;
    enum d2d_svg_paint_kind kind;
    D2D1_COLOR_F color;
    float opacity;
    struct d2d_svg_element *gradient;
};

struct d2d_svg_element
{
    ID2D1SvgElement ID2D1SvgElement_iface;
    struct d2d_svg_document *document;
    struct d2d_svg_element *parent;
    struct d2d_svg_element **children;
    size_t child_count;
    size_t child_capacity;
    enum d2d_svg_tag tag;
    WCHAR *tag_name;
    struct d2d_svg_attr *attrs;
    size_t attr_count;
    size_t attr_capacity;
    const WCHAR *id;
    struct d2d_svg_props props;
    D2D1_MATRIX_3X2_F transform;
    BOOL has_transform;
    ID2D1Geometry *geometry;
    BOOL geometry_built;
    D2D1_FILL_MODE geometry_fill_mode;
    D2D1_RECT_F bounds;
    struct d2d_svg_brush_cache fill_cache, stroke_cache;
    ID2D1StrokeStyle *stroke_style;
    D2D1_STROKE_STYLE_PROPERTIES stroke_props;
    const float *stroke_dashes;
    float stroke_dash_width;
};

struct d2d_svg_document
{
    ID2D1SvgDocument ID2D1SvgDocument_iface;
    LONG refcount;
    ID2D1Factory *factory;
    D2D1_SIZE_F viewport;
    struct d2d_svg_element *root;
    struct d2d_svg_element **ids;
    size_t id_capacity;
};

struct d2d_svg_style
{
    struct d2d_svg_paint *fill, *stroke;
    float fill_opacity, stroke_opacity, stroke_width, miter_limit, dash_offset;
    D2D1_COLOR_F color;
    D2D1_FILL_MODE fill_rule, clip_rule;
    D2D1_CAP_STYLE cap;
    D2D1_LINE_JOIN join;
    const float *dashes;
    UINT32 dash_count;
    BOOL hidden;
};

struct d2d_svg_draw
{
    ID2D1DeviceContext *context;
    struct d2d_svg_document *document;
    D2D1_SIZE_F percent_base;
    unsigned int use_depth;
};

static const D2D1_MATRIX_3X2_F d2d_svg_identity = {{{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f}}};

static inline struct d2d_svg_document *impl_from_ID2D1SvgDocument(ID2D1SvgDocument *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_svg_document, ID2D1SvgDocument_iface);
}

static inline struct d2d_svg_element *impl_from_ID2D1SvgElement(ID2D1SvgElement *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_svg_element, ID2D1SvgElement_iface);
}

/* Value parsing. */

static const WCHAR *d2d_svg_skip_space(const WCHAR *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        ++p;
    return p;
}

static const WCHAR *d2d_svg_skip_separator(const WCHAR *p)
{
    p = d2d_svg_skip_space(p);
    if (*p == ',')
        p = d2d_svg_skip_space(p + 1);
    return p;
}

static BOOL d2d_svg_parse_number(const WCHAR **str, float *value)
{
    const WCHAR *p = d2d_svg_skip_space(*str), *start = p;
    double v = 0.0, frac = 0.1;
    int sign = 1, exp_sign = 1, exponent = 0;
    BOOL digits = FALSE;

    if (*p == '+' || *p == '-')
        sign = *p++ == '-' ? -1 : 1;
    while (*p >= '0' && *p <= '9')
    {
        v = v * 10.0 + (*p++ - '0');
        digits = TRUE;
    }
    if (*p == '.')
    {
        ++p;
        while (*p >= '0' && *p <= '9')
        {
            v += (*p++ - '0') * frac;
            frac *= 0.1;
            digits = TRUE;
        }
    }
    if (!digits)
    {
        *str = start;
        return FALSE;
    }
    if ((*p == 'e' || *p == 'E') && p[1] != 'm' && p[1] != 'x')
    {
        const WCHAR *e = p + 1;

        if (*e == '+' || *e == '-')
            exp_sign = *e++ == '-' ? -1 : 1;
        if (*e >= '0' && *e <= '9')
        {
            while (*e >= '0' && *e <= '9')
            {
                exponent = min(exponent * 10 + (*e - '0'), 400);
                ++e;
            }
            v *= pow(10.0, exp_sign * exponent);
            p = e;
        }
    }
    *value = (float)(sign * v);
    *str = p;
    return TRUE;
}

static BOOL d2d_svg_parse_length(const WCHAR *str, struct d2d_svg_length *length)
{
    static const struct
    {
        const WCHAR *unit;
        float scale;
    }
    units[] =
    {
        {L"px", 1.0f}, {L"pt", 96.0f / 72.0f}, {L"pc", 16.0f}, {L"mm", 96.0f / 25.4f}, {L"cm", 96.0f / 2.54f},
        {L"in", 96.0f},
    };
    const WCHAR *p = str;
    unsigned int i;

    if (!d2d_svg_parse_number(&p, &length->value))
        return FALSE;
    length->percent = FALSE;
    if (*p == '%')
    {
        length->percent = TRUE;
        ++p;
    }
    else
    {
        for (i = 0; i < ARRAY_SIZE(units); ++i)
        {
            if (p[0] == units[i].unit[0] && p[1] == units[i].unit[1])
            {
                length->value *= units[i].scale;
                p += 2;
                break;
            }
        }
    }
    return !*d2d_svg_skip_space(p);
}

/* Percentages resolve against the width, the height or the normalized diagonal of the viewport. */
static float d2d_svg_resolve_length(const struct d2d_svg_length *length, const D2D1_SIZE_F *base, int axis)
{
    float reference;

    if (!length->percent)
        return length->value;
    if (axis == 0)
        reference = base->width;
    else if (axis == 1)
        reference = base->height;
    else
        reference = sqrtf((base->width * base->width + base->height * base->height) / 2.0f);
    return length->value * reference / 100.0f;
}

static const struct
{
    const WCHAR *name;
    DWORD rgb;
}
d2d_svg_colors[] =
{
    {L"aliceblue", 0xf0f8ff}, {L"antiquewhite", 0xfaebd7}, {L"aqua", 0x00ffff}, {L"aquamarine", 0x7fffd4},
    {L"azure", 0xf0ffff}, {L"beige", 0xf5f5dc}, {L"bisque", 0xffe4c4}, {L"black", 0x000000},
    {L"blanchedalmond", 0xffebcd}, {L"blue", 0x0000ff}, {L"blueviolet", 0x8a2be2}, {L"brown", 0xa52a2a},
    {L"burlywood", 0xdeb887}, {L"cadetblue", 0x5f9ea0}, {L"chartreuse", 0x7fff00}, {L"chocolate", 0xd2691e},
    {L"coral", 0xff7f50}, {L"cornflowerblue", 0x6495ed}, {L"cornsilk", 0xfff8dc}, {L"crimson", 0xdc143c},
    {L"cyan", 0x00ffff}, {L"darkblue", 0x00008b}, {L"darkcyan", 0x008b8b}, {L"darkgoldenrod", 0xb8860b},
    {L"darkgray", 0xa9a9a9}, {L"darkgreen", 0x006400}, {L"darkgrey", 0xa9a9a9}, {L"darkkhaki", 0xbdb76b},
    {L"darkmagenta", 0x8b008b}, {L"darkolivegreen", 0x556b2f}, {L"darkorange", 0xff8c00},
    {L"darkorchid", 0x9932cc}, {L"darkred", 0x8b0000}, {L"darksalmon", 0xe9967a}, {L"darkseagreen", 0x8fbc8f},
    {L"darkslateblue", 0x483d8b}, {L"darkslategray", 0x2f4f4f}, {L"darkslategrey", 0x2f4f4f},
    {L"darkturquoise", 0x00ced1}, {L"darkviolet", 0x9400d3}, {L"deeppink", 0xff1493},
    {L"deepskyblue", 0x00bfff}, {L"dimgray", 0x696969}, {L"dimgrey", 0x696969}, {L"dodgerblue", 0x1e90ff},
    {L"firebrick", 0xb22222}, {L"floralwhite", 0xfffaf0}, {L"forestgreen", 0x228b22}, {L"fuchsia", 0xff00ff},
    {L"gainsboro", 0xdcdcdc}, {L"ghostwhite", 0xf8f8ff}, {L"gold", 0xffd700}, {L"goldenrod", 0xdaa520},
    {L"gray", 0x808080}, {L"green", 0x008000}, {L"greenyellow", 0xadff2f}, {L"grey", 0x808080},
    {L"honeydew", 0xf0fff0}, {L"hotpink", 0xff69b4}, {L"indianred", 0xcd5c5c}, {L"indigo", 0x4b0082},
    {L"ivory", 0xfffff0}, {L"khaki", 0xf0e68c}, {L"lavender", 0xe6e6fa}, {L"lavenderblush", 0xfff0f5},
    {L"lawngreen", 0x7cfc00}, {L"lemonchiffon", 0xfffacd}, {L"lightblue", 0xadd8e6}, {L"lightcoral", 0xf08080},
    {L"lightcyan", 0xe0ffff}, {L"lightgoldenrodyellow", 0xfafad2}, {L"lightgray", 0xd3d3d3},
    {L"lightgreen", 0x90ee90}, {L"lightgrey", 0xd3d3d3}, {L"lightpink", 0xffb6c1}, {L"lightsalmon", 0xffa07a},
    {L"lightseagreen", 0x20b2aa}, {L"lightskyblue", 0x87cefa}, {L"lightslategray", 0x778899},
    {L"lightslategrey", 0x778899}, {L"lightsteelblue", 0xb0c4de}, {L"lightyellow", 0xffffe0},
    {L"lime", 0x00ff00}, {L"limegreen", 0x32cd32}, {L"linen", 0xfaf0e6}, {L"magenta", 0xff00ff},
    {L"maroon", 0x800000}, {L"mediumaquamarine", 0x66cdaa}, {L"mediumblue", 0x0000cd},
    {L"mediumorchid", 0xba55d3}, {L"mediumpurple", 0x9370db}, {L"mediumseagreen", 0x3cb371},
    {L"mediumslateblue", 0x7b68ee}, {L"mediumspringgreen", 0x00fa9a}, {L"mediumturquoise", 0x48d1cc},
    {L"mediumvioletred", 0xc71585}, {L"midnightblue", 0x191970}, {L"mintcream", 0xf5fffa},
    {L"mistyrose", 0xffe4e1}, {L"moccasin", 0xffe4b5}, {L"navajowhite", 0xffdead}, {L"navy", 0x000080},
    {L"oldlace", 0xfdf5e6}, {L"olive", 0x808000}, {L"olivedrab", 0x6b8e23}, {L"orange", 0xffa500},
    {L"orangered", 0xff4500}, {L"orchid", 0xda70d6}, {L"palegoldenrod", 0xeee8aa}, {L"palegreen", 0x98fb98},
    {L"paleturquoise", 0xafeeee}, {L"palevioletred", 0xdb7093}, {L"papayawhip", 0xffefd5},
    {L"peachpuff", 0xffdab9}, {L"peru", 0xcd853f}, {L"pink", 0xffc0cb}, {L"plum", 0xdda0dd},
    {L"powderblue", 0xb0e0e6}, {L"purple", 0x800080}, {L"red", 0xff0000}, {L"rosybrown", 0xbc8f8f},
    {L"royalblue", 0x4169e1}, {L"saddlebrown", 0x8b4513}, {L"salmon", 0xfa8072}, {L"sandybrown", 0xf4a460},
    {L"seagreen", 0x2e8b57}, {L"seashell", 0xfff5ee}, {L"sienna", 0xa0522d}, {L"silver", 0xc0c0c0},
    {L"skyblue", 0x87ceeb}, {L"slateblue", 0x6a5acd}, {L"slategray", 0x708090}, {L"slategrey", 0x708090},
    {L"snow", 0xfffafa}, {L"springgreen", 0x00ff7f}, {L"steelblue", 0x4682b4}, {L"tan", 0xd2b48c},
    {L"teal", 0x008080}, {L"thistle", 0xd8bfd8}, {L"tomato", 0xff6347}, {L"turquoise", 0x40e0d0},
    {L"violet", 0xee82ee}, {L"wheat", 0xf5deb3}, {L"white", 0xffffff}, {L"whitesmoke", 0xf5f5f5},
    {L"yellow", 0xffff00}, {L"yellowgreen", 0x9acd32},
};

static void d2d_svg_color_from_rgb(DWORD rgb, D2D1_COLOR_F *color)
{
    color->r = ((rgb >> 16) & 0xff) / 255.0f;
    color->g = ((rgb >> 8) & 0xff) / 255.0f;
    color->b = (rgb & 0xff) / 255.0f;
    color->a = 1.0f;
}

static int d2d_svg_hex_digit(WCHAR c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static BOOL d2d_svg_parse_color(const WCHAR *str, D2D1_COLOR_F *color)
{
    const WCHAR *p = d2d_svg_skip_space(str);
    size_t length, i;
    int lo = 0, hi = ARRAY_SIZE(d2d_svg_colors) - 1, d[6];
    float channels[3];

    if (*p == '#')
    {
        ++p;
        for (length = 0; length < 6 && (d[length] = d2d_svg_hex_digit(p[length])) >= 0; ++length)
            ;
        if (d2d_svg_hex_digit(p[length]) >= 0 || (length != 3 && length != 6) || *d2d_svg_skip_space(p + length))
            return FALSE;
        if (length == 3)
            d2d_svg_color_from_rgb((d[0] * 17) << 16 | (d[1] * 17) << 8 | d[2] * 17, color);
        else
            d2d_svg_color_from_rgb(d[0] << 20 | d[1] << 16 | d[2] << 12 | d[3] << 8 | d[4] << 4 | d[5], color);
        return TRUE;
    }
    if (!wcsncmp(p, L"rgb(", 4))
    {
        p += 4;
        for (i = 0; i < 3; ++i)
        {
            if (!d2d_svg_parse_number(&p, &channels[i]))
                return FALSE;
            channels[i] = *p == '%' ? channels[i] * 255.0f / 100.0f : channels[i];
            if (*p == '%')
                ++p;
            channels[i] = min(max(channels[i], 0.0f), 255.0f) / 255.0f;
            p = d2d_svg_skip_separator(p);
        }
        if (*p != ')')
            return FALSE;
        color->r = channels[0];
        color->g = channels[1];
        color->b = channels[2];
        color->a = 1.0f;
        return TRUE;
    }
    for (length = 0; p[length] && p[length] != ' ' && p[length] != '\t'; ++length)
        ;
    while (lo <= hi)
    {
        int mid = (lo + hi) / 2, cmp = _wcsnicmp(p, d2d_svg_colors[mid].name, length);

        if (!cmp && d2d_svg_colors[mid].name[length])
            cmp = -1;
        if (!cmp)
        {
            d2d_svg_color_from_rgb(d2d_svg_colors[mid].rgb, color);
            return TRUE;
        }
        if (cmp < 0)
            hi = mid - 1;
        else
            lo = mid + 1;
    }
    return FALSE;
}

/* "url(#id)" gives a pointer to the id inside the string and its length. */
static BOOL d2d_svg_parse_url(const WCHAR *str, const WCHAR **id, size_t *length, const WCHAR **rest)
{
    const WCHAR *p = d2d_svg_skip_space(str), *end;

    if (wcsncmp(p, L"url(", 4))
        return FALSE;
    p = d2d_svg_skip_space(p + 4);
    if (*p != '#')
        return FALSE;
    ++p;
    for (end = p; *end && *end != ')' && *end != ' '; ++end)
        ;
    *id = p;
    *length = end - p;
    while (*end && *end != ')')
        ++end;
    if (*end != ')')
        return FALSE;
    *rest = end + 1;
    return TRUE;
}

static WCHAR *d2d_svg_strndup(const WCHAR *str, size_t length)
{
    WCHAR *copy = malloc((length + 1) * sizeof(*copy));

    if (copy)
    {
        memcpy(copy, str, length * sizeof(*copy));
        copy[length] = 0;
    }
    return copy;
}

static BOOL d2d_svg_parse_paint(const WCHAR *str, struct d2d_svg_paint *paint)
{
    const WCHAR *p = d2d_svg_skip_space(str), *id, *rest;
    size_t length;

    memset(paint, 0, sizeof(*paint));
    if (!wcscmp(p, L"none"))
    {
        paint->kind = SVG_PAINT_NONE;
        return TRUE;
    }
    if (!wcscmp(p, L"currentColor"))
    {
        paint->kind = SVG_PAINT_CURRENT_COLOR;
        return TRUE;
    }
    if (d2d_svg_parse_url(p, &id, &length, &rest))
    {
        paint->kind = SVG_PAINT_URL;
        paint->url = d2d_svg_strndup(id, length);
        rest = d2d_svg_skip_space(rest);
        paint->fallback = SVG_PAINT_NONE;
        if (*rest && wcscmp(rest, L"none"))
        {
            if (!wcscmp(rest, L"currentColor"))
                paint->fallback = SVG_PAINT_CURRENT_COLOR;
            else if (d2d_svg_parse_color(rest, &paint->color))
                paint->fallback = SVG_PAINT_COLOR;
        }
        return paint->url != NULL;
    }
    if (d2d_svg_parse_color(p, &paint->color))
    {
        paint->kind = SVG_PAINT_COLOR;
        return TRUE;
    }
    return FALSE;
}

static void d2d_svg_multiply(D2D1_MATRIX_3X2_F *a, const D2D1_MATRIX_3X2_F *b)
{
    D2D1_MATRIX_3X2_F t = *a;

    a->_11 = t._11 * b->_11 + t._12 * b->_21;
    a->_12 = t._11 * b->_12 + t._12 * b->_22;
    a->_21 = t._21 * b->_11 + t._22 * b->_21;
    a->_22 = t._21 * b->_12 + t._22 * b->_22;
    a->_31 = t._31 * b->_11 + t._32 * b->_21 + b->_31;
    a->_32 = t._31 * b->_12 + t._32 * b->_22 + b->_32;
}

/* A transform list applies its rightmost item first; in row-vector form each item multiplies on the left. */
static BOOL d2d_svg_parse_transform(const WCHAR *str, D2D1_MATRIX_3X2_F *matrix)
{
    static const struct
    {
        const WCHAR *name;
        unsigned int min_args, max_args;
    }
    kinds[] =
    {
        {L"matrix", 6, 6}, {L"translate", 1, 2}, {L"scale", 1, 2}, {L"rotate", 1, 3}, {L"skewX", 1, 1},
        {L"skewY", 1, 1},
    };
    const WCHAR *p = d2d_svg_skip_space(str);
    D2D1_MATRIX_3X2_F result = d2d_svg_identity, item;
    unsigned int i, count;
    float args[6], angle;

    while (*p)
    {
        for (i = 0; i < ARRAY_SIZE(kinds); ++i)
        {
            if (!wcsncmp(p, kinds[i].name, wcslen(kinds[i].name)))
                break;
        }
        if (i == ARRAY_SIZE(kinds))
            return FALSE;
        p = d2d_svg_skip_space(p + wcslen(kinds[i].name));
        if (*p++ != '(')
            return FALSE;
        for (count = 0; count < 6; ++count)
        {
            p = d2d_svg_skip_space(p);
            if (*p == ')')
                break;
            if (!d2d_svg_parse_number(&p, &args[count]))
                return FALSE;
            p = d2d_svg_skip_separator(p);
        }
        p = d2d_svg_skip_space(p);
        if (*p++ != ')' || count < kinds[i].min_args || count > kinds[i].max_args)
            return FALSE;
        item = d2d_svg_identity;
        switch (i)
        {
            case 0:
                item._11 = args[0]; item._12 = args[1]; item._21 = args[2];
                item._22 = args[3]; item._31 = args[4]; item._32 = args[5];
                break;
            case 1:
                item._31 = args[0];
                item._32 = count > 1 ? args[1] : 0.0f;
                break;
            case 2:
                item._11 = args[0];
                item._22 = count > 1 ? args[1] : args[0];
                break;
            case 3:
                if (count != 1 && count != 3)
                    return FALSE;
                angle = args[0] * (float)M_PI / 180.0f;
                item._11 = cosf(angle); item._12 = sinf(angle);
                item._21 = -sinf(angle); item._22 = cosf(angle);
                if (count == 3)
                {
                    item._31 = args[1] - args[1] * item._11 - args[2] * item._21;
                    item._32 = args[2] - args[1] * item._12 - args[2] * item._22;
                }
                break;
            case 4:
                item._21 = tanf(args[0] * (float)M_PI / 180.0f);
                break;
            default:
                item._12 = tanf(args[0] * (float)M_PI / 180.0f);
                break;
        }
        d2d_svg_multiply(&item, &result);
        result = item;
        p = d2d_svg_skip_separator(p);
    }
    *matrix = result;
    return TRUE;
}

static BOOL d2d_svg_parse_float_attr(const WCHAR *str, float *value)
{
    const WCHAR *p = str;

    return d2d_svg_parse_number(&p, value) && !*d2d_svg_skip_space(p);
}

static BOOL d2d_svg_parse_opacity(const WCHAR *str, float *value)
{
    const WCHAR *p = str;

    if (!d2d_svg_parse_number(&p, value))
        return FALSE;
    if (*p == '%')
    {
        *value /= 100.0f;
        ++p;
    }
    *value = min(max(*value, 0.0f), 1.0f);
    return !*d2d_svg_skip_space(p);
}

static BOOL d2d_svg_parse_dashes(const WCHAR *str, struct d2d_svg_props *props)
{
    const WCHAR *p = d2d_svg_skip_space(str);
    float values[64];
    UINT32 count = 0, i;

    free(props->dashes);
    props->dashes = NULL;
    props->dash_count = 0;
    if (!wcscmp(p, L"none"))
        return TRUE;
    while (*p && count < ARRAY_SIZE(values) / 2)
    {
        if (!d2d_svg_parse_number(&p, &values[count]) || values[count] < 0.0f)
            return FALSE;
        if (*p == '%')
            return FALSE;
        ++count;
        p = d2d_svg_skip_separator(p);
    }
    if (!count)
        return FALSE;
    /* An odd list repeats to make it even. */
    if (count & 1)
    {
        for (i = 0; i < count; ++i)
            values[count + i] = values[i];
        count *= 2;
    }
    if (!(props->dashes = malloc(count * sizeof(*props->dashes))))
        return FALSE;
    memcpy(props->dashes, values, count * sizeof(*props->dashes));
    props->dash_count = count;
    return TRUE;
}

static void d2d_svg_set_property(struct d2d_svg_props *props, const WCHAR *name, const WCHAR *value)
{
    const WCHAR *v = d2d_svg_skip_space(value), *id, *rest;
    struct d2d_svg_length length;
    struct d2d_svg_paint paint;
    size_t id_length;
    float number;

    if (!wcscmp(v, L"inherit"))
        return;
    if (!wcscmp(name, L"fill") || !wcscmp(name, L"stroke"))
    {
        BOOL fill = name[0] == 'f';

        if (!d2d_svg_parse_paint(v, &paint))
            return;
        free((void *)(fill ? props->fill.url : props->stroke.url));
        if (fill)
        {
            props->fill = paint;
            props->set |= SVG_PROP_FILL;
        }
        else
        {
            props->stroke = paint;
            props->set |= SVG_PROP_STROKE;
        }
    }
    else if (!wcscmp(name, L"fill-opacity") && d2d_svg_parse_opacity(v, &props->fill_opacity))
        props->set |= SVG_PROP_FILL_OPACITY;
    else if (!wcscmp(name, L"stroke-opacity") && d2d_svg_parse_opacity(v, &props->stroke_opacity))
        props->set |= SVG_PROP_STROKE_OPACITY;
    else if (!wcscmp(name, L"opacity") && d2d_svg_parse_opacity(v, &props->opacity))
        props->set |= SVG_PROP_OPACITY;
    else if (!wcscmp(name, L"stop-opacity") && d2d_svg_parse_opacity(v, &props->stop_opacity))
        props->set |= SVG_PROP_STOP_OPACITY;
    else if (!wcscmp(name, L"fill-rule") || !wcscmp(name, L"clip-rule"))
    {
        D2D1_FILL_MODE mode;

        if (!wcscmp(v, L"nonzero"))
            mode = D2D1_FILL_MODE_WINDING;
        else if (!wcscmp(v, L"evenodd"))
            mode = D2D1_FILL_MODE_ALTERNATE;
        else
            return;
        if (name[0] == 'f')
        {
            props->fill_rule = mode;
            props->set |= SVG_PROP_FILL_RULE;
        }
        else
        {
            props->clip_rule = mode;
            props->set |= SVG_PROP_CLIP_RULE;
        }
    }
    else if (!wcscmp(name, L"stroke-width") && d2d_svg_parse_length(v, &length) && !length.percent
            && length.value >= 0.0f)
    {
        props->stroke_width = length.value;
        props->set |= SVG_PROP_STROKE_WIDTH;
    }
    else if (!wcscmp(name, L"stroke-miterlimit") && d2d_svg_parse_float_attr(v, &number) && number >= 1.0f)
    {
        props->miter_limit = number;
        props->set |= SVG_PROP_MITER_LIMIT;
    }
    else if (!wcscmp(name, L"stroke-dashoffset") && d2d_svg_parse_length(v, &length) && !length.percent)
    {
        props->dash_offset = length.value;
        props->set |= SVG_PROP_DASH_OFFSET;
    }
    else if (!wcscmp(name, L"stroke-dasharray") && d2d_svg_parse_dashes(v, props))
        props->set |= SVG_PROP_DASH_ARRAY;
    else if (!wcscmp(name, L"stroke-linecap"))
    {
        if (!wcscmp(v, L"butt"))
            props->cap = D2D1_CAP_STYLE_FLAT;
        else if (!wcscmp(v, L"round"))
            props->cap = D2D1_CAP_STYLE_ROUND;
        else if (!wcscmp(v, L"square"))
            props->cap = D2D1_CAP_STYLE_SQUARE;
        else
            return;
        props->set |= SVG_PROP_LINE_CAP;
    }
    else if (!wcscmp(name, L"stroke-linejoin"))
    {
        if (!wcscmp(v, L"miter"))
            props->join = D2D1_LINE_JOIN_MITER_OR_BEVEL;
        else if (!wcscmp(v, L"round"))
            props->join = D2D1_LINE_JOIN_ROUND;
        else if (!wcscmp(v, L"bevel"))
            props->join = D2D1_LINE_JOIN_BEVEL;
        else
            return;
        props->set |= SVG_PROP_LINE_JOIN;
    }
    else if (!wcscmp(name, L"color") && d2d_svg_parse_color(v, &props->color))
        props->set |= SVG_PROP_COLOR;
    else if (!wcscmp(name, L"stop-color"))
    {
        if (!wcscmp(v, L"currentColor"))
            return;
        if (d2d_svg_parse_color(v, &props->stop_color))
            props->set |= SVG_PROP_STOP_COLOR;
    }
    else if (!wcscmp(name, L"display"))
    {
        props->display_none = !wcscmp(v, L"none");
        props->set |= SVG_PROP_DISPLAY;
    }
    else if (!wcscmp(name, L"visibility"))
    {
        props->hidden = !wcscmp(v, L"hidden") || !wcscmp(v, L"collapse");
        props->set |= SVG_PROP_VISIBILITY;
    }
    else if (!wcscmp(name, L"overflow"))
    {
        props->overflow_hidden = !wcscmp(v, L"hidden") || !wcscmp(v, L"scroll");
        props->set |= SVG_PROP_OVERFLOW;
    }
    else if (!wcscmp(name, L"clip-path"))
    {
        free((void *)props->clip_path);
        props->clip_path = NULL;
        if (d2d_svg_parse_url(v, &id, &id_length, &rest))
            props->clip_path = d2d_svg_strndup(id, id_length);
        props->set |= SVG_PROP_CLIP_PATH;
    }
}

/* "name: value; ..." declarations override the presentation attributes. */
static void d2d_svg_apply_style(struct d2d_svg_props *props, const WCHAR *style)
{
    WCHAR name[64], value[256];
    const WCHAR *p = style;
    size_t n, v;

    while (*p)
    {
        p = d2d_svg_skip_space(p);
        for (n = 0; *p && *p != ':' && *p != ';' && n < ARRAY_SIZE(name) - 1; ++p)
            name[n++] = *p;
        while (n && (name[n - 1] == ' ' || name[n - 1] == '\t'))
            --n;
        name[n] = 0;
        if (*p != ':')
        {
            while (*p && *p != ';')
                ++p;
            if (*p)
                ++p;
            continue;
        }
        ++p;
        for (v = 0; *p && *p != ';' && v < ARRAY_SIZE(value) - 1; ++p)
            value[v++] = *p;
        while (v && (value[v - 1] == ' ' || value[v - 1] == '\t'))
            --v;
        value[v] = 0;
        while (*p && *p != ';')
            ++p;
        if (*p)
            ++p;
        d2d_svg_set_property(props, name, value);
    }
}

static const WCHAR *d2d_svg_get_attr(const struct d2d_svg_element *element, const WCHAR *name)
{
    size_t i;

    for (i = 0; i < element->attr_count; ++i)
    {
        if (!wcscmp(element->attrs[i].name, name))
            return element->attrs[i].value;
    }
    return NULL;
}

static BOOL d2d_svg_get_length_attr(const struct d2d_svg_element *element, const WCHAR *name,
        struct d2d_svg_length *length)
{
    const WCHAR *value = d2d_svg_get_attr(element, name);

    return value && d2d_svg_parse_length(value, length);
}

static float d2d_svg_length_attr(const struct d2d_svg_element *element, const WCHAR *name, float fallback,
        const D2D1_SIZE_F *base, int axis)
{
    struct d2d_svg_length length;

    if (!d2d_svg_get_length_attr(element, name, &length))
        return fallback;
    return d2d_svg_resolve_length(&length, base, axis);
}

/* Path data. */

struct d2d_svg_path_builder
{
    ID2D1GeometrySink *sink;
    D2D1_POINT_2F start, current, last_control;
    BOOL in_figure;
    WCHAR last_command;
};

static void d2d_svg_path_end(struct d2d_svg_path_builder *b, BOOL closed)
{
    if (!b->in_figure)
        return;
    ID2D1GeometrySink_EndFigure(b->sink, closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    b->in_figure = FALSE;
}

static void d2d_svg_path_ensure(struct d2d_svg_path_builder *b)
{
    if (b->in_figure)
        return;
    ID2D1GeometrySink_BeginFigure(b->sink, b->current, D2D1_FIGURE_BEGIN_FILLED);
    b->start = b->current;
    b->in_figure = TRUE;
}

static BOOL d2d_svg_parse_flag(const WCHAR **str, BOOL *flag)
{
    const WCHAR *p = d2d_svg_skip_separator(*str);

    if (*p != '0' && *p != '1')
        return FALSE;
    *flag = *p == '1';
    *str = p + 1;
    return TRUE;
}

static BOOL d2d_svg_parse_point(const WCHAR **p, D2D1_POINT_2F *point)
{
    if (!d2d_svg_parse_number(p, &point->x))
        return FALSE;
    *p = d2d_svg_skip_separator(*p);
    if (!d2d_svg_parse_number(p, &point->y))
        return FALSE;
    *p = d2d_svg_skip_separator(*p);
    return TRUE;
}

/* Rendering stops at the first error in path data, keeping what came before it. */
static void d2d_svg_build_path(ID2D1GeometrySink *sink, const WCHAR *data)
{
    struct d2d_svg_path_builder b = {sink};
    const WCHAR *p = d2d_svg_skip_space(data);
    WCHAR command = 0;

    while (*p)
    {
        D2D1_POINT_2F p1, p2, p3, offset;
        D2D1_BEZIER_SEGMENT bezier;
        D2D1_QUADRATIC_BEZIER_SEGMENT quad;
        D2D1_ARC_SEGMENT arc;
        BOOL relative, large, sweep;
        float value, rotation;

        p = d2d_svg_skip_space(p);
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z'))
            command = *p++;
        else if (!command || command == 'z' || command == 'Z')
            break;
        else if (command == 'M')
            command = 'L';
        else if (command == 'm')
            command = 'l';
        relative = command >= 'a';
        offset = relative ? b.current : (D2D1_POINT_2F){0.0f, 0.0f};
        p = d2d_svg_skip_space(p);
        switch (command)
        {
            case 'M': case 'm':
                if (!d2d_svg_parse_point(&p, &p1))
                {
                    d2d_svg_path_end(&b, FALSE);
                    return;
                }
                d2d_svg_path_end(&b, FALSE);
                b.current.x = p1.x + offset.x;
                b.current.y = p1.y + offset.y;
                d2d_svg_path_ensure(&b);
                b.last_control = b.current;
                break;
            case 'L': case 'l':
                if (!d2d_svg_parse_point(&p, &p1))
                {
                    d2d_svg_path_end(&b, FALSE);
                    return;
                }
                d2d_svg_path_ensure(&b);
                b.current.x = p1.x + offset.x;
                b.current.y = p1.y + offset.y;
                ID2D1GeometrySink_AddLine(sink, b.current);
                b.last_control = b.current;
                break;
            case 'H': case 'h': case 'V': case 'v':
                if (!d2d_svg_parse_number(&p, &value))
                {
                    d2d_svg_path_end(&b, FALSE);
                    return;
                }
                p = d2d_svg_skip_separator(p);
                d2d_svg_path_ensure(&b);
                if (command == 'H' || command == 'h')
                    b.current.x = value + offset.x;
                else
                    b.current.y = value + offset.y;
                ID2D1GeometrySink_AddLine(sink, b.current);
                b.last_control = b.current;
                break;
            case 'C': case 'c': case 'S': case 's':
                if (command == 'C' || command == 'c')
                {
                    if (!d2d_svg_parse_point(&p, &p1))
                    {
                        d2d_svg_path_end(&b, FALSE);
                        return;
                    }
                    p1.x += offset.x;
                    p1.y += offset.y;
                }
                else if (b.last_command == 'C' || b.last_command == 'c' || b.last_command == 'S'
                        || b.last_command == 's')
                {
                    p1.x = 2.0f * b.current.x - b.last_control.x;
                    p1.y = 2.0f * b.current.y - b.last_control.y;
                }
                else
                {
                    p1 = b.current;
                }
                if (!d2d_svg_parse_point(&p, &p2) || !d2d_svg_parse_point(&p, &p3))
                {
                    d2d_svg_path_end(&b, FALSE);
                    return;
                }
                d2d_svg_path_ensure(&b);
                bezier.point1 = p1;
                bezier.point2.x = p2.x + offset.x;
                bezier.point2.y = p2.y + offset.y;
                bezier.point3.x = p3.x + offset.x;
                bezier.point3.y = p3.y + offset.y;
                ID2D1GeometrySink_AddBezier(sink, &bezier);
                b.last_control = bezier.point2;
                b.current = bezier.point3;
                break;
            case 'Q': case 'q': case 'T': case 't':
                if (command == 'Q' || command == 'q')
                {
                    if (!d2d_svg_parse_point(&p, &p1))
                    {
                        d2d_svg_path_end(&b, FALSE);
                        return;
                    }
                    p1.x += offset.x;
                    p1.y += offset.y;
                }
                else if (b.last_command == 'Q' || b.last_command == 'q' || b.last_command == 'T'
                        || b.last_command == 't')
                {
                    p1.x = 2.0f * b.current.x - b.last_control.x;
                    p1.y = 2.0f * b.current.y - b.last_control.y;
                }
                else
                {
                    p1 = b.current;
                }
                if (!d2d_svg_parse_point(&p, &p2))
                {
                    d2d_svg_path_end(&b, FALSE);
                    return;
                }
                d2d_svg_path_ensure(&b);
                quad.point1 = p1;
                quad.point2.x = p2.x + offset.x;
                quad.point2.y = p2.y + offset.y;
                ID2D1GeometrySink_AddQuadraticBezier(sink, &quad);
                b.last_control = p1;
                b.current = quad.point2;
                break;
            case 'A': case 'a':
                if (!d2d_svg_parse_number(&p, &arc.size.width))
                {
                    d2d_svg_path_end(&b, FALSE);
                    return;
                }
                p = d2d_svg_skip_separator(p);
                if (!d2d_svg_parse_number(&p, &arc.size.height))
                {
                    d2d_svg_path_end(&b, FALSE);
                    return;
                }
                p = d2d_svg_skip_separator(p);
                if (!d2d_svg_parse_number(&p, &rotation) || !d2d_svg_parse_flag(&p, &large)
                        || !d2d_svg_parse_flag(&p, &sweep))
                {
                    d2d_svg_path_end(&b, FALSE);
                    return;
                }
                p = d2d_svg_skip_separator(p);
                if (!d2d_svg_parse_point(&p, &p1))
                {
                    d2d_svg_path_end(&b, FALSE);
                    return;
                }
                d2d_svg_path_ensure(&b);
                arc.point.x = p1.x + offset.x;
                arc.point.y = p1.y + offset.y;
                arc.size.width = fabsf(arc.size.width);
                arc.size.height = fabsf(arc.size.height);
                arc.rotationAngle = rotation;
                arc.sweepDirection = sweep ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
                arc.arcSize = large ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
                if (arc.point.x == b.current.x && arc.point.y == b.current.y)
                    break;
                if (arc.size.width == 0.0f || arc.size.height == 0.0f)
                    ID2D1GeometrySink_AddLine(sink, arc.point);
                else
                    ID2D1GeometrySink_AddArc(sink, &arc);
                b.current = arc.point;
                b.last_control = b.current;
                break;
            case 'Z': case 'z':
                d2d_svg_path_end(&b, TRUE);
                b.current = b.start;
                b.last_control = b.current;
                p = d2d_svg_skip_separator(p);
                break;
            default:
            {
                d2d_svg_path_end(&b, FALSE);
                return;
            }
        }
        b.last_command = command;
    }
    d2d_svg_path_end(&b, FALSE);
}

static BOOL d2d_svg_build_points(ID2D1GeometrySink *sink, const WCHAR *data, BOOL closed, BOOL filled)
{
    const WCHAR *p = d2d_svg_skip_space(data);
    D2D1_POINT_2F point;
    BOOL started = FALSE;

    while (*p && d2d_svg_parse_point(&p, &point))
    {
        if (!started)
        {
            ID2D1GeometrySink_BeginFigure(sink, point, filled ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
            started = TRUE;
        }
        else
        {
            ID2D1GeometrySink_AddLine(sink, point);
        }
    }
    if (started)
        ID2D1GeometrySink_EndFigure(sink, closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    return started;
}

static ID2D1Geometry *d2d_svg_create_path_geometry(ID2D1Factory *factory, D2D1_FILL_MODE fill_mode,
        enum d2d_svg_tag tag, const WCHAR *data, const D2D1_POINT_2F *line)
{
    ID2D1PathGeometry *path;
    ID2D1GeometrySink *sink;

    if (FAILED(ID2D1Factory_CreatePathGeometry(factory, &path)))
        return NULL;
    if (FAILED(ID2D1PathGeometry_Open(path, &sink)))
    {
        ID2D1PathGeometry_Release(path);
        return NULL;
    }
    ID2D1GeometrySink_SetFillMode(sink, fill_mode);
    if (tag == SVG_TAG_PATH)
        d2d_svg_build_path(sink, data);
    else if (tag == SVG_TAG_LINE)
    {
        ID2D1GeometrySink_BeginFigure(sink, line[0], D2D1_FIGURE_BEGIN_HOLLOW);
        ID2D1GeometrySink_AddLine(sink, line[1]);
        ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_OPEN);
    }
    else
        d2d_svg_build_points(sink, data, tag == SVG_TAG_POLYGON, TRUE);
    if (FAILED(ID2D1GeometrySink_Close(sink)))
    {
        ID2D1GeometrySink_Release(sink);
        ID2D1PathGeometry_Release(path);
        return NULL;
    }
    ID2D1GeometrySink_Release(sink);
    return (ID2D1Geometry *)path;
}

/* The geometry of a shape in its own user space, built on first use and kept for the fill rule it was built with. */
static ID2D1Geometry *d2d_svg_get_geometry(struct d2d_svg_element *element, const D2D1_SIZE_F *base,
        D2D1_FILL_MODE fill_mode)
{
    ID2D1Factory *factory = element->document->factory;
    ID2D1EllipseGeometry *ellipse_geometry;
    ID2D1RectangleGeometry *rect_geometry;
    ID2D1RoundedRectangleGeometry *rounded;
    D2D1_ROUNDED_RECT rounded_rect;
    D2D1_POINT_2F line[2];
    D2D1_ELLIPSE ellipse;
    struct d2d_svg_length length;
    BOOL has_rx, has_ry;
    float w, h, rx, ry;

    if (element->geometry_built && (element->geometry_fill_mode == fill_mode || (element->tag != SVG_TAG_PATH
            && element->tag != SVG_TAG_POLYGON && element->tag != SVG_TAG_POLYLINE)))
        return element->geometry;
    if (element->geometry)
        ID2D1Geometry_Release(element->geometry);
    element->geometry = NULL;
    element->geometry_built = TRUE;
    element->geometry_fill_mode = fill_mode;
    switch (element->tag)
    {
        case SVG_TAG_PATH:
            if (d2d_svg_get_attr(element, L"d"))
                element->geometry = d2d_svg_create_path_geometry(factory, fill_mode, SVG_TAG_PATH,
                        d2d_svg_get_attr(element, L"d"), NULL);
            break;
        case SVG_TAG_POLYGON:
        case SVG_TAG_POLYLINE:
            if (d2d_svg_get_attr(element, L"points"))
                element->geometry = d2d_svg_create_path_geometry(factory, fill_mode, element->tag,
                        d2d_svg_get_attr(element, L"points"), NULL);
            break;
        case SVG_TAG_LINE:
            line[0].x = d2d_svg_length_attr(element, L"x1", 0.0f, base, 0);
            line[0].y = d2d_svg_length_attr(element, L"y1", 0.0f, base, 1);
            line[1].x = d2d_svg_length_attr(element, L"x2", 0.0f, base, 0);
            line[1].y = d2d_svg_length_attr(element, L"y2", 0.0f, base, 1);
            element->geometry = d2d_svg_create_path_geometry(factory, fill_mode, SVG_TAG_LINE, NULL, line);
            break;
        case SVG_TAG_RECT:
            w = d2d_svg_length_attr(element, L"width", 0.0f, base, 0);
            h = d2d_svg_length_attr(element, L"height", 0.0f, base, 1);
            if (w <= 0.0f || h <= 0.0f)
                break;
            rounded_rect.rect.left = d2d_svg_length_attr(element, L"x", 0.0f, base, 0);
            rounded_rect.rect.top = d2d_svg_length_attr(element, L"y", 0.0f, base, 1);
            rounded_rect.rect.right = rounded_rect.rect.left + w;
            rounded_rect.rect.bottom = rounded_rect.rect.top + h;
            has_rx = d2d_svg_get_length_attr(element, L"rx", &length) && length.value >= 0.0f;
            rx = has_rx ? d2d_svg_resolve_length(&length, base, 0) : 0.0f;
            has_ry = d2d_svg_get_length_attr(element, L"ry", &length) && length.value >= 0.0f;
            ry = has_ry ? d2d_svg_resolve_length(&length, base, 1) : 0.0f;
            if (has_rx && !has_ry)
                ry = rx;
            else if (has_ry && !has_rx)
                rx = ry;
            rounded_rect.radiusX = min(rx, w / 2.0f);
            rounded_rect.radiusY = min(ry, h / 2.0f);
            if (rounded_rect.radiusX > 0.0f && rounded_rect.radiusY > 0.0f)
            {
                if (SUCCEEDED(ID2D1Factory_CreateRoundedRectangleGeometry(factory, &rounded_rect, &rounded)))
                    element->geometry = (ID2D1Geometry *)rounded;
            }
            else if (SUCCEEDED(ID2D1Factory_CreateRectangleGeometry(factory, &rounded_rect.rect, &rect_geometry)))
            {
                element->geometry = (ID2D1Geometry *)rect_geometry;
            }
            break;
        case SVG_TAG_CIRCLE:
        case SVG_TAG_ELLIPSE:
            ellipse.point.x = d2d_svg_length_attr(element, L"cx", 0.0f, base, 0);
            ellipse.point.y = d2d_svg_length_attr(element, L"cy", 0.0f, base, 1);
            if (element->tag == SVG_TAG_CIRCLE)
                ellipse.radiusX = ellipse.radiusY = d2d_svg_length_attr(element, L"r", 0.0f, base, 2);
            else
            {
                ellipse.radiusX = d2d_svg_length_attr(element, L"rx", 0.0f, base, 0);
                ellipse.radiusY = d2d_svg_length_attr(element, L"ry", 0.0f, base, 1);
            }
            if (ellipse.radiusX <= 0.0f || ellipse.radiusY <= 0.0f)
                break;
            if (SUCCEEDED(ID2D1Factory_CreateEllipseGeometry(factory, &ellipse, &ellipse_geometry)))
                element->geometry = (ID2D1Geometry *)ellipse_geometry;
            break;
        default:
            break;
    }
    if (element->geometry && FAILED(ID2D1Geometry_GetBounds(element->geometry, NULL, &element->bounds)))
        memset(&element->bounds, 0, sizeof(element->bounds));
    return element->geometry;
}

/* Element ids. */

static size_t d2d_svg_hash(const WCHAR *str, size_t length)
{
    size_t hash = 5381, i;

    for (i = 0; i < length && str[i]; ++i)
        hash = hash * 33 + str[i];
    return hash;
}

static struct d2d_svg_element *d2d_svg_find_id(const struct d2d_svg_document *document, const WCHAR *id,
        size_t length)
{
    size_t i, mask = document->id_capacity - 1;

    if (!document->id_capacity)
        return NULL;
    for (i = d2d_svg_hash(id, length) & mask; document->ids[i]; i = (i + 1) & mask)
    {
        const WCHAR *candidate = document->ids[i]->id;

        if (!wcsncmp(candidate, id, length) && !candidate[length])
            return document->ids[i];
    }
    return NULL;
}

static void d2d_svg_count_ids(const struct d2d_svg_element *element, size_t *count)
{
    size_t i;

    if (element->id)
        ++*count;
    for (i = 0; i < element->child_count; ++i)
        d2d_svg_count_ids(element->children[i], count);
}

/* The first element with an id keeps it. */
static void d2d_svg_add_ids(struct d2d_svg_document *document, struct d2d_svg_element *element)
{
    size_t i, mask = document->id_capacity - 1;

    if (element->id && !d2d_svg_find_id(document, element->id, wcslen(element->id)))
    {
        for (i = d2d_svg_hash(element->id, wcslen(element->id)) & mask; document->ids[i]; i = (i + 1) & mask)
            ;
        document->ids[i] = element;
    }
    for (i = 0; i < element->child_count; ++i)
        d2d_svg_add_ids(document, element->children[i]);
}

static BOOL d2d_svg_build_ids(struct d2d_svg_document *document)
{
    size_t count = 0, capacity = 16;

    free(document->ids);
    document->ids = NULL;
    document->id_capacity = 0;
    if (!document->root)
        return TRUE;
    d2d_svg_count_ids(document->root, &count);
    while (capacity < count * 2)
        capacity *= 2;
    if (!(document->ids = calloc(capacity, sizeof(*document->ids))))
        return FALSE;
    document->id_capacity = capacity;
    d2d_svg_add_ids(document, document->root);
    return TRUE;
}

static struct d2d_svg_element *d2d_svg_resolve_href(const struct d2d_svg_element *element)
{
    const WCHAR *href = d2d_svg_get_attr(element, L"xlink:href");

    if (!href)
        href = d2d_svg_get_attr(element, L"href");
    href = href ? d2d_svg_skip_space(href) : NULL;
    if (!href || *href != '#')
        return NULL;
    return d2d_svg_find_id(element->document, href + 1, wcslen(href + 1));
}

/* Styles. */

static void d2d_svg_style_init(struct d2d_svg_style *style)
{
    static struct d2d_svg_paint black = {SVG_PAINT_COLOR, {0.0f, 0.0f, 0.0f, 1.0f}};
    static struct d2d_svg_paint none = {SVG_PAINT_NONE};

    memset(style, 0, sizeof(*style));
    style->fill = &black;
    style->stroke = &none;
    style->fill_opacity = 1.0f;
    style->stroke_opacity = 1.0f;
    style->stroke_width = 1.0f;
    style->miter_limit = 4.0f;
    style->color.a = 1.0f;
    style->fill_rule = D2D1_FILL_MODE_WINDING;
    style->clip_rule = D2D1_FILL_MODE_WINDING;
    style->cap = D2D1_CAP_STYLE_FLAT;
    style->join = D2D1_LINE_JOIN_MITER_OR_BEVEL;
}

static void d2d_svg_style_inherit(struct d2d_svg_style *style, struct d2d_svg_props *props)
{
    if (props->set & SVG_PROP_FILL)
        style->fill = &props->fill;
    if (props->set & SVG_PROP_STROKE)
        style->stroke = &props->stroke;
    if (props->set & SVG_PROP_FILL_OPACITY)
        style->fill_opacity = props->fill_opacity;
    if (props->set & SVG_PROP_STROKE_OPACITY)
        style->stroke_opacity = props->stroke_opacity;
    if (props->set & SVG_PROP_STROKE_WIDTH)
        style->stroke_width = props->stroke_width;
    if (props->set & SVG_PROP_MITER_LIMIT)
        style->miter_limit = props->miter_limit;
    if (props->set & SVG_PROP_DASH_OFFSET)
        style->dash_offset = props->dash_offset;
    if (props->set & SVG_PROP_DASH_ARRAY)
    {
        style->dashes = props->dashes;
        style->dash_count = props->dash_count;
    }
    if (props->set & SVG_PROP_COLOR)
        style->color = props->color;
    if (props->set & SVG_PROP_FILL_RULE)
        style->fill_rule = props->fill_rule;
    if (props->set & SVG_PROP_CLIP_RULE)
        style->clip_rule = props->clip_rule;
    if (props->set & SVG_PROP_LINE_CAP)
        style->cap = props->cap;
    if (props->set & SVG_PROP_LINE_JOIN)
        style->join = props->join;
    if (props->set & SVG_PROP_VISIBILITY)
        style->hidden = props->hidden;
}

/* Brushes. */

static struct d2d_svg_element *d2d_svg_resolve_paint(struct d2d_svg_element *element, struct d2d_svg_paint *paint)
{
    struct d2d_svg_element *target;

    if (paint->kind != SVG_PAINT_URL)
        return NULL;
    if (!paint->target_resolved)
    {
        target = d2d_svg_find_id(element->document, paint->url, wcslen(paint->url));
        paint->target = target && (target->tag == SVG_TAG_LINEAR_GRADIENT || target->tag == SVG_TAG_RADIAL_GRADIENT)
                ? target : NULL;
        paint->target_resolved = TRUE;
    }
    return paint->target;
}

/* A gradient takes the attributes and the stops it lacks from the gradient it references. */
static const WCHAR *d2d_svg_gradient_attr(struct d2d_svg_element *gradient, const WCHAR *name)
{
    unsigned int depth;
    const WCHAR *value;

    for (depth = 0; gradient && depth < 16; ++depth)
    {
        if ((value = d2d_svg_get_attr(gradient, name)))
            return value;
        gradient = d2d_svg_resolve_href(gradient);
        if (gradient && gradient->tag != SVG_TAG_LINEAR_GRADIENT && gradient->tag != SVG_TAG_RADIAL_GRADIENT)
            return NULL;
    }
    return NULL;
}

static struct d2d_svg_element *d2d_svg_gradient_stops(struct d2d_svg_element *gradient)
{
    unsigned int depth;
    size_t i;

    for (depth = 0; gradient && depth < 16; ++depth)
    {
        for (i = 0; i < gradient->child_count; ++i)
        {
            if (gradient->children[i]->tag == SVG_TAG_STOP)
                return gradient;
        }
        gradient = d2d_svg_resolve_href(gradient);
        if (gradient && gradient->tag != SVG_TAG_LINEAR_GRADIENT && gradient->tag != SVG_TAG_RADIAL_GRADIENT)
            return NULL;
    }
    return NULL;
}

static float d2d_svg_gradient_coord(struct d2d_svg_element *gradient, const WCHAR *name, float fallback,
        BOOL bbox, const D2D1_SIZE_F *base, int axis)
{
    const WCHAR *value = d2d_svg_gradient_attr(gradient, name);
    struct d2d_svg_length length;

    if (!value || !d2d_svg_parse_length(value, &length))
        return fallback;
    if (bbox)
        return length.percent ? length.value / 100.0f : length.value;
    return d2d_svg_resolve_length(&length, base, axis);
}

static ID2D1Brush *d2d_svg_create_gradient_brush(ID2D1DeviceContext *context, struct d2d_svg_element *gradient,
        float opacity, const D2D1_RECT_F *bounds, const D2D1_SIZE_F *base, D2D1_COLOR_F *single)
{
    D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES linear;
    D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES radial;
    D2D1_GRADIENT_STOP stops[64];
    D2D1_BRUSH_PROPERTIES brush_props;
    D2D1_MATRIX_3X2_F gradient_transform = d2d_svg_identity, bbox_matrix = d2d_svg_identity;
    ID2D1GradientStopCollection *collection;
    struct d2d_svg_element *stop_owner = d2d_svg_gradient_stops(gradient), *stop;
    D2D1_EXTEND_MODE extend = D2D1_EXTEND_MODE_CLAMP;
    ID2D1LinearGradientBrush *linear_brush;
    ID2D1RadialGradientBrush *radial_brush;
    const WCHAR *value;
    UINT32 count = 0;
    BOOL bbox = TRUE;
    float offset, last = 0.0f;
    size_t i;

    if (!stop_owner)
        return NULL;
    for (i = 0; i < stop_owner->child_count && count < ARRAY_SIZE(stops); ++i)
    {
        stop = stop_owner->children[i];
        if (stop->tag != SVG_TAG_STOP)
            continue;
        offset = 0.0f;
        if ((value = d2d_svg_get_attr(stop, L"offset")))
            d2d_svg_parse_opacity(value, &offset);
        offset = max(offset, last);
        last = offset;
        stops[count].position = offset;
        stops[count].color = (stop->props.set & SVG_PROP_STOP_COLOR) ? stop->props.stop_color
                : (D2D1_COLOR_F){0.0f, 0.0f, 0.0f, 1.0f};
        if (stop->props.set & SVG_PROP_STOP_OPACITY)
            stops[count].color.a *= stop->props.stop_opacity;
        ++count;
    }
    if (!count)
        return NULL;
    /* A single stop paints its color, like a solid paint. */
    if (count == 1)
    {
        *single = stops[0].color;
        return NULL;
    }
    if ((value = d2d_svg_gradient_attr(gradient, L"spreadMethod")))
    {
        if (!wcscmp(value, L"reflect"))
            extend = D2D1_EXTEND_MODE_MIRROR;
        else if (!wcscmp(value, L"repeat"))
            extend = D2D1_EXTEND_MODE_WRAP;
    }
    if ((value = d2d_svg_gradient_attr(gradient, L"gradientUnits")) && !wcscmp(value, L"userSpaceOnUse"))
        bbox = FALSE;
    if ((value = d2d_svg_gradient_attr(gradient, L"gradientTransform")))
        d2d_svg_parse_transform(value, &gradient_transform);
    if (bbox)
    {
        if (bounds->right <= bounds->left || bounds->bottom <= bounds->top)
            return NULL;
        bbox_matrix._11 = bounds->right - bounds->left;
        bbox_matrix._22 = bounds->bottom - bounds->top;
        bbox_matrix._31 = bounds->left;
        bbox_matrix._32 = bounds->top;
    }
    d2d_svg_multiply(&gradient_transform, &bbox_matrix);
    brush_props.opacity = opacity;
    brush_props.transform = gradient_transform;
    if (FAILED(ID2D1RenderTarget_CreateGradientStopCollection((ID2D1RenderTarget *)context, stops, count,
            D2D1_GAMMA_2_2, extend,
            &collection)))
        return NULL;
    if (gradient->tag == SVG_TAG_LINEAR_GRADIENT)
    {
        linear.startPoint.x = d2d_svg_gradient_coord(gradient, L"x1", 0.0f, bbox, base, 0);
        linear.startPoint.y = d2d_svg_gradient_coord(gradient, L"y1", 0.0f, bbox, base, 1);
        linear.endPoint.x = d2d_svg_gradient_coord(gradient, L"x2", bbox ? 1.0f : base->width, bbox, base, 0);
        linear.endPoint.y = d2d_svg_gradient_coord(gradient, L"y2", 0.0f, bbox, base, 1);
        if (FAILED(ID2D1DeviceContext_CreateLinearGradientBrush(context, &linear, &brush_props, collection,
                &linear_brush)))
            linear_brush = NULL;
        ID2D1GradientStopCollection_Release(collection);
        return (ID2D1Brush *)linear_brush;
    }
    radial.center.x = d2d_svg_gradient_coord(gradient, L"cx", bbox ? 0.5f : base->width / 2.0f, bbox, base, 0);
    radial.center.y = d2d_svg_gradient_coord(gradient, L"cy", bbox ? 0.5f : base->height / 2.0f, bbox, base, 1);
    radial.radiusX = radial.radiusY = d2d_svg_gradient_coord(gradient, L"r", bbox ? 0.5f
            : sqrtf((base->width * base->width + base->height * base->height) / 2.0f) / 2.0f, bbox, base, 2);
    radial.gradientOriginOffset.x = d2d_svg_gradient_coord(gradient, L"fx", radial.center.x, bbox, base, 0)
            - radial.center.x;
    radial.gradientOriginOffset.y = d2d_svg_gradient_coord(gradient, L"fy", radial.center.y, bbox, base, 1)
            - radial.center.y;
    if (FAILED(ID2D1DeviceContext_CreateRadialGradientBrush(context, &radial, &brush_props, collection,
            &radial_brush)))
        radial_brush = NULL;
    ID2D1GradientStopCollection_Release(collection);
    return (ID2D1Brush *)radial_brush;
}

static ID2D1Brush *d2d_svg_get_brush(struct d2d_svg_draw *draw, struct d2d_svg_element *element,
        struct d2d_svg_brush_cache *cache, struct d2d_svg_paint *paint, const D2D1_COLOR_F *current_color,
        float opacity)
{
    enum d2d_svg_paint_kind kind = paint->kind;
    struct d2d_svg_element *gradient = d2d_svg_resolve_paint(element, paint);
    D2D1_COLOR_F color = paint->color, single;
    ID2D1SolidColorBrush *solid;

    if (kind == SVG_PAINT_URL && !gradient)
        kind = paint->fallback;
    if (kind == SVG_PAINT_NONE)
        return NULL;
    if (kind == SVG_PAINT_CURRENT_COLOR)
        color = *current_color;
    if (kind != SVG_PAINT_URL)
        gradient = NULL;
    if (cache->brush && cache->context == draw->context && cache->kind == kind && cache->opacity == opacity
            && cache->gradient == gradient && (gradient || !memcmp(&cache->color, &color, sizeof(color))))
        return cache->brush;
    if (cache->brush)
        ID2D1Brush_Release(cache->brush);
    cache->brush = NULL;
    cache->context = draw->context;
    cache->kind = kind;
    cache->opacity = opacity;
    cache->gradient = gradient;
    cache->color = color;
    if (gradient)
    {
        single.a = -1.0f;
        cache->brush = d2d_svg_create_gradient_brush(draw->context, gradient, opacity, &element->bounds,
                &draw->percent_base, &single);
        if (cache->brush || single.a < 0.0f)
            return cache->brush;
        color = single;
    }
    color.a *= opacity;
    if (SUCCEEDED(ID2D1DeviceContext_CreateSolidColorBrush(draw->context, &color, NULL, &solid)))
        cache->brush = (ID2D1Brush *)solid;
    return cache->brush;
}

/* SVG dashes are in user units, Direct2D dashes in units of the stroke width. */
static ID2D1StrokeStyle *d2d_svg_get_stroke_style(struct d2d_svg_element *element, const struct d2d_svg_style *style)
{
    D2D1_STROKE_STYLE_PROPERTIES props;
    float dashes[64];
    UINT32 i, count = style->dash_count;

    props.startCap = props.endCap = props.dashCap = style->cap;
    props.lineJoin = style->join;
    props.miterLimit = style->miter_limit;
    props.dashStyle = count ? D2D1_DASH_STYLE_CUSTOM : D2D1_DASH_STYLE_SOLID;
    props.dashOffset = count && style->stroke_width > 0.0f ? style->dash_offset / style->stroke_width : 0.0f;
    if (element->stroke_style && !memcmp(&element->stroke_props, &props, sizeof(props))
            && element->stroke_dashes == style->dashes && element->stroke_dash_width == style->stroke_width)
        return element->stroke_style;
    if (element->stroke_style)
        ID2D1StrokeStyle_Release(element->stroke_style);
    element->stroke_style = NULL;
    element->stroke_props = props;
    element->stroke_dashes = style->dashes;
    element->stroke_dash_width = style->stroke_width;
    count = min(count, ARRAY_SIZE(dashes));
    for (i = 0; i < count; ++i)
        dashes[i] = style->dashes[i] / style->stroke_width;
    if (FAILED(ID2D1Factory_CreateStrokeStyle(element->document->factory, &props, count ? dashes : NULL, count,
            &element->stroke_style)))
        element->stroke_style = NULL;
    return element->stroke_style;
}

/* Drawing. */

static void d2d_svg_draw_element(struct d2d_svg_draw *draw, struct d2d_svg_element *element,
        const struct d2d_svg_style *parent_style, const D2D1_MATRIX_3X2_F *parent_matrix);

static struct d2d_svg_element *d2d_svg_resolve_clip(struct d2d_svg_element *element)
{
    struct d2d_svg_element *target;

    if (!element->props.clip_path)
        return NULL;
    if (!element->props.clip_resolved)
    {
        target = d2d_svg_find_id(element->document, element->props.clip_path, wcslen(element->props.clip_path));
        element->props.clip_target = target && target->tag == SVG_TAG_CLIP_PATH ? target : NULL;
        element->props.clip_resolved = TRUE;
    }
    return element->props.clip_target;
}

/* Adds the shapes of a clip path, each under its transform, to a list of transformed geometries. */
static void d2d_svg_collect_clip(struct d2d_svg_draw *draw, struct d2d_svg_element *element,
        const D2D1_MATRIX_3X2_F *matrix, D2D1_FILL_MODE rule, ID2D1Geometry **list, UINT32 *count,
        UINT32 capacity, unsigned int depth)
{
    ID2D1TransformedGeometry *transformed;
    struct d2d_svg_element *target;
    D2D1_MATRIX_3X2_F child_matrix;
    ID2D1Geometry *geometry;

    if (depth > 8 || (element->props.set & SVG_PROP_DISPLAY && element->props.display_none))
        return;
    child_matrix = *matrix;
    if (element->has_transform)
    {
        child_matrix = element->transform;
        d2d_svg_multiply(&child_matrix, matrix);
    }
    if (element->props.set & SVG_PROP_CLIP_RULE)
        rule = element->props.clip_rule;
    if (element->tag == SVG_TAG_USE)
    {
        D2D1_MATRIX_3X2_F use_matrix = d2d_svg_identity;

        if (!(target = d2d_svg_resolve_href(element)) || target->tag == SVG_TAG_USE)
            return;
        use_matrix._31 = d2d_svg_length_attr(element, L"x", 0.0f, &draw->percent_base, 0);
        use_matrix._32 = d2d_svg_length_attr(element, L"y", 0.0f, &draw->percent_base, 1);
        d2d_svg_multiply(&use_matrix, &child_matrix);
        d2d_svg_collect_clip(draw, target, &use_matrix, rule, list, count, capacity, depth + 1);
        return;
    }
    if (*count >= capacity || !(geometry = d2d_svg_get_geometry(element, &draw->percent_base, rule)))
        return;
    if (SUCCEEDED(ID2D1Factory_CreateTransformedGeometry(element->document->factory, geometry, &child_matrix,
            &transformed)))
        list[(*count)++] = (ID2D1Geometry *)transformed;
}

/* The clip region is the union of the clip path's shapes; an empty clip path hides everything. */
static ID2D1Geometry *d2d_svg_create_clip(struct d2d_svg_draw *draw, struct d2d_svg_element *clip,
        struct d2d_svg_element *element)
{
    ID2D1Geometry *list[256];
    ID2D1GeometryGroup *group;
    D2D1_MATRIX_3X2_F matrix = d2d_svg_identity;
    const WCHAR *units = d2d_svg_get_attr(clip, L"clipPathUnits");
    UINT32 count = 0, i;
    size_t c;

    if (units && !wcscmp(units, L"objectBoundingBox"))
    {
        /* TODO: object bounding box clip units for groups; shapes use their own bounds */
        if (element->bounds.right <= element->bounds.left || element->bounds.bottom <= element->bounds.top)
            return NULL;
        matrix._11 = element->bounds.right - element->bounds.left;
        matrix._22 = element->bounds.bottom - element->bounds.top;
        matrix._31 = element->bounds.left;
        matrix._32 = element->bounds.top;
    }
    if (clip->has_transform)
    {
        D2D1_MATRIX_3X2_F t = clip->transform;

        d2d_svg_multiply(&t, &matrix);
        matrix = t;
    }
    for (c = 0; c < clip->child_count; ++c)
        d2d_svg_collect_clip(draw, clip->children[c], &matrix, D2D1_FILL_MODE_WINDING, list, &count,
                ARRAY_SIZE(list), 0);
    if (FAILED(ID2D1Factory_CreateGeometryGroup(clip->document->factory, D2D1_FILL_MODE_WINDING, list, count,
            &group)))
        group = NULL;
    for (i = 0; i < count; ++i)
        ID2D1Geometry_Release(list[i]);
    return (ID2D1Geometry *)group;
}

static BOOL d2d_svg_push_layer(struct d2d_svg_draw *draw, const D2D1_MATRIX_3X2_F *matrix, ID2D1Geometry *mask,
        float opacity)
{
    D2D1_LAYER_PARAMETERS1 params;

    ID2D1DeviceContext_SetTransform(draw->context, matrix);
    params.contentBounds.left = -FLT_MAX;
    params.contentBounds.top = -FLT_MAX;
    params.contentBounds.right = FLT_MAX;
    params.contentBounds.bottom = FLT_MAX;
    params.geometricMask = mask;
    params.maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
    params.maskTransform = d2d_svg_identity;
    params.opacity = opacity;
    params.opacityBrush = NULL;
    params.layerOptions = D2D1_LAYER_OPTIONS1_NONE;
    ID2D1DeviceContext_PushLayer(draw->context, &params, NULL);
    return TRUE;
}

static void d2d_svg_draw_shape(struct d2d_svg_draw *draw, struct d2d_svg_element *element,
        const struct d2d_svg_style *style, const D2D1_MATRIX_3X2_F *matrix)
{
    ID2D1Geometry *geometry;
    ID2D1StrokeStyle *stroke_style;
    ID2D1Brush *brush;

    if (style->hidden || !(geometry = d2d_svg_get_geometry(element, &draw->percent_base, style->fill_rule)))
        return;
    ID2D1DeviceContext_SetTransform(draw->context, matrix);
    if (element->tag != SVG_TAG_LINE && (brush = d2d_svg_get_brush(draw, element, &element->fill_cache, style->fill,
            &style->color, style->fill_opacity)))
        ID2D1DeviceContext_FillGeometry(draw->context, geometry, brush, NULL);
    if (style->stroke_width > 0.0f && (brush = d2d_svg_get_brush(draw, element, &element->stroke_cache, style->stroke,
            &style->color, style->stroke_opacity)))
    {
        stroke_style = d2d_svg_get_stroke_style(element, style);
        ID2D1DeviceContext_DrawGeometry(draw->context, geometry, brush, style->stroke_width, stroke_style);
    }
}

static void d2d_svg_draw_children(struct d2d_svg_draw *draw, struct d2d_svg_element *element,
        const struct d2d_svg_style *style, const D2D1_MATRIX_3X2_F *matrix)
{
    size_t i;

    for (i = 0; i < element->child_count; ++i)
        d2d_svg_draw_element(draw, element->children[i], style, matrix);
}

static void d2d_svg_draw_element(struct d2d_svg_draw *draw, struct d2d_svg_element *element,
        const struct d2d_svg_style *parent_style, const D2D1_MATRIX_3X2_F *parent_matrix)
{
    struct d2d_svg_element *clip, *target;
    D2D1_MATRIX_3X2_F matrix = *parent_matrix, use_matrix;
    ID2D1Geometry *clip_geometry = NULL;
    struct d2d_svg_style style;
    unsigned int layers = 0;
    float opacity;

    switch (element->tag)
    {
        case SVG_TAG_G:
        case SVG_TAG_USE:
        case SVG_TAG_SVG:
        case SVG_TAG_PATH:
        case SVG_TAG_RECT:
        case SVG_TAG_CIRCLE:
        case SVG_TAG_ELLIPSE:
        case SVG_TAG_LINE:
        case SVG_TAG_POLYLINE:
        case SVG_TAG_POLYGON:
            break;
        default:
            /* TODO: base64 image elements are not drawn */
            return;
    }
    if (element->props.set & SVG_PROP_DISPLAY && element->props.display_none)
        return;
    style = *parent_style;
    d2d_svg_style_inherit(&style, &element->props);
    if (element->has_transform)
    {
        matrix = element->transform;
        d2d_svg_multiply(&matrix, parent_matrix);
    }
    if ((clip = d2d_svg_resolve_clip(element)))
    {
        if (element->tag != SVG_TAG_G && element->tag != SVG_TAG_USE && element->tag != SVG_TAG_SVG)
            d2d_svg_get_geometry(element, &draw->percent_base, style.fill_rule);
        if (!(clip_geometry = d2d_svg_create_clip(draw, clip, element)))
            return;
        d2d_svg_push_layer(draw, &matrix, clip_geometry, 1.0f);
        ++layers;
    }
    opacity = element->props.set & SVG_PROP_OPACITY ? element->props.opacity : 1.0f;
    if (opacity <= 0.0f)
        layers += 0;
    else if (opacity < 1.0f)
    {
        d2d_svg_push_layer(draw, &matrix, NULL, opacity);
        ++layers;
    }
    if (opacity > 0.0f)
    {
        switch (element->tag)
        {
            case SVG_TAG_G:
            case SVG_TAG_SVG:
                /* TODO: nested svg elements do not establish a new viewport */
                d2d_svg_draw_children(draw, element, &style, &matrix);
                break;
            case SVG_TAG_USE:
                if (draw->use_depth < 16 && (target = d2d_svg_resolve_href(element)))
                {
                    use_matrix = d2d_svg_identity;
                    use_matrix._31 = d2d_svg_length_attr(element, L"x", 0.0f, &draw->percent_base, 0);
                    use_matrix._32 = d2d_svg_length_attr(element, L"y", 0.0f, &draw->percent_base, 1);
                    d2d_svg_multiply(&use_matrix, &matrix);
                    ++draw->use_depth;
                    d2d_svg_draw_element(draw, target, &style, &use_matrix);
                    --draw->use_depth;
                }
                break;
            default:
                d2d_svg_draw_shape(draw, element, &style, &matrix);
                break;
        }
    }
    while (layers--)
        ID2D1DeviceContext_PopLayer(draw->context);
    if (clip_geometry)
        ID2D1Geometry_Release(clip_geometry);
}

/* The root's viewBox maps onto the viewport following preserveAspectRatio (default xMidYMid meet). */
static void d2d_svg_viewbox_matrix(const struct d2d_svg_element *root, const D2D1_SIZE_F *viewport,
        D2D1_MATRIX_3X2_F *matrix, D2D1_SIZE_F *percent_base)
{
    const WCHAR *value = d2d_svg_get_attr(root, L"viewBox"), *p;
    float box[4], width, height, sx, sy, tx = 0.0f, ty = 0.0f;
    D2D1_SVG_ASPECT_ALIGN align = D2D1_SVG_ASPECT_ALIGN_X_MID_Y_MID;
    BOOL slice = FALSE;
    unsigned int i;

    width = d2d_svg_length_attr(root, L"width", viewport->width, viewport, 0);
    height = d2d_svg_length_attr(root, L"height", viewport->height, viewport, 1);
    *matrix = d2d_svg_identity;
    *percent_base = *viewport;
    if (!value)
        return;
    p = value;
    for (i = 0; i < 4; ++i)
    {
        if (!d2d_svg_parse_number(&p, &box[i]))
            return;
        p = d2d_svg_skip_separator(p);
    }
    if (box[2] <= 0.0f || box[3] <= 0.0f)
        return;
    percent_base->width = box[2];
    percent_base->height = box[3];
    if ((value = d2d_svg_get_attr(root, L"preserveAspectRatio")))
    {
        static const WCHAR *aligns[] = {L"none", L"xMinYMin", L"xMidYMin", L"xMaxYMin", L"xMinYMid", L"xMidYMid",
                L"xMaxYMid", L"xMinYMax", L"xMidYMax", L"xMaxYMax"};

        p = d2d_svg_skip_space(value);
        if (!wcsncmp(p, L"defer", 5))
            p = d2d_svg_skip_space(p + 5);
        for (i = 0; i < ARRAY_SIZE(aligns); ++i)
        {
            if (!wcsncmp(p, aligns[i], wcslen(aligns[i])))
            {
                align = i;
                p = d2d_svg_skip_space(p + wcslen(aligns[i]));
                break;
            }
        }
        slice = !wcsncmp(p, L"slice", 5);
    }
    sx = width / box[2];
    sy = height / box[3];
    if (align != D2D1_SVG_ASPECT_ALIGN_NONE)
    {
        sx = sy = slice ? max(sx, sy) : min(sx, sy);
        if (align == D2D1_SVG_ASPECT_ALIGN_X_MID_Y_MIN || align == D2D1_SVG_ASPECT_ALIGN_X_MID_Y_MID
                || align == D2D1_SVG_ASPECT_ALIGN_X_MID_Y_MAX)
            tx = (width - box[2] * sx) / 2.0f;
        else if (align == D2D1_SVG_ASPECT_ALIGN_X_MAX_Y_MIN || align == D2D1_SVG_ASPECT_ALIGN_X_MAX_Y_MID
                || align == D2D1_SVG_ASPECT_ALIGN_X_MAX_Y_MAX)
            tx = width - box[2] * sx;
        if (align == D2D1_SVG_ASPECT_ALIGN_X_MIN_Y_MID || align == D2D1_SVG_ASPECT_ALIGN_X_MID_Y_MID
                || align == D2D1_SVG_ASPECT_ALIGN_X_MAX_Y_MID)
            ty = (height - box[3] * sy) / 2.0f;
        else if (align == D2D1_SVG_ASPECT_ALIGN_X_MIN_Y_MAX || align == D2D1_SVG_ASPECT_ALIGN_X_MID_Y_MAX
                || align == D2D1_SVG_ASPECT_ALIGN_X_MAX_Y_MAX)
            ty = height - box[3] * sy;
    }
    matrix->_11 = sx;
    matrix->_22 = sy;
    matrix->_31 = tx - box[0] * sx;
    matrix->_32 = ty - box[1] * sy;
}

void d2d_svg_document_draw(ID2D1SvgDocument *iface, ID2D1DeviceContext *context)
{
    struct d2d_svg_document *document = impl_from_ID2D1SvgDocument(iface);
    D2D1_MATRIX_3X2_F world, matrix;
    struct d2d_svg_style style;
    struct d2d_svg_draw draw;
    D2D1_RECT_F viewport;

    if (!document->root)
        return;
    draw.context = context;
    draw.document = document;
    draw.use_depth = 0;
    ID2D1DeviceContext_GetTransform(context, &world);
    d2d_svg_viewbox_matrix(document->root, &document->viewport, &matrix, &draw.percent_base);
    d2d_svg_multiply(&matrix, &world);
    d2d_svg_style_init(&style);
    /* The root element clips its content to the viewport. */
    viewport.left = 0.0f;
    viewport.top = 0.0f;
    viewport.right = document->viewport.width;
    viewport.bottom = document->viewport.height;
    ID2D1DeviceContext_PushAxisAlignedClip(context, &viewport, D2D1_ANTIALIAS_MODE_ALIASED);
    d2d_svg_draw_element(&draw, document->root, &style, &matrix);
    ID2D1DeviceContext_SetTransform(context, &world);
    ID2D1DeviceContext_PopAxisAlignedClip(context);
}

/* Elements. */

static const struct ID2D1SvgElementVtbl d2d_svg_element_vtbl;

static void d2d_svg_element_destroy(struct d2d_svg_element *element)
{
    size_t i;

    for (i = 0; i < element->child_count; ++i)
        d2d_svg_element_destroy(element->children[i]);
    free(element->children);
    for (i = 0; i < element->attr_count; ++i)
    {
        free(element->attrs[i].name);
        free(element->attrs[i].value);
    }
    free(element->attrs);
    free(element->tag_name);
    free((void *)element->props.fill.url);
    free((void *)element->props.stroke.url);
    free((void *)element->props.clip_path);
    free(element->props.dashes);
    if (element->geometry)
        ID2D1Geometry_Release(element->geometry);
    if (element->fill_cache.brush)
        ID2D1Brush_Release(element->fill_cache.brush);
    if (element->stroke_cache.brush)
        ID2D1Brush_Release(element->stroke_cache.brush);
    if (element->stroke_style)
        ID2D1StrokeStyle_Release(element->stroke_style);
    free(element);
}

static enum d2d_svg_tag d2d_svg_lookup_tag(const WCHAR *name)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(d2d_svg_tags); ++i)
    {
        if (!wcscmp(name, d2d_svg_tags[i].name))
            return d2d_svg_tags[i].tag;
    }
    return SVG_TAG_UNSUPPORTED;
}

static struct d2d_svg_element *d2d_svg_element_create(struct d2d_svg_document *document, const WCHAR *name,
        enum d2d_svg_tag tag)
{
    struct d2d_svg_element *element;

    if (!(element = calloc(1, sizeof(*element))))
        return NULL;
    element->ID2D1SvgElement_iface.lpVtbl = &d2d_svg_element_vtbl;
    element->document = document;
    element->tag = tag;
    element->transform = d2d_svg_identity;
    if (!(element->tag_name = d2d_svg_strndup(name, wcslen(name))))
    {
        free(element);
        return NULL;
    }
    return element;
}

static BOOL d2d_svg_element_add_attr(struct d2d_svg_element *element, const WCHAR *name, const WCHAR *value)
{
    struct d2d_svg_attr *attr;

    if (!d2d_array_reserve((void **)&element->attrs, &element->attr_capacity, element->attr_count + 1,
            sizeof(*element->attrs)))
        return FALSE;
    attr = &element->attrs[element->attr_count];
    if (!(attr->name = d2d_svg_strndup(name, wcslen(name))) || !(attr->value = d2d_svg_strndup(value, wcslen(value))))
    {
        free(attr->name);
        return FALSE;
    }
    ++element->attr_count;
    return TRUE;
}

static BOOL d2d_svg_element_add_child(struct d2d_svg_element *parent, struct d2d_svg_element *child)
{
    if (!d2d_array_reserve((void **)&parent->children, &parent->child_capacity, parent->child_count + 1,
            sizeof(*parent->children)))
        return FALSE;
    parent->children[parent->child_count++] = child;
    child->parent = parent;
    return TRUE;
}

/* Presentation attributes first, then the style declarations, which take precedence. */
static void d2d_svg_element_parse_props(struct d2d_svg_element *element)
{
    static const WCHAR *presentation[] =
    {
        L"clip-path", L"clip-rule", L"color", L"display", L"fill", L"fill-opacity", L"fill-rule", L"opacity",
        L"overflow", L"stop-color", L"stop-opacity", L"stroke", L"stroke-dasharray", L"stroke-dashoffset",
        L"stroke-linecap", L"stroke-linejoin", L"stroke-miterlimit", L"stroke-opacity", L"stroke-width",
        L"visibility",
    };
    const WCHAR *value;
    unsigned int i;
    size_t a;

    for (a = 0; a < element->attr_count; ++a)
    {
        for (i = 0; i < ARRAY_SIZE(presentation); ++i)
        {
            if (!wcscmp(element->attrs[a].name, presentation[i]))
            {
                d2d_svg_set_property(&element->props, element->attrs[a].name, element->attrs[a].value);
                break;
            }
        }
        if (!wcscmp(element->attrs[a].name, L"id"))
            element->id = element->attrs[a].value;
    }
    if ((value = d2d_svg_get_attr(element, L"style")))
        d2d_svg_apply_style(&element->props, value);
    if (element->tag != SVG_TAG_LINEAR_GRADIENT && element->tag != SVG_TAG_RADIAL_GRADIENT
            && element->tag != SVG_TAG_STOP && element->tag != SVG_TAG_SVG
            && (value = d2d_svg_get_attr(element, L"transform")))
        element->has_transform = d2d_svg_parse_transform(value, &element->transform);
}

static HRESULT d2d_svg_parse(struct d2d_svg_document *document, IStream *stream)
{
    static const WCHAR svg_ns[] = L"http://www.w3.org/2000/svg";
    static const WCHAR xlink_ns[] = L"http://www.w3.org/1999/xlink";
    struct d2d_svg_element *current = NULL, *element;
    const WCHAR *name, *ns, *value;
    unsigned int skip = 0;
    XmlNodeType type;
    IXmlReader *reader;
    BOOL empty;
    HRESULT hr;

    if (FAILED(hr = CreateXmlReader(&IID_IXmlReader, (void **)&reader, NULL)))
        return hr;
    if (FAILED(hr = IXmlReader_SetProperty(reader, XmlReaderProperty_DtdProcessing, DtdProcessing_Parse))
            || FAILED(hr = IXmlReader_SetInput(reader, (IUnknown *)stream)))
    {
        IXmlReader_Release(reader);
        return hr;
    }
    while ((hr = IXmlReader_Read(reader, &type)) == S_OK)
    {
        if (type == XmlNodeType_EndElement)
        {
            if (skip)
                --skip;
            else if (current)
                current = current->parent;
            continue;
        }
        if (type != XmlNodeType_Element)
            continue;
        empty = IXmlReader_IsEmptyElement(reader);
        if (skip)
        {
            skip += !empty;
            continue;
        }
        if (FAILED(hr = IXmlReader_GetLocalName(reader, &name, NULL))
                || FAILED(hr = IXmlReader_GetNamespaceUri(reader, &ns, NULL)))
            break;
        /* Elements Direct2D does not support are ignored with their content. */
        if (wcscmp(ns, svg_ns) || d2d_svg_lookup_tag(name) == SVG_TAG_UNSUPPORTED || (!current && wcscmp(name, L"svg")))
        {
            skip = !empty;
            continue;
        }
        if (!(element = d2d_svg_element_create(document, name, d2d_svg_lookup_tag(name))))
        {
            hr = E_OUTOFMEMORY;
            break;
        }
        for (hr = IXmlReader_MoveToFirstAttribute(reader); hr == S_OK; hr = IXmlReader_MoveToNextAttribute(reader))
        {
            const WCHAR *attr_ns, *prefix;
            WCHAR qualified[64];

            if (FAILED(IXmlReader_GetLocalName(reader, &name, NULL)) || FAILED(IXmlReader_GetValue(reader, &value, NULL))
                    || FAILED(IXmlReader_GetNamespaceUri(reader, &attr_ns, NULL))
                    || FAILED(IXmlReader_GetPrefix(reader, &prefix, NULL)))
                continue;
            if (!wcscmp(prefix, L"xmlns") || !wcscmp(name, L"xmlns"))
                continue;
            if (!wcscmp(attr_ns, xlink_ns))
            {
                _snwprintf(qualified, ARRAY_SIZE(qualified), L"xlink:%s", name);
                qualified[ARRAY_SIZE(qualified) - 1] = 0;
                name = qualified;
            }
            else if (*attr_ns)
                continue;
            if (!d2d_svg_element_add_attr(element, name, value))
            {
                hr = E_OUTOFMEMORY;
                break;
            }
        }
        if (FAILED(hr))
        {
            d2d_svg_element_destroy(element);
            break;
        }
        IXmlReader_MoveToElement(reader);
        d2d_svg_element_parse_props(element);
        if (!current)
            document->root = element;
        else if (!d2d_svg_element_add_child(current, element))
        {
            d2d_svg_element_destroy(element);
            hr = E_OUTOFMEMORY;
            break;
        }
        if (!empty)
            current = element;
    }
    IXmlReader_Release(reader);
    if (FAILED(hr))
        return hr;
    if (!document->root)
        return E_INVALIDARG;
    return d2d_svg_build_ids(document) ? S_OK : E_OUTOFMEMORY;
}

/* ID2D1SvgElement. Elements live as long as their document and hold references on it. */

static HRESULT STDMETHODCALLTYPE d2d_svg_element_QueryInterface(ID2D1SvgElement *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1SvgElement) || IsEqualGUID(iid, &IID_ID2D1Resource)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID2D1SvgElement_AddRef(iface);
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_svg_element_AddRef(ID2D1SvgElement *iface)
{
    struct d2d_svg_element *element = impl_from_ID2D1SvgElement(iface);

    return ID2D1SvgDocument_AddRef(&element->document->ID2D1SvgDocument_iface);
}

static ULONG STDMETHODCALLTYPE d2d_svg_element_Release(ID2D1SvgElement *iface)
{
    struct d2d_svg_element *element = impl_from_ID2D1SvgElement(iface);

    return ID2D1SvgDocument_Release(&element->document->ID2D1SvgDocument_iface);
}

static void STDMETHODCALLTYPE d2d_svg_element_GetFactory(ID2D1SvgElement *iface, ID2D1Factory **factory)
{
    struct d2d_svg_element *element = impl_from_ID2D1SvgElement(iface);

    ID2D1Factory_AddRef(*factory = element->document->factory);
}

static void STDMETHODCALLTYPE d2d_svg_element_GetDocument(ID2D1SvgElement *iface, ID2D1SvgDocument **document)
{
    struct d2d_svg_element *element = impl_from_ID2D1SvgElement(iface);

    ID2D1SvgDocument_AddRef(*document = &element->document->ID2D1SvgDocument_iface);
}

static HRESULT d2d_svg_copy_string(const WCHAR *src, WCHAR *dst, UINT32 count)
{
    size_t length = wcslen(src);

    if (!dst || count <= length)
        return E_INVALIDARG;
    memcpy(dst, src, (length + 1) * sizeof(*dst));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_element_GetTagName(ID2D1SvgElement *iface, WCHAR *name, UINT32 name_count)
{
    struct d2d_svg_element *element = impl_from_ID2D1SvgElement(iface);

    return d2d_svg_copy_string(element->tag_name, name, name_count);
}

static UINT32 STDMETHODCALLTYPE d2d_svg_element_GetTagNameLength(ID2D1SvgElement *iface)
{
    struct d2d_svg_element *element = impl_from_ID2D1SvgElement(iface);

    return wcslen(element->tag_name);
}

static BOOL STDMETHODCALLTYPE d2d_svg_element_IsTextContent(ID2D1SvgElement *iface)
{
    return FALSE;
}

static void d2d_svg_element_out(struct d2d_svg_element *element, ID2D1SvgElement **out)
{
    *out = element ? &element->ID2D1SvgElement_iface : NULL;
    if (*out)
        ID2D1SvgElement_AddRef(*out);
}

static void STDMETHODCALLTYPE d2d_svg_element_GetParent(ID2D1SvgElement *iface, ID2D1SvgElement **parent)
{
    d2d_svg_element_out(impl_from_ID2D1SvgElement(iface)->parent, parent);
}

static BOOL STDMETHODCALLTYPE d2d_svg_element_HasChildren(ID2D1SvgElement *iface)
{
    return impl_from_ID2D1SvgElement(iface)->child_count != 0;
}

static void STDMETHODCALLTYPE d2d_svg_element_GetFirstChild(ID2D1SvgElement *iface, ID2D1SvgElement **child)
{
    struct d2d_svg_element *element = impl_from_ID2D1SvgElement(iface);

    d2d_svg_element_out(element->child_count ? element->children[0] : NULL, child);
}

static void STDMETHODCALLTYPE d2d_svg_element_GetLastChild(ID2D1SvgElement *iface, ID2D1SvgElement **child)
{
    struct d2d_svg_element *element = impl_from_ID2D1SvgElement(iface);

    d2d_svg_element_out(element->child_count ? element->children[element->child_count - 1] : NULL, child);
}

static HRESULT d2d_svg_element_sibling(struct d2d_svg_element *element, ID2D1SvgElement *reference, int step,
        ID2D1SvgElement **out)
{
    size_t i;

    *out = NULL;
    for (i = 0; i < element->child_count; ++i)
    {
        if (&element->children[i]->ID2D1SvgElement_iface != reference)
            continue;
        if ((step < 0 && i) || (step > 0 && i + 1 < element->child_count))
            d2d_svg_element_out(element->children[i + step], out);
        return S_OK;
    }
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_element_GetPreviousChild(ID2D1SvgElement *iface,
        ID2D1SvgElement *reference_child, ID2D1SvgElement **previous_child)
{
    return d2d_svg_element_sibling(impl_from_ID2D1SvgElement(iface), reference_child, -1, previous_child);
}

static HRESULT STDMETHODCALLTYPE d2d_svg_element_GetNextChild(ID2D1SvgElement *iface,
        ID2D1SvgElement *reference_child, ID2D1SvgElement **next_child)
{
    return d2d_svg_element_sibling(impl_from_ID2D1SvgElement(iface), reference_child, 1, next_child);
}

static HRESULT STDMETHODCALLTYPE d2d_svg_element_InsertChildBefore(ID2D1SvgElement *iface, ID2D1SvgElement *new_child,
        ID2D1SvgElement *reference_child)
{
    FIXME("iface %p, new_child %p, reference_child %p stub!\n", iface, new_child, reference_child);
    /* TODO: editing the element tree */
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_element_AppendChild(ID2D1SvgElement *iface, ID2D1SvgElement *new_child)
{
    FIXME("iface %p, new_child %p stub!\n", iface, new_child);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_element_ReplaceChild(ID2D1SvgElement *iface, ID2D1SvgElement *new_child,
        ID2D1SvgElement *old_child)
{
    FIXME("iface %p, new_child %p, old_child %p stub!\n", iface, new_child, old_child);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_element_RemoveChild(ID2D1SvgElement *iface, ID2D1SvgElement *old_child)
{
    FIXME("iface %p, old_child %p stub!\n", iface, old_child);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_element_CreateChild(ID2D1SvgElement *iface, const WCHAR *tag_name,
        ID2D1SvgElement **new_child)
{
    FIXME("iface %p, tag_name %s, new_child %p stub!\n", iface, debugstr_w(tag_name), new_child);
    return E_NOTIMPL;
}

static BOOL STDMETHODCALLTYPE d2d_svg_element_IsAttributeSpecified(ID2D1SvgElement *iface, const WCHAR *name,
        BOOL *inherited)
{
    struct d2d_svg_element *element = impl_from_ID2D1SvgElement(iface);

    if (inherited)
        *inherited = FALSE;
    return name && d2d_svg_get_attr(element, name) != NULL;
}

static UINT32 STDMETHODCALLTYPE d2d_svg_element_GetSpecifiedAttributeCount(ID2D1SvgElement *iface)
{
    return impl_from_ID2D1SvgElement(iface)->attr_count;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_element_GetSpecifiedAttributeName(ID2D1SvgElement *iface, UINT32 index,
        WCHAR *name, UINT32 name_count, BOOL *inherited)
{
    struct d2d_svg_element *element = impl_from_ID2D1SvgElement(iface);

    if (inherited)
        *inherited = FALSE;
    if (index >= element->attr_count)
        return E_INVALIDARG;
    return d2d_svg_copy_string(element->attrs[index].name, name, name_count);
}

static HRESULT STDMETHODCALLTYPE d2d_svg_element_GetSpecifiedAttributeNameLength(ID2D1SvgElement *iface,
        UINT32 index, UINT32 *name_length, BOOL *inherited)
{
    struct d2d_svg_element *element = impl_from_ID2D1SvgElement(iface);

    if (inherited)
        *inherited = FALSE;
    if (index >= element->attr_count)
        return E_INVALIDARG;
    *name_length = wcslen(element->attrs[index].name);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_element_RemoveAttribute(ID2D1SvgElement *iface, const WCHAR *name)
{
    FIXME("iface %p, name %s stub!\n", iface, debugstr_w(name));
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_element_SetTextValue(ID2D1SvgElement *iface, const WCHAR *text,
        UINT32 text_count)
{
    FIXME("iface %p, text %s, text_count %u stub!\n", iface, debugstr_wn(text, text_count), text_count);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_element_GetTextValue(ID2D1SvgElement *iface, WCHAR *text, UINT32 text_count)
{
    FIXME("iface %p, text %p, text_count %u stub!\n", iface, text, text_count);
    return E_NOTIMPL;
}

static UINT32 STDMETHODCALLTYPE d2d_svg_element_GetTextValueLength(ID2D1SvgElement *iface)
{
    return 0;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_element_SetAttributeValueObject(ID2D1SvgElement *iface, const WCHAR *name,
        ID2D1SvgAttribute *value)
{
    FIXME("iface %p, name %s, value %p stub!\n", iface, debugstr_w(name), value);
    /* TODO: typed attribute values */
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_element_SetAttributeValuePod(ID2D1SvgElement *iface, const WCHAR *name,
        D2D1_SVG_ATTRIBUTE_POD_TYPE type, const void *value, UINT32 value_size)
{
    FIXME("iface %p, name %s, type %u, value %p, value_size %u stub!\n", iface, debugstr_w(name), type, value,
            value_size);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_element_SetAttributeValueString(ID2D1SvgElement *iface, const WCHAR *name,
        D2D1_SVG_ATTRIBUTE_STRING_TYPE type, const WCHAR *value)
{
    FIXME("iface %p, name %s, type %u, value %s stub!\n", iface, debugstr_w(name), type, debugstr_w(value));
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_element_GetAttributeValueObject(ID2D1SvgElement *iface, const WCHAR *name,
        REFIID iid, void **value)
{
    FIXME("iface %p, name %s, iid %s, value %p stub!\n", iface, debugstr_w(name), debugstr_guid(iid), value);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_element_GetAttributeValuePod(ID2D1SvgElement *iface, const WCHAR *name,
        D2D1_SVG_ATTRIBUTE_POD_TYPE type, void *value, UINT32 value_size)
{
    FIXME("iface %p, name %s, type %u, value %p, value_size %u stub!\n", iface, debugstr_w(name), type, value,
            value_size);
    return E_NOTIMPL;
}

/* The specified string of an attribute; values come back as written. */
static HRESULT STDMETHODCALLTYPE d2d_svg_element_GetAttributeValueString(ID2D1SvgElement *iface, const WCHAR *name,
        D2D1_SVG_ATTRIBUTE_STRING_TYPE type, WCHAR *value, UINT32 value_count)
{
    struct d2d_svg_element *element = impl_from_ID2D1SvgElement(iface);
    const WCHAR *attr = name ? d2d_svg_get_attr(element, name) : NULL;

    TRACE("iface %p, name %s, type %u, value %p, value_count %u.\n", iface, debugstr_w(name), type, value,
            value_count);

    if (!attr)
        return E_INVALIDARG;
    /* TODO: normalized string forms of typed attributes */
    return d2d_svg_copy_string(attr, value, value_count);
}

static HRESULT STDMETHODCALLTYPE d2d_svg_element_GetAttributeValueLength(ID2D1SvgElement *iface, const WCHAR *name,
        D2D1_SVG_ATTRIBUTE_STRING_TYPE type, UINT32 *value_length)
{
    struct d2d_svg_element *element = impl_from_ID2D1SvgElement(iface);
    const WCHAR *attr = name ? d2d_svg_get_attr(element, name) : NULL;

    if (!attr)
        return E_INVALIDARG;
    *value_length = wcslen(attr);
    return S_OK;
}

static const struct ID2D1SvgElementVtbl d2d_svg_element_vtbl =
{
    d2d_svg_element_QueryInterface,
    d2d_svg_element_AddRef,
    d2d_svg_element_Release,
    d2d_svg_element_GetFactory,
    d2d_svg_element_GetDocument,
    d2d_svg_element_GetTagName,
    d2d_svg_element_GetTagNameLength,
    d2d_svg_element_IsTextContent,
    d2d_svg_element_GetParent,
    d2d_svg_element_HasChildren,
    d2d_svg_element_GetFirstChild,
    d2d_svg_element_GetLastChild,
    d2d_svg_element_GetPreviousChild,
    d2d_svg_element_GetNextChild,
    d2d_svg_element_InsertChildBefore,
    d2d_svg_element_AppendChild,
    d2d_svg_element_ReplaceChild,
    d2d_svg_element_RemoveChild,
    d2d_svg_element_CreateChild,
    d2d_svg_element_IsAttributeSpecified,
    d2d_svg_element_GetSpecifiedAttributeCount,
    d2d_svg_element_GetSpecifiedAttributeName,
    d2d_svg_element_GetSpecifiedAttributeNameLength,
    d2d_svg_element_RemoveAttribute,
    d2d_svg_element_SetTextValue,
    d2d_svg_element_GetTextValue,
    d2d_svg_element_GetTextValueLength,
    d2d_svg_element_SetAttributeValueObject,
    d2d_svg_element_SetAttributeValuePod,
    d2d_svg_element_SetAttributeValueString,
    d2d_svg_element_GetAttributeValueObject,
    d2d_svg_element_GetAttributeValuePod,
    d2d_svg_element_GetAttributeValueString,
    d2d_svg_element_GetAttributeValueLength,
};

/* ID2D1SvgDocument. */

static HRESULT STDMETHODCALLTYPE d2d_svg_document_QueryInterface(ID2D1SvgDocument *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1SvgDocument) || IsEqualGUID(iid, &IID_ID2D1Resource)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID2D1SvgDocument_AddRef(iface);
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_svg_document_AddRef(ID2D1SvgDocument *iface)
{
    struct d2d_svg_document *document = impl_from_ID2D1SvgDocument(iface);
    ULONG refcount = InterlockedIncrement(&document->refcount);

    TRACE("%p increasing refcount to %lu.\n", iface, refcount);
    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_svg_document_Release(ID2D1SvgDocument *iface)
{
    struct d2d_svg_document *document = impl_from_ID2D1SvgDocument(iface);
    ULONG refcount = InterlockedDecrement(&document->refcount);

    TRACE("%p decreasing refcount to %lu.\n", iface, refcount);
    if (!refcount)
    {
        if (document->root)
            d2d_svg_element_destroy(document->root);
        free(document->ids);
        ID2D1Factory_Release(document->factory);
        free(document);
    }
    return refcount;
}

static void STDMETHODCALLTYPE d2d_svg_document_GetFactory(ID2D1SvgDocument *iface, ID2D1Factory **factory)
{
    struct d2d_svg_document *document = impl_from_ID2D1SvgDocument(iface);

    ID2D1Factory_AddRef(*factory = document->factory);
}

static HRESULT STDMETHODCALLTYPE d2d_svg_document_SetViewportSize(ID2D1SvgDocument *iface, D2D1_SIZE_F viewport_size)
{
    struct d2d_svg_document *document = impl_from_ID2D1SvgDocument(iface);

    TRACE("iface %p, viewport_size {%.8e, %.8e}.\n", iface, viewport_size.width, viewport_size.height);

    if (viewport_size.width < 0.0f || viewport_size.height < 0.0f)
        return E_INVALIDARG;
    document->viewport = viewport_size;
    return S_OK;
}

static D2D1_SIZE_F * STDMETHODCALLTYPE d2d_svg_document_GetViewportSize(ID2D1SvgDocument *iface, D2D1_SIZE_F *size)
{
    *size = impl_from_ID2D1SvgDocument(iface)->viewport;
    return size;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_document_SetRoot(ID2D1SvgDocument *iface, ID2D1SvgElement *root)
{
    FIXME("iface %p, root %p stub!\n", iface, root);
    /* TODO: replacing the root element */
    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d2d_svg_document_GetRoot(ID2D1SvgDocument *iface, ID2D1SvgElement **root)
{
    d2d_svg_element_out(impl_from_ID2D1SvgDocument(iface)->root, root);
}

static HRESULT STDMETHODCALLTYPE d2d_svg_document_FindElementById(ID2D1SvgDocument *iface, const WCHAR *id,
        ID2D1SvgElement **element)
{
    struct d2d_svg_document *document = impl_from_ID2D1SvgDocument(iface);

    TRACE("iface %p, id %s, element %p.\n", iface, debugstr_w(id), element);

    if (!id || !element)
        return E_INVALIDARG;
    d2d_svg_element_out(d2d_svg_find_id(document, id, wcslen(id)), element);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_document_Serialize(ID2D1SvgDocument *iface, IStream *stream,
        ID2D1SvgElement *subtree)
{
    FIXME("iface %p, stream %p, subtree %p stub!\n", iface, stream, subtree);
    /* TODO: serializing a document */
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_document_Deserialize(ID2D1SvgDocument *iface, IStream *stream,
        ID2D1SvgElement **subtree)
{
    FIXME("iface %p, stream %p, subtree %p stub!\n", iface, stream, subtree);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_document_CreatePaint(ID2D1SvgDocument *iface, D2D1_SVG_PAINT_TYPE type,
        const D2D1_COLOR_F *color, const WCHAR *id, ID2D1SvgPaint **paint)
{
    FIXME("iface %p, type %u, color %p, id %s, paint %p stub!\n", iface, type, color, debugstr_w(id), paint);
    /* TODO: attribute objects */
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_document_CreateStrokeDashArray(ID2D1SvgDocument *iface,
        const D2D1_SVG_LENGTH *dashes, UINT32 dashes_count, ID2D1SvgStrokeDashArray **array)
{
    FIXME("iface %p, dashes %p, dashes_count %u, array %p stub!\n", iface, dashes, dashes_count, array);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_document_CreatePointCollection(ID2D1SvgDocument *iface,
        const D2D1_POINT_2F *points, UINT32 points_count, ID2D1SvgPointCollection **collection)
{
    FIXME("iface %p, points %p, points_count %u, collection %p stub!\n", iface, points, points_count, collection);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_document_CreatePathData(ID2D1SvgDocument *iface, const float *data,
        UINT32 data_count, const D2D1_SVG_PATH_COMMAND *commands, UINT32 commands_count, ID2D1SvgPathData **path)
{
    FIXME("iface %p, data %p, data_count %u, commands %p, commands_count %u, path %p stub!\n", iface, data,
            data_count, commands, commands_count, path);
    return E_NOTIMPL;
}

static const struct ID2D1SvgDocumentVtbl d2d_svg_document_vtbl =
{
    d2d_svg_document_QueryInterface,
    d2d_svg_document_AddRef,
    d2d_svg_document_Release,
    d2d_svg_document_GetFactory,
    d2d_svg_document_SetViewportSize,
    d2d_svg_document_GetViewportSize,
    d2d_svg_document_SetRoot,
    d2d_svg_document_GetRoot,
    d2d_svg_document_FindElementById,
    d2d_svg_document_Serialize,
    d2d_svg_document_Deserialize,
    d2d_svg_document_CreatePaint,
    d2d_svg_document_CreateStrokeDashArray,
    d2d_svg_document_CreatePointCollection,
    d2d_svg_document_CreatePathData,
};

HRESULT d2d_svg_document_create(ID2D1Factory *factory, IStream *stream, D2D1_SIZE_F viewport,
        ID2D1SvgDocument **out)
{
    struct d2d_svg_document *document;
    HRESULT hr;

    *out = NULL;
    if (viewport.width < 0.0f || viewport.height < 0.0f)
        return E_INVALIDARG;
    if (!(document = calloc(1, sizeof(*document))))
        return E_OUTOFMEMORY;
    document->ID2D1SvgDocument_iface.lpVtbl = &d2d_svg_document_vtbl;
    document->refcount = 1;
    ID2D1Factory_AddRef(document->factory = factory);
    document->viewport = viewport;
    /* Without a stream the document is empty: a root svg element with nothing in it. */
    if (!stream)
        hr = (document->root = d2d_svg_element_create(document, L"svg", SVG_TAG_SVG)) ? S_OK : E_OUTOFMEMORY;
    else
        hr = d2d_svg_parse(document, stream);
    if (FAILED(hr))
    {
        WARN("Failed to parse the document, hr %#lx.\n", hr);
        ID2D1SvgDocument_Release(&document->ID2D1SvgDocument_iface);
        return hr;
    }
    *out = &document->ID2D1SvgDocument_iface;
    return S_OK;
}
