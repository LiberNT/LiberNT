/*
 * PROJECT:     LiberNT MSHTML
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Phrase, span, block, paragraph, heading, div, BR, HR, font, basefont and base elements
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <stdarg.h>
#include <stdio.h>
#include <math.h>

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "ole2.h"
#include "mshtmdid.h"

#include "mshtml_private.h"
#include "htmlevent.h"
#include "binding.h"

HRESULT elem_get_attr_string(HTMLElement *elem, const WCHAR *name, BSTR *p)
{
    *p = NULL;
    if(!elem->dom_element || !element_has_attribute(elem, name))
        return S_OK;
    return elem_string_attr_getter(elem, name, FALSE, p);
}

HRESULT elem_put_bool_attr(HTMLElement *elem, const WCHAR *name, VARIANT_BOOL v)
{
    if(v)
        return elem_put_reflected_string(elem, name, (BSTR)L"");
    return element_remove_attribute(elem, name);
}

HRESULT elem_get_bool_attr(HTMLElement *elem, const WCHAR *name, VARIANT_BOOL *p)
{
    *p = element_has_attribute(elem, name);
    return S_OK;
}

static HRESULT get_url_attr(HTMLElement *elem, const WCHAR *name, BSTR *p)
{
    HTMLInnerWindow *window = elem->node.doc->window;
    HRESULT hres;
    BSTR val;
    IUri *uri;

    hres = elem_get_attr_string(elem, name, &val);
    if(SUCCEEDED(hres) && !val)
        return (*p = SysAllocString(L"")) ? S_OK : E_OUTOFMEMORY;
    if(FAILED(hres) || !window || !window->base.outer_window) {
        *p = val;
        return hres;
    }

    hres = create_relative_uri(window->base.outer_window, val, &uri);
    if(FAILED(hres)) {
        *p = val;
        return S_OK;
    }

    SysFreeString(val);
    hres = IUri_GetDisplayUri(uri, p);
    IUri_Release(uri);
    return hres;
}

static const WCHAR *find_keyword(const WCHAR *v, const WCHAR *const *keywords)
{
    for(; *keywords; keywords++) {
        if(!wcsicmp(v, *keywords))
            return *keywords;
    }
    return NULL;
}

static HRESULT put_keyword_attr(HTMLElement *elem, const WCHAR *name, BSTR v, const WCHAR *const *keywords)
{
    const WCHAR *keyword;

    if(!v || !*v)
        return element_remove_attribute(elem, name);
    if(!(keyword = find_keyword(v, keywords)))
        return E_INVALIDARG;
    return elem_put_reflected_string(elem, name, (BSTR)keyword);
}

static HRESULT get_keyword_attr(HTMLElement *elem, const WCHAR *name, BSTR *p, const WCHAR *const *keywords)
{
    const WCHAR *keyword;
    HRESULT hres;
    BSTR val;

    hres = elem_get_attr_string(elem, name, &val);
    if(FAILED(hres))
        return hres;

    keyword = val ? find_keyword(val, keywords) : NULL;
    SysFreeString(val);
    if(!keyword) {
        *p = NULL;
        return S_OK;
    }
    return (*p = SysAllocString(keyword)) ? S_OK : E_OUTOFMEMORY;
}

static const WCHAR *const clear_keywords[] = {L"none", L"left", L"right", L"all", L"both", NULL};
static const WCHAR *const align_keywords[] = {L"left", L"center", L"right", L"justify", NULL};

static const struct {
    const WCHAR *name;
    DWORD rgb;
} named_colors[] = {
    {L"aliceblue", 0xf0f8ff}, {L"antiquewhite", 0xfaebd7}, {L"aqua", 0x00ffff}, {L"aquamarine", 0x7fffd4},
    {L"azure", 0xf0ffff}, {L"beige", 0xf5f5dc}, {L"bisque", 0xffe4c4}, {L"black", 0x000000},
    {L"blanchedalmond", 0xffebcd}, {L"blue", 0x0000ff}, {L"blueviolet", 0x8a2be2}, {L"brown", 0xa52a2a},
    {L"burlywood", 0xdeb887}, {L"cadetblue", 0x5f9ea0}, {L"chartreuse", 0x7fff00}, {L"chocolate", 0xd2691e},
    {L"coral", 0xff7f50}, {L"cornflowerblue", 0x6495ed}, {L"cornsilk", 0xfff8dc}, {L"crimson", 0xdc143c},
    {L"cyan", 0x00ffff}, {L"darkblue", 0x00008b}, {L"darkcyan", 0x008b8b}, {L"darkgoldenrod", 0xb8860b},
    {L"darkgray", 0xa9a9a9}, {L"darkgreen", 0x006400}, {L"darkgrey", 0xa9a9a9}, {L"darkkhaki", 0xbdb76b},
    {L"darkmagenta", 0x8b008b}, {L"darkolivegreen", 0x556b2f}, {L"darkorange", 0xff8c00}, {L"darkorchid", 0x9932cc},
    {L"darkred", 0x8b0000}, {L"darksalmon", 0xe9967a}, {L"darkseagreen", 0x8fbc8f}, {L"darkslateblue", 0x483d8b},
    {L"darkslategray", 0x2f4f4f}, {L"darkslategrey", 0x2f4f4f}, {L"darkturquoise", 0x00ced1}, {L"darkviolet", 0x9400d3},
    {L"deeppink", 0xff1493}, {L"deepskyblue", 0x00bfff}, {L"dimgray", 0x696969}, {L"dimgrey", 0x696969},
    {L"dodgerblue", 0x1e90ff}, {L"firebrick", 0xb22222}, {L"floralwhite", 0xfffaf0}, {L"forestgreen", 0x228b22},
    {L"fuchsia", 0xff00ff}, {L"gainsboro", 0xdcdcdc}, {L"ghostwhite", 0xf8f8ff}, {L"gold", 0xffd700},
    {L"goldenrod", 0xdaa520}, {L"gray", 0x808080}, {L"green", 0x008000}, {L"greenyellow", 0xadff2f},
    {L"grey", 0x808080}, {L"honeydew", 0xf0fff0}, {L"hotpink", 0xff69b4}, {L"indianred", 0xcd5c5c},
    {L"indigo", 0x4b0082}, {L"ivory", 0xfffff0}, {L"khaki", 0xf0e68c}, {L"lavender", 0xe6e6fa},
    {L"lavenderblush", 0xfff0f5}, {L"lawngreen", 0x7cfc00}, {L"lemonchiffon", 0xfffacd}, {L"lightblue", 0xadd8e6},
    {L"lightcoral", 0xf08080}, {L"lightcyan", 0xe0ffff}, {L"lightgoldenrodyellow", 0xfafad2}, {L"lightgray", 0xd3d3d3},
    {L"lightgreen", 0x90ee90}, {L"lightgrey", 0xd3d3d3}, {L"lightpink", 0xffb6c1}, {L"lightsalmon", 0xffa07a},
    {L"lightseagreen", 0x20b2aa}, {L"lightskyblue", 0x87cefa}, {L"lightslategray", 0x778899}, {L"lightslategrey", 0x778899},
    {L"lightsteelblue", 0xb0c4de}, {L"lightyellow", 0xffffe0}, {L"lime", 0x00ff00}, {L"limegreen", 0x32cd32},
    {L"linen", 0xfaf0e6}, {L"magenta", 0xff00ff}, {L"maroon", 0x800000}, {L"mediumaquamarine", 0x66cdaa},
    {L"mediumblue", 0x0000cd}, {L"mediumorchid", 0xba55d3}, {L"mediumpurple", 0x9370db}, {L"mediumseagreen", 0x3cb371},
    {L"mediumslateblue", 0x7b68ee}, {L"mediumspringgreen", 0x00fa9a}, {L"mediumturquoise", 0x48d1cc}, {L"mediumvioletred", 0xc71585},
    {L"midnightblue", 0x191970}, {L"mintcream", 0xf5fffa}, {L"mistyrose", 0xffe4e1}, {L"moccasin", 0xffe4b5},
    {L"navajowhite", 0xffdead}, {L"navy", 0x000080}, {L"oldlace", 0xfdf5e6}, {L"olive", 0x808000},
    {L"olivedrab", 0x6b8e23}, {L"orange", 0xffa500}, {L"orangered", 0xff4500}, {L"orchid", 0xda70d6},
    {L"palegoldenrod", 0xeee8aa}, {L"palegreen", 0x98fb98}, {L"paleturquoise", 0xafeeee}, {L"palevioletred", 0xdb7093},
    {L"papayawhip", 0xffefd5}, {L"peachpuff", 0xffdab9}, {L"peru", 0xcd853f}, {L"pink", 0xffc0cb},
    {L"plum", 0xdda0dd}, {L"powderblue", 0xb0e0e6}, {L"purple", 0x800080}, {L"rebeccapurple", 0x663399},
    {L"red", 0xff0000}, {L"rosybrown", 0xbc8f8f}, {L"royalblue", 0x4169e1}, {L"saddlebrown", 0x8b4513},
    {L"salmon", 0xfa8072}, {L"sandybrown", 0xf4a460}, {L"seagreen", 0x2e8b57}, {L"seashell", 0xfff5ee},
    {L"sienna", 0xa0522d}, {L"silver", 0xc0c0c0}, {L"skyblue", 0x87ceeb}, {L"slateblue", 0x6a5acd},
    {L"slategray", 0x708090}, {L"slategrey", 0x708090}, {L"snow", 0xfffafa}, {L"springgreen", 0x00ff7f},
    {L"steelblue", 0x4682b4}, {L"tan", 0xd2b48c}, {L"teal", 0x008080}, {L"thistle", 0xd8bfd8},
    {L"tomato", 0xff6347}, {L"turquoise", 0x40e0d0}, {L"violet", 0xee82ee}, {L"wheat", 0xf5deb3},
    {L"white", 0xffffff}, {L"whitesmoke", 0xf5f5f5}, {L"yellow", 0xffff00}, {L"yellowgreen", 0x9acd32},
};

static const struct {
    const WCHAR *name;
    int index;
} system_colors[] = {
    {L"activeborder", COLOR_ACTIVEBORDER}, {L"activecaption", COLOR_ACTIVECAPTION},
    {L"appworkspace", COLOR_APPWORKSPACE}, {L"background", COLOR_BACKGROUND},
    {L"buttonface", COLOR_BTNFACE}, {L"buttonhighlight", COLOR_BTNHIGHLIGHT},
    {L"buttonshadow", COLOR_BTNSHADOW}, {L"buttontext", COLOR_BTNTEXT},
    {L"captiontext", COLOR_CAPTIONTEXT}, {L"graytext", COLOR_GRAYTEXT},
    {L"highlight", COLOR_HIGHLIGHT}, {L"highlighttext", COLOR_HIGHLIGHTTEXT},
    {L"inactiveborder", COLOR_INACTIVEBORDER}, {L"inactivecaption", COLOR_INACTIVECAPTION},
    {L"inactivecaptiontext", COLOR_INACTIVECAPTIONTEXT}, {L"infobackground", COLOR_INFOBK},
    {L"infotext", COLOR_INFOTEXT}, {L"menu", COLOR_MENU},
    {L"menutext", COLOR_MENUTEXT}, {L"scrollbar", COLOR_SCROLLBAR},
    {L"threeddarkshadow", COLOR_3DDKSHADOW}, {L"threedface", COLOR_3DFACE},
    {L"threedhighlight", COLOR_3DHIGHLIGHT}, {L"threedlightshadow", COLOR_3DLIGHT},
    {L"threedshadow", COLOR_3DSHADOW}, {L"window", COLOR_WINDOW},
    {L"windowframe", COLOR_WINDOWFRAME}, {L"windowtext", COLOR_WINDOWTEXT},
};

static int hex_value(WCHAR c)
{
    if(is_digit(c))
        return c - '0';
    if(c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if(c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return 0;
}

static const WCHAR *find_color_name(const WCHAR *v, DWORD *rgb)
{
    unsigned i;

    for(i = 0; i < ARRAY_SIZE(named_colors); i++) {
        if(!wcsicmp(v, named_colors[i].name)) {
            *rgb = named_colors[i].rgb;
            return named_colors[i].name;
        }
    }
    for(i = 0; i < ARRAY_SIZE(system_colors); i++) {
        if(!wcsicmp(v, system_colors[i].name)) {
            COLORREF c = GetSysColor(system_colors[i].index);
            *rgb = (GetRValue(c) << 16) | (GetGValue(c) << 8) | GetBValue(c);
            return system_colors[i].name;
        }
    }
    return NULL;
}

static BOOL parse_rgb_component(const WCHAR **ptr, unsigned *ret)
{
    const WCHAR *v = *ptr;
    double val = 0, scale = 1;
    BOOL neg = FALSE;

    if(*v == '-' || *v == '+')
        neg = *v++ == '-';
    if(!is_digit(*v))
        return FALSE;
    for(; is_digit(*v); v++)
        val = min(val * 10 + (*v - '0'), 1e6);
    if(*v == '.') {
        for(v++; is_digit(*v); v++)
            val += (*v - '0') * (scale /= 10);
    }
    if(*v == '%') {
        val = val * 255 / 100;
        v++;
    }
    *ret = neg ? 0 : (unsigned)min(val + 0.5, 255);
    *ptr = v;
    return TRUE;
}

static BOOL parse_rgb_function(const WCHAR *v, DWORD *rgb)
{
    unsigned i, comp;

    if(wcsnicmp(v, L"rgb(", 4))
        return FALSE;
    v += 4;
    *rgb = 0;
    for(i = 0; i < 3; i++) {
        while(*v == ' ')
            v++;
        if(!parse_rgb_component(&v, &comp))
            return FALSE;
        while(*v == ' ')
            v++;
        if(*v++ != (i < 2 ? ',' : ')'))
            return FALSE;
        *rgb = (*rgb << 8) | comp;
    }
    return !*v;
}

static DWORD legacy_color_value(const WCHAR *v)
{
    unsigned len, comp, start = 0, i, j;
    DWORD rgb = 0;

    if(*v == '#')
        v++;
    len = lstrlenW(v);
    comp = (len + 2) / 3;
    if(comp > 8) {
        start = comp - 8;
        comp = 8;
    }
    while(comp > 2) {
        for(i = 0; i < 3; i++) {
            j = i * (start + comp) + start;
            if(j < len && hex_value(v[j]))
                break;
        }
        if(i < 3)
            break;
        start++;
        comp--;
    }
    for(i = 0; i < 3; i++) {
        unsigned base = i * (start + comp) + start, part = 0;

        for(j = 0; j < min(comp, 2); j++)
            part = part * 16 + (base + j < len ? hex_value(v[base + j]) : 0);
        rgb = (rgb << 8) | part;
    }
    return rgb;
}

static HRESULT normalize_color(const WCHAR *v, BSTR *ret)
{
    WCHAR buf[8];
    DWORD rgb;

    if(!wcsnicmp(v, L"transparent", 11))
        return (*ret = SysAllocString(L"transparent")) ? S_OK : E_OUTOFMEMORY;
    if(!find_color_name(v, &rgb) && !parse_rgb_function(v, &rgb))
        rgb = legacy_color_value(v);
    swprintf(buf, ARRAY_SIZE(buf), L"#%06x", rgb);
    return (*ret = SysAllocString(buf)) ? S_OK : E_OUTOFMEMORY;
}

HRESULT nscolor_to_str(LPCWSTR color, BSTR *ret)
{
    if(!color || !*color) {
        *ret = NULL;
        return S_OK;
    }
    return normalize_color(color, ret);
}

static HRESULT canonical_color(const WCHAR *v, BOOL exact_transparent, BSTR *ret)
{
    const WCHAR *name, *hex = *v == '#' ? v + 1 : v;
    unsigned len, i;
    WCHAR buf[24];
    DWORD rgb;

    if(exact_transparent ? !wcsicmp(v, L"transparent") : !wcsnicmp(v, L"transparent", 11))
        name = L"transparent";
    else
        name = find_color_name(v, &rgb);
    if(name)
        return (*ret = SysAllocString(name)) ? S_OK : E_OUTOFMEMORY;
    len = lstrlenW(hex);
    if(parse_rgb_function(v, &rgb))
        swprintf(buf, ARRAY_SIZE(buf), L"rgb(%u,%u,%u)", rgb >> 16, (rgb >> 8) & 0xff, rgb & 0xff);
    else if(!len || len > 6)
        swprintf(buf, ARRAY_SIZE(buf), L"#%06x", legacy_color_value(v));
    else {
        /* FIXME: each non-ASCII character counts as one digit; Windows 11 counts some of them as several */
        buf[0] = '#';
        for(i = 0; i < len; i++)
            buf[i + 1] = L"0123456789abcdef"[hex_value(hex[i])];
        buf[len + 1] = 0;
    }
    return (*ret = SysAllocString(buf)) ? S_OK : E_OUTOFMEMORY;
}

static BOOL parse_css_number(const WCHAR **ptr, double *ret)
{
    const WCHAR *v = *ptr;
    double val = 0, scale = 1;
    BOOL neg = FALSE, digits = FALSE;

    if(*v == '-' || *v == '+')
        neg = *v++ == '-';
    for(; is_digit(*v); v++, digits = TRUE)
        val = min(val * 10 + (*v - '0'), 1e6);
    if(*v == '.') {
        for(v++; is_digit(*v); v++, digits = TRUE)
            val += (*v - '0') * (scale /= 10);
    }
    if(!digits)
        return FALSE;
    *ret = neg ? -val : val;
    *ptr = v;
    return TRUE;
}

static BOOL parse_hsl_component(const WCHAR **ptr, unsigned idx, unsigned *ret)
{
    const WCHAR *v = *ptr;
    double val;
    int i;

    if(!parse_css_number(&v, &val) || (*v == '%') != (idx > 0))
        return FALSE;
    if(idx)
        v++;
    i = (int)val;
    *ret = idx ? (unsigned)max(0, min(i, 100)) : (unsigned)((i % 360 + 360) % 360);
    *ptr = v;
    return TRUE;
}

static HRESULT css_color_function(const WCHAR *v, BSTR *ret)
{
    unsigned comp[3], count, i;
    WCHAR buf[64], alpha[32];
    BOOL hsl = TRUE;
    double a;

    if(!wcsnicmp(v, L"rgba(", 5) || !wcsnicmp(v, L"hsla(", 5)) {
        hsl = towlower(*v) == 'h';
        count = 4;
        v += 5;
    }else if(!wcsnicmp(v, L"hsl(", 4)) {
        count = 3;
        v += 4;
    }else {
        return S_FALSE;
    }
    for(i = 0; i < count; i++) {
        while(*v == ' ')
            v++;
        if(i == 3) {
            if(!parse_css_number(&v, &a))
                return S_FALSE;
            swprintf(alpha, ARRAY_SIZE(alpha), L"%g", max(0.0, min(a, 1.0)));
        }else if(hsl ? !parse_hsl_component(&v, i, &comp[i]) : !parse_rgb_component(&v, &comp[i])) {
            return S_FALSE;
        }
        while(*v == ' ')
            v++;
        if(*v++ != (i < count - 1 ? ',' : ')'))
            return S_FALSE;
    }
    if(*v)
        return S_FALSE;
    if(!hsl)
        swprintf(buf, ARRAY_SIZE(buf), L"rgba(%u,%u,%u,%s)", comp[0], comp[1], comp[2], alpha);
    else if(count == 4)
        swprintf(buf, ARRAY_SIZE(buf), L"hsla(%u, %u%%, %u%%, %s)", comp[0], comp[1], comp[2], alpha);
    else
        swprintf(buf, ARRAY_SIZE(buf), L"hsl(%u,%u%%,%u%%)", comp[0], comp[1], comp[2]);
    return (*ret = SysAllocString(buf)) ? S_OK : E_OUTOFMEMORY;
}

static BOOL is_color_space(WCHAR c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

static HRESULT ie9_color_value(const WCHAR *v, compat_mode_t mode, BSTR *ret)
{
    const WCHAR *end;
    HRESULT hres;
    BSTR str;

    while(is_color_space(*v))
        v++;
    for(end = v + lstrlenW(v); end > v && is_color_space(end[-1]); end--);
    if(!(str = SysAllocStringLen(v, end - v)))
        return E_OUTOFMEMORY;
    if(!wcsicmp(str, L"currentcolor"))
        hres = (*ret = SysAllocString(L"currentColor")) ? S_OK : E_OUTOFMEMORY;
    else if((hres = css_color_function(str, ret)) == S_FALSE)
        hres = canonical_color(str, mode >= COMPAT_MODE_IE10, ret);
    SysFreeString(str);
    return hres;
}

HRESULT elem_put_color_attr(HTMLElement *elem, const WCHAR *name, VARIANT *v)
{
    WCHAR buf[8];
    HRESULT hres;
    BSTR str;

    switch(V_VT(v)) {
    case VT_BSTR:
        if(!V_BSTR(v) || !*V_BSTR(v))
            return elem_put_reflected_string(elem, name, NULL);
        hres = canonical_color(V_BSTR(v), FALSE, &str);
        if(FAILED(hres))
            return hres;
        hres = elem_put_reflected_string(elem, name, str);
        SysFreeString(str);
        return hres;
    case VT_I4:
        if(V_I4(v) < 0 || V_I4(v) > 0xffffff)
            return S_OK;
        swprintf(buf, ARRAY_SIZE(buf), L"#%06x", V_I4(v));
        return elem_put_reflected_string(elem, name, buf);
    case VT_R8:
        if(!(V_R8(v) > -1.0 && V_R8(v) < 16777216.0))
            return S_OK;
        swprintf(buf, ARRAY_SIZE(buf), L"#%06x", (LONG)V_R8(v));
        return elem_put_reflected_string(elem, name, buf);
    default:
        return S_OK;
    }
}

HRESULT elem_get_color_attr(HTMLElement *elem, const WCHAR *name, VARIANT *p)
{
    compat_mode_t mode;
    HRESULT hres;
    BSTR val;

    V_VT(p) = VT_BSTR;
    V_BSTR(p) = NULL;
    hres = elem_get_attr_string(elem, name, &val);
    if(FAILED(hres) || !val)
        return hres;
    mode = dispex_compat_mode(&elem->node.event_target.dispex);
    if(!*val)
        hres = S_OK;
    else if(mode >= COMPAT_MODE_IE9)
        hres = ie9_color_value(val, mode, &V_BSTR(p));
    else
        hres = normalize_color(val, &V_BSTR(p));
    SysFreeString(val);
    return hres;
}

static BOOL normalize_font_size(const WCHAR *v, WCHAR *buf, size_t size)
{
    WCHAR sign = 0;
    UINT64 val = 0;

    if(!*v) {
        swprintf(buf, size, L"3");
        return TRUE;
    }
    while(*v == ' ' || *v == '\t' || *v == '\n' || *v == '\r' || *v == '\f')
        v++;
    if(*v == '+' || *v == '-')
        sign = *v++;
    if(!is_digit(*v))
        return FALSE;
    for(; is_digit(*v); v++)
        val = min(val * 10 + (*v - '0'), sign == '-' ? 0x80000000 : 0x7fffffff);
    if(sign)
        swprintf(buf, size, L"%c%u", sign, (unsigned)val);
    else
        swprintf(buf, size, L"%u", (unsigned)val);
    return TRUE;
}

static HRESULT put_font_size_attr(HTMLElement *elem, VARIANT *v)
{
    WCHAR buf[16];
    VARIANT str;
    HRESULT hres;

    if(V_VT(v) == VT_NULL)
        return element_remove_attribute(elem, L"size");
    if(V_VT(v) == VT_BOOL)
        return E_INVALIDARG;

    V_VT(&str) = VT_EMPTY;
    if(V_VT(v) != VT_BSTR) {
        hres = VariantChangeType(&str, v, 0, VT_BSTR);
        if(FAILED(hres))
            return E_INVALIDARG;
        v = &str;
    }
    hres = normalize_font_size(V_BSTR(v) ? V_BSTR(v) : L"", buf, ARRAY_SIZE(buf))
        ? elem_put_reflected_string(elem, L"size", buf) : E_INVALIDARG;
    VariantClear(&str);
    return hres;
}

static HRESULT get_font_size_attr(HTMLElement *elem, VARIANT *p)
{
    WCHAR buf[16];
    HRESULT hres;
    BSTR val;

    V_VT(p) = VT_BSTR;
    V_BSTR(p) = NULL;
    hres = elem_get_attr_string(elem, L"size", &val);
    if(FAILED(hres) || !val)
        return hres;
    if(normalize_font_size(val, buf, ARRAY_SIZE(buf)) && !(V_BSTR(p) = SysAllocString(buf)))
        hres = E_OUTOFMEMORY;
    SysFreeString(val);
    return hres;
}

static HRESULT get_clear_attr(HTMLElement *elem, const WCHAR *name, BSTR *p)
{
    return get_keyword_attr(elem, name, p, clear_keywords);
}

static HRESULT get_align_attr(HTMLElement *elem, const WCHAR *name, BSTR *p)
{
    return get_keyword_attr(elem, name, p, align_keywords);
}

static inline HTMLElement *elem_from_dispex(DispatchEx *dispex)
{
    return CONTAINING_RECORD(dispex, HTMLElement, node.event_target.dispex);
}

static HRESULT ie9_attr_get(DispatchEx *dispex, WORD flags, DISPPARAMS *dp, VARIANT *res,
        HRESULT (*getter)(HTMLElement*,const WCHAR*,BSTR*), const WCHAR *name)
{
    HRESULT hres;
    BSTR str;

    if(!(flags & DISPATCH_PROPERTYGET) || (dp && dp->cArgs))
        return S_FALSE;

    hres = getter(elem_from_dispex(dispex), name, &str);
    if(FAILED(hres))
        return hres;
    if(!str && !(str = SysAllocString(L"")))
        return E_OUTOFMEMORY;
    V_VT(res) = VT_BSTR;
    V_BSTR(res) = str;
    return S_OK;
}

#define IE9_ATTR_HOOK(hook, getter, name) \
    static HRESULT hook(DispatchEx *dispex, WORD flags, DISPPARAMS *dp, VARIANT *res, \
            EXCEPINFO *ei, IServiceProvider *caller) \
    { \
        return ie9_attr_get(dispex, flags, dp, res, getter, name); \
    }

IE9_ATTR_HOOK(ie9_cite_hook,     get_url_attr,     L"cite")
IE9_ATTR_HOOK(ie9_datetime_hook, elem_get_attr_string,  L"datetime")
IE9_ATTR_HOOK(ie9_width_hook,    elem_get_attr_string,  L"width")
IE9_ATTR_HOOK(ie9_clear_hook,    get_clear_attr,   L"clear")
IE9_ATTR_HOOK(ie9_align_hook,    get_align_attr,   L"align")
IE9_ATTR_HOOK(ie9_face_hook,     elem_get_attr_string,  L"face")
IE9_ATTR_HOOK(ie9_href_hook,     get_url_attr,     L"href")
IE9_ATTR_HOOK(ie9_target_hook,   elem_get_attr_string,  L"target")

static HRESULT ie9_color_hook(DispatchEx *dispex, WORD flags, DISPPARAMS *dp, VARIANT *res,
        EXCEPINFO *ei, IServiceProvider *caller)
{
    HRESULT hres;

    if(!(flags & DISPATCH_PROPERTYGET) || (dp && dp->cArgs))
        return S_FALSE;

    hres = elem_get_color_attr(elem_from_dispex(dispex), L"color", res);
    if(SUCCEEDED(hres) && !V_BSTR(res) && !(V_BSTR(res) = SysAllocString(L"")))
        hres = E_OUTOFMEMORY;
    return hres;
}

static HRESULT ie9_font_size_hook(DispatchEx *dispex, WORD flags, DISPPARAMS *dp, VARIANT *res,
        EXCEPINFO *ei, IServiceProvider *caller)
{
    HTMLElement *elem = elem_from_dispex(dispex);
    HRESULT hres;

    if(!(flags & DISPATCH_PROPERTYGET) || (dp && dp->cArgs))
        return S_FALSE;

    hres = get_font_size_attr(elem, res);
    if(SUCCEEDED(hres) && !V_BSTR(res)
       && !(V_BSTR(res) = SysAllocString(element_has_attribute(elem, L"size") ? L"3" : L"")))
        hres = E_OUTOFMEMORY;
    return hres;
}

struct HTMLPhraseElement {
    HTMLElement element;

    IHTMLPhraseElement IHTMLPhraseElement_iface;
    IHTMLPhraseElement2 IHTMLPhraseElement2_iface;
    IHTMLPhraseElement3 IHTMLPhraseElement3_iface;
    IHTMLSpanElement IHTMLSpanElement_iface;
};

static inline HTMLPhraseElement *impl_from_IHTMLPhraseElement(IHTMLPhraseElement *iface)
{
    return CONTAINING_RECORD(iface, HTMLPhraseElement, IHTMLPhraseElement_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLPhraseElement, IHTMLPhraseElement,
                      impl_from_IHTMLPhraseElement(iface)->element.node.event_target.dispex)

static const IHTMLPhraseElementVtbl HTMLPhraseElementVtbl = {
    HTMLPhraseElement_QueryInterface,
    HTMLPhraseElement_AddRef,
    HTMLPhraseElement_Release,
    HTMLPhraseElement_GetTypeInfoCount,
    HTMLPhraseElement_GetTypeInfo,
    HTMLPhraseElement_GetIDsOfNames,
    HTMLPhraseElement_Invoke
};

static inline HTMLPhraseElement *impl_from_IHTMLPhraseElement2(IHTMLPhraseElement2 *iface)
{
    return CONTAINING_RECORD(iface, HTMLPhraseElement, IHTMLPhraseElement2_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLPhraseElement2, IHTMLPhraseElement2,
                      impl_from_IHTMLPhraseElement2(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLPhraseElement2_put_cite(IHTMLPhraseElement2 *iface, BSTR v)
{
    return elem_put_reflected_string(&impl_from_IHTMLPhraseElement2(iface)->element, L"cite", v);
}

static HRESULT WINAPI HTMLPhraseElement2_get_cite(IHTMLPhraseElement2 *iface, BSTR *p)
{
    return elem_get_attr_string(&impl_from_IHTMLPhraseElement2(iface)->element, L"cite", p);
}

static HRESULT WINAPI HTMLPhraseElement2_put_dateTime(IHTMLPhraseElement2 *iface, BSTR v)
{
    return elem_put_reflected_string(&impl_from_IHTMLPhraseElement2(iface)->element, L"datetime", v);
}

static HRESULT WINAPI HTMLPhraseElement2_get_dateTime(IHTMLPhraseElement2 *iface, BSTR *p)
{
    return elem_get_attr_string(&impl_from_IHTMLPhraseElement2(iface)->element, L"datetime", p);
}

static const IHTMLPhraseElement2Vtbl HTMLPhraseElement2Vtbl = {
    HTMLPhraseElement2_QueryInterface,
    HTMLPhraseElement2_AddRef,
    HTMLPhraseElement2_Release,
    HTMLPhraseElement2_GetTypeInfoCount,
    HTMLPhraseElement2_GetTypeInfo,
    HTMLPhraseElement2_GetIDsOfNames,
    HTMLPhraseElement2_Invoke,
    HTMLPhraseElement2_put_cite,
    HTMLPhraseElement2_get_cite,
    HTMLPhraseElement2_put_dateTime,
    HTMLPhraseElement2_get_dateTime
};

static inline HTMLPhraseElement *impl_from_IHTMLPhraseElement3(IHTMLPhraseElement3 *iface)
{
    return CONTAINING_RECORD(iface, HTMLPhraseElement, IHTMLPhraseElement3_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLPhraseElement3, IHTMLPhraseElement3,
                      impl_from_IHTMLPhraseElement3(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLPhraseElement3_put_cite(IHTMLPhraseElement3 *iface, BSTR v)
{
    return elem_put_reflected_string(&impl_from_IHTMLPhraseElement3(iface)->element, L"cite", v);
}

static HRESULT WINAPI HTMLPhraseElement3_get_cite(IHTMLPhraseElement3 *iface, BSTR *p)
{
    return get_url_attr(&impl_from_IHTMLPhraseElement3(iface)->element, L"cite", p);
}

static const IHTMLPhraseElement3Vtbl HTMLPhraseElement3Vtbl = {
    HTMLPhraseElement3_QueryInterface,
    HTMLPhraseElement3_AddRef,
    HTMLPhraseElement3_Release,
    HTMLPhraseElement3_GetTypeInfoCount,
    HTMLPhraseElement3_GetTypeInfo,
    HTMLPhraseElement3_GetIDsOfNames,
    HTMLPhraseElement3_Invoke,
    HTMLPhraseElement3_put_cite,
    HTMLPhraseElement3_get_cite
};

static inline HTMLPhraseElement *impl_from_IHTMLSpanElement(IHTMLSpanElement *iface)
{
    return CONTAINING_RECORD(iface, HTMLPhraseElement, IHTMLSpanElement_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLSpanElement, IHTMLSpanElement,
                      impl_from_IHTMLSpanElement(iface)->element.node.event_target.dispex)

static const IHTMLSpanElementVtbl HTMLSpanElementVtbl = {
    HTMLSpanElement_QueryInterface,
    HTMLSpanElement_AddRef,
    HTMLSpanElement_Release,
    HTMLSpanElement_GetTypeInfoCount,
    HTMLSpanElement_GetTypeInfo,
    HTMLSpanElement_GetIDsOfNames,
    HTMLSpanElement_Invoke
};

static const NodeImplVtbl HTMLSpanElementImplVtbl;

static inline HTMLPhraseElement *HTMLPhraseElement_from_DispatchEx(DispatchEx *iface)
{
    return CONTAINING_RECORD(iface, HTMLPhraseElement, element.node.event_target.dispex);
}

static void *HTMLPhraseElement_query_interface(DispatchEx *dispex, REFIID riid)
{
    HTMLPhraseElement *This = HTMLPhraseElement_from_DispatchEx(dispex);

    if(This->element.node.vtbl == &HTMLSpanElementImplVtbl && IsEqualGUID(&IID_IHTMLSpanElement, riid))
        return &This->IHTMLSpanElement_iface;
    if(This->element.node.vtbl == &HTMLSpanElementImplVtbl && IsEqualGUID(&IID_IHTMLDatabinding, riid))
        return &This->element.IHTMLDatabinding_iface;
    if(IsEqualGUID(&IID_IHTMLPhraseElement, riid))
        return &This->IHTMLPhraseElement_iface;
    if(IsEqualGUID(&IID_IHTMLPhraseElement2, riid))
        return &This->IHTMLPhraseElement2_iface;
    if(IsEqualGUID(&IID_IHTMLPhraseElement3, riid))
        return &This->IHTMLPhraseElement3_iface;

    return HTMLElement_query_interface(&This->element.node.event_target.dispex, riid);
}

static const NodeImplVtbl HTMLPhraseElementImplVtbl = {
    .clsid                 = &CLSID_HTMLPhraseElement,
    .cpc_entries           = HTMLElement_cpc,
    .clone                 = HTMLElement_clone,
    .get_attr_col          = HTMLElement_get_attr_col
};

static const NodeImplVtbl HTMLSpanElementImplVtbl = {
    .clsid                 = &CLSID_HTMLSpanElement,
    .cpc_entries           = HTMLElement_cpc,
    .clone                 = HTMLElement_clone,
    .get_attr_col          = HTMLElement_get_attr_col
};

static const event_target_vtbl_t HTMLPhraseElement_event_target_vtbl = {
    {
        HTMLELEMENT_DISPEX_VTBL_ENTRIES,
        .query_interface= HTMLPhraseElement_query_interface,
        .destructor     = HTMLElement_destructor,
        .traverse       = HTMLElement_traverse,
        .unlink         = HTMLElement_unlink
    },
    HTMLELEMENT_EVENT_TARGET_VTBL_ENTRIES,
    .handle_event       = HTMLElement_handle_event
};

static void HTMLPhraseElement_init_dispex_info(dispex_data_t *info, compat_mode_t mode)
{
    static const dispex_hook_t phrase2_ie9_hooks[] = {
        {DISPID_IHTMLPHRASEELEMENT2_CITE, ie9_cite_hook},
        {DISPID_IHTMLPHRASEELEMENT2_DATETIME, ie9_datetime_hook},
        {DISPID_UNKNOWN}
    };

    HTMLElement_init_dispex_info(info, mode);
    dispex_info_add_interface(info, IHTMLPhraseElement2_tid, mode >= COMPAT_MODE_IE9 ? phrase2_ie9_hooks : NULL);
}

dispex_static_data_t HTMLPhraseElement_dispex = {
    .id           = OBJID_HTMLPhraseElement,
    .prototype_id = OBJID_HTMLElement,
    .vtbl         = &HTMLPhraseElement_event_target_vtbl.dispex_vtbl,
    .disp_tid     = DispHTMLPhraseElement_tid,
    .init_info    = HTMLPhraseElement_init_dispex_info,
};

dispex_static_data_t HTMLModElement_dispex = {
    .id           = OBJID_HTMLModElement,
    .prototype_id = OBJID_HTMLElement,
    .vtbl         = &HTMLPhraseElement_event_target_vtbl.dispex_vtbl,
    .disp_tid     = DispHTMLPhraseElement_tid,
    .init_info    = HTMLPhraseElement_init_dispex_info,
};

dispex_static_data_t HTMLQuoteElement_dispex = {
    .id           = OBJID_HTMLQuoteElement,
    .prototype_id = OBJID_HTMLElement,
    .vtbl         = &HTMLPhraseElement_event_target_vtbl.dispex_vtbl,
    .disp_tid     = DispHTMLPhraseElement_tid,
    .init_info    = HTMLPhraseElement_init_dispex_info,
};

static void HTMLSpanElement_init_dispex_info(dispex_data_t *info, compat_mode_t mode)
{
    if(mode < COMPAT_MODE_IE11)
        dispex_info_add_interface(info, IHTMLDatabinding_tid, NULL);
    HTMLElement_init_dispex_info(info, mode);
}

dispex_static_data_t HTMLSpanElement_dispex = {
    .id           = OBJID_HTMLSpanElement,
    .prototype_id = OBJID_HTMLElement,
    .vtbl         = &HTMLPhraseElement_event_target_vtbl.dispex_vtbl,
    .disp_tid     = DispHTMLSpanElement_tid,
    .init_info    = HTMLSpanElement_init_dispex_info,
};

static HRESULT create_phrase_element(HTMLDocumentNode *doc, nsIDOMElement *nselem, const NodeImplVtbl *vtbl,
        dispex_static_data_t *dispex, HTMLElement **elem)
{
    HTMLPhraseElement *ret;

    ret = calloc(1, sizeof(*ret));
    if(!ret)
        return E_OUTOFMEMORY;

    ret->IHTMLPhraseElement_iface.lpVtbl = &HTMLPhraseElementVtbl;
    ret->IHTMLPhraseElement2_iface.lpVtbl = &HTMLPhraseElement2Vtbl;
    ret->IHTMLPhraseElement3_iface.lpVtbl = &HTMLPhraseElement3Vtbl;
    ret->IHTMLSpanElement_iface.lpVtbl = &HTMLSpanElementVtbl;
    ret->element.node.vtbl = vtbl;

    HTMLElement_Init(&ret->element, doc, nselem, dispex);

    *elem = &ret->element;
    return S_OK;
}

HRESULT HTMLPhraseElement_Create(HTMLDocumentNode *doc, nsIDOMElement *nselem, HTMLElement **elem)
{
    return create_phrase_element(doc, nselem, &HTMLPhraseElementImplVtbl, &HTMLPhraseElement_dispex, elem);
}

HRESULT HTMLModElement_Create(HTMLDocumentNode *doc, nsIDOMElement *nselem, HTMLElement **elem)
{
    return create_phrase_element(doc, nselem, &HTMLPhraseElementImplVtbl, &HTMLModElement_dispex, elem);
}

HRESULT HTMLQuoteElement_Create(HTMLDocumentNode *doc, nsIDOMElement *nselem, HTMLElement **elem)
{
    return create_phrase_element(doc, nselem, &HTMLPhraseElementImplVtbl, &HTMLQuoteElement_dispex, elem);
}

HRESULT HTMLSpanElement_Create(HTMLDocumentNode *doc, nsIDOMElement *nselem, HTMLElement **elem)
{
    return create_phrase_element(doc, nselem, &HTMLSpanElementImplVtbl, &HTMLSpanElement_dispex, elem);
}

typedef enum {
    BLOCK_PLAIN,
    BLOCK_PARA,
    BLOCK_HEADER
} block_kind_t;

struct HTMLBlockElement {
    HTMLElement element;

    IHTMLBlockElement IHTMLBlockElement_iface;
    IHTMLBlockElement2 IHTMLBlockElement2_iface;
    IHTMLBlockElement3 IHTMLBlockElement3_iface;
    IHTMLParaElement IHTMLParaElement_iface;
    IHTMLHeaderElement IHTMLHeaderElement_iface;

    block_kind_t kind;
};

static inline HTMLBlockElement *impl_from_IHTMLBlockElement(IHTMLBlockElement *iface)
{
    return CONTAINING_RECORD(iface, HTMLBlockElement, IHTMLBlockElement_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLBlockElement, IHTMLBlockElement,
                      impl_from_IHTMLBlockElement(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLBlockElement_put_clear(IHTMLBlockElement *iface, BSTR v)
{
    return put_keyword_attr(&impl_from_IHTMLBlockElement(iface)->element, L"clear", v, clear_keywords);
}

static HRESULT WINAPI HTMLBlockElement_get_clear(IHTMLBlockElement *iface, BSTR *p)
{
    return get_keyword_attr(&impl_from_IHTMLBlockElement(iface)->element, L"clear", p, clear_keywords);
}

static const IHTMLBlockElementVtbl HTMLBlockElementVtbl = {
    HTMLBlockElement_QueryInterface,
    HTMLBlockElement_AddRef,
    HTMLBlockElement_Release,
    HTMLBlockElement_GetTypeInfoCount,
    HTMLBlockElement_GetTypeInfo,
    HTMLBlockElement_GetIDsOfNames,
    HTMLBlockElement_Invoke,
    HTMLBlockElement_put_clear,
    HTMLBlockElement_get_clear
};

static inline HTMLBlockElement *impl_from_IHTMLBlockElement2(IHTMLBlockElement2 *iface)
{
    return CONTAINING_RECORD(iface, HTMLBlockElement, IHTMLBlockElement2_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLBlockElement2, IHTMLBlockElement2,
                      impl_from_IHTMLBlockElement2(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLBlockElement2_put_cite(IHTMLBlockElement2 *iface, BSTR v)
{
    HTMLBlockElement *This = impl_from_IHTMLBlockElement2(iface);

    if(This->kind != BLOCK_PLAIN)
        return S_OK;
    return elem_put_reflected_string(&This->element, L"cite", v);
}

static HRESULT WINAPI HTMLBlockElement2_get_cite(IHTMLBlockElement2 *iface, BSTR *p)
{
    HTMLBlockElement *This = impl_from_IHTMLBlockElement2(iface);

    if(This->kind != BLOCK_PLAIN) {
        *p = NULL;
        return S_OK;
    }
    return elem_get_attr_string(&This->element, L"cite", p);
}

static HRESULT WINAPI HTMLBlockElement2_put_width(IHTMLBlockElement2 *iface, BSTR v)
{
    HTMLBlockElement *This = impl_from_IHTMLBlockElement2(iface);

    if(This->kind != BLOCK_PLAIN)
        return S_OK;
    return elem_put_reflected_string(&This->element, L"width", v);
}

static HRESULT WINAPI HTMLBlockElement2_get_width(IHTMLBlockElement2 *iface, BSTR *p)
{
    HTMLBlockElement *This = impl_from_IHTMLBlockElement2(iface);

    if(This->kind != BLOCK_PLAIN) {
        *p = NULL;
        return S_OK;
    }
    return elem_get_attr_string(&This->element, L"width", p);
}

static const IHTMLBlockElement2Vtbl HTMLBlockElement2Vtbl = {
    HTMLBlockElement2_QueryInterface,
    HTMLBlockElement2_AddRef,
    HTMLBlockElement2_Release,
    HTMLBlockElement2_GetTypeInfoCount,
    HTMLBlockElement2_GetTypeInfo,
    HTMLBlockElement2_GetIDsOfNames,
    HTMLBlockElement2_Invoke,
    HTMLBlockElement2_put_cite,
    HTMLBlockElement2_get_cite,
    HTMLBlockElement2_put_width,
    HTMLBlockElement2_get_width
};

static inline HTMLBlockElement *impl_from_IHTMLBlockElement3(IHTMLBlockElement3 *iface)
{
    return CONTAINING_RECORD(iface, HTMLBlockElement, IHTMLBlockElement3_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLBlockElement3, IHTMLBlockElement3,
                      impl_from_IHTMLBlockElement3(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLBlockElement3_put_cite(IHTMLBlockElement3 *iface, BSTR v)
{
    HTMLBlockElement *This = impl_from_IHTMLBlockElement3(iface);

    if(This->kind != BLOCK_PLAIN)
        return S_OK;
    return elem_put_reflected_string(&This->element, L"cite", v);
}

static HRESULT WINAPI HTMLBlockElement3_get_cite(IHTMLBlockElement3 *iface, BSTR *p)
{
    HTMLBlockElement *This = impl_from_IHTMLBlockElement3(iface);

    if(This->kind != BLOCK_PLAIN)
        return (*p = SysAllocString(L"")) ? S_OK : E_OUTOFMEMORY;
    return get_url_attr(&This->element, L"cite", p);
}

static const IHTMLBlockElement3Vtbl HTMLBlockElement3Vtbl = {
    HTMLBlockElement3_QueryInterface,
    HTMLBlockElement3_AddRef,
    HTMLBlockElement3_Release,
    HTMLBlockElement3_GetTypeInfoCount,
    HTMLBlockElement3_GetTypeInfo,
    HTMLBlockElement3_GetIDsOfNames,
    HTMLBlockElement3_Invoke,
    HTMLBlockElement3_put_cite,
    HTMLBlockElement3_get_cite
};

static inline HTMLBlockElement *impl_from_IHTMLParaElement(IHTMLParaElement *iface)
{
    return CONTAINING_RECORD(iface, HTMLBlockElement, IHTMLParaElement_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLParaElement, IHTMLParaElement,
                      impl_from_IHTMLParaElement(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLParaElement_put_align(IHTMLParaElement *iface, BSTR v)
{
    return put_keyword_attr(&impl_from_IHTMLParaElement(iface)->element, L"align", v, align_keywords);
}

static HRESULT WINAPI HTMLParaElement_get_align(IHTMLParaElement *iface, BSTR *p)
{
    return get_keyword_attr(&impl_from_IHTMLParaElement(iface)->element, L"align", p, align_keywords);
}

static const IHTMLParaElementVtbl HTMLParaElementVtbl = {
    HTMLParaElement_QueryInterface,
    HTMLParaElement_AddRef,
    HTMLParaElement_Release,
    HTMLParaElement_GetTypeInfoCount,
    HTMLParaElement_GetTypeInfo,
    HTMLParaElement_GetIDsOfNames,
    HTMLParaElement_Invoke,
    HTMLParaElement_put_align,
    HTMLParaElement_get_align
};

static inline HTMLBlockElement *impl_from_IHTMLHeaderElement(IHTMLHeaderElement *iface)
{
    return CONTAINING_RECORD(iface, HTMLBlockElement, IHTMLHeaderElement_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLHeaderElement, IHTMLHeaderElement,
                      impl_from_IHTMLHeaderElement(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLHeaderElement_put_align(IHTMLHeaderElement *iface, BSTR v)
{
    return put_keyword_attr(&impl_from_IHTMLHeaderElement(iface)->element, L"align", v, align_keywords);
}

static HRESULT WINAPI HTMLHeaderElement_get_align(IHTMLHeaderElement *iface, BSTR *p)
{
    return get_keyword_attr(&impl_from_IHTMLHeaderElement(iface)->element, L"align", p, align_keywords);
}

static const IHTMLHeaderElementVtbl HTMLHeaderElementVtbl = {
    HTMLHeaderElement_QueryInterface,
    HTMLHeaderElement_AddRef,
    HTMLHeaderElement_Release,
    HTMLHeaderElement_GetTypeInfoCount,
    HTMLHeaderElement_GetTypeInfo,
    HTMLHeaderElement_GetIDsOfNames,
    HTMLHeaderElement_Invoke,
    HTMLHeaderElement_put_align,
    HTMLHeaderElement_get_align
};

static inline HTMLBlockElement *HTMLBlockElement_from_DispatchEx(DispatchEx *iface)
{
    return CONTAINING_RECORD(iface, HTMLBlockElement, element.node.event_target.dispex);
}

static void *HTMLBlockElement_query_interface(DispatchEx *dispex, REFIID riid)
{
    HTMLBlockElement *This = HTMLBlockElement_from_DispatchEx(dispex);

    if(IsEqualGUID(&IID_IHTMLBlockElement, riid))
        return &This->IHTMLBlockElement_iface;
    if(IsEqualGUID(&IID_IHTMLBlockElement2, riid))
        return &This->IHTMLBlockElement2_iface;
    if(IsEqualGUID(&IID_IHTMLBlockElement3, riid))
        return &This->IHTMLBlockElement3_iface;
    if(This->kind == BLOCK_PARA && IsEqualGUID(&IID_IHTMLParaElement, riid))
        return &This->IHTMLParaElement_iface;
    if(This->kind == BLOCK_HEADER && IsEqualGUID(&IID_IHTMLHeaderElement, riid))
        return &This->IHTMLHeaderElement_iface;

    return HTMLElement_query_interface(&This->element.node.event_target.dispex, riid);
}

static const NodeImplVtbl HTMLBlockElementImplVtbl = {
    .clsid                 = &CLSID_HTMLBlockElement,
    .cpc_entries           = HTMLElement_cpc,
    .clone                 = HTMLElement_clone,
    .get_attr_col          = HTMLElement_get_attr_col
};

static const NodeImplVtbl HTMLParaElementImplVtbl = {
    .clsid                 = &CLSID_HTMLParaElement,
    .cpc_entries           = HTMLElement_cpc,
    .clone                 = HTMLElement_clone,
    .get_attr_col          = HTMLElement_get_attr_col
};

static const NodeImplVtbl HTMLHeaderElementImplVtbl = {
    .clsid                 = &CLSID_HTMLHeaderElement,
    .cpc_entries           = HTMLElement_cpc,
    .clone                 = HTMLElement_clone,
    .get_attr_col          = HTMLElement_get_attr_col
};

static const event_target_vtbl_t HTMLBlockElement_event_target_vtbl = {
    {
        HTMLELEMENT_DISPEX_VTBL_ENTRIES,
        .query_interface= HTMLBlockElement_query_interface,
        .destructor     = HTMLElement_destructor,
        .traverse       = HTMLElement_traverse,
        .unlink         = HTMLElement_unlink
    },
    HTMLELEMENT_EVENT_TARGET_VTBL_ENTRIES,
    .handle_event       = HTMLElement_handle_event
};

static const dispex_hook_t clear_ie9_hooks[] = {
    {DISPID_A_CLEAR, ie9_clear_hook},
    {DISPID_UNKNOWN}
};

static const dispex_hook_t align_ie9_hooks[] = {
    {STDPROPID_XOBJ_BLOCKALIGN, ie9_align_hook},
    {DISPID_UNKNOWN}
};

static void HTMLBlockElement_init_dispex_info(dispex_data_t *info, compat_mode_t mode)
{
    static const dispex_hook_t block2_ie9_hooks[] = {
        {DISPID_IHTMLBLOCKELEMENT2_CITE, ie9_cite_hook},
        {DISPID_IHTMLBLOCKELEMENT2_WIDTH, ie9_width_hook},
        {DISPID_UNKNOWN}
    };

    HTMLElement_init_dispex_info(info, mode);
    dispex_info_add_interface(info, IHTMLBlockElement_tid, mode >= COMPAT_MODE_IE9 ? clear_ie9_hooks : NULL);
    dispex_info_add_interface(info, IHTMLBlockElement2_tid, mode >= COMPAT_MODE_IE9 ? block2_ie9_hooks : NULL);
}

dispex_static_data_t HTMLBlockElement_dispex = {
    .id           = OBJID_HTMLBlockElement,
    .prototype_id = OBJID_HTMLElement,
    .vtbl         = &HTMLBlockElement_event_target_vtbl.dispex_vtbl,
    .disp_tid     = DispHTMLBlockElement_tid,
    .init_info    = HTMLBlockElement_init_dispex_info,
};

dispex_static_data_t HTMLPreElement_dispex = {
    .id           = OBJID_HTMLPreElement,
    .prototype_id = OBJID_HTMLElement,
    .vtbl         = &HTMLBlockElement_event_target_vtbl.dispex_vtbl,
    .disp_tid     = DispHTMLBlockElement_tid,
    .init_info    = HTMLBlockElement_init_dispex_info,
};

static void HTMLParagraphElement_init_dispex_info(dispex_data_t *info, compat_mode_t mode)
{
    HTMLElement_init_dispex_info(info, mode);
    dispex_info_add_interface(info, IHTMLBlockElement_tid, mode >= COMPAT_MODE_IE9 ? clear_ie9_hooks : NULL);
    dispex_info_add_interface(info, IHTMLParaElement_tid, mode >= COMPAT_MODE_IE9 ? align_ie9_hooks : NULL);
}

dispex_static_data_t HTMLParagraphElement_dispex = {
    .id           = OBJID_HTMLParagraphElement,
    .prototype_id = OBJID_HTMLElement,
    .vtbl         = &HTMLBlockElement_event_target_vtbl.dispex_vtbl,
    .disp_tid     = DispHTMLParaElement_tid,
    .init_info    = HTMLParagraphElement_init_dispex_info,
};

static void HTMLHeadingElement_init_dispex_info(dispex_data_t *info, compat_mode_t mode)
{
    HTMLElement_init_dispex_info(info, mode);
    dispex_info_add_interface(info, IHTMLBlockElement_tid, mode >= COMPAT_MODE_IE9 ? clear_ie9_hooks : NULL);
    dispex_info_add_interface(info, IHTMLHeaderElement_tid, mode >= COMPAT_MODE_IE9 ? align_ie9_hooks : NULL);
}

dispex_static_data_t HTMLHeadingElement_dispex = {
    .id           = OBJID_HTMLHeadingElement,
    .prototype_id = OBJID_HTMLElement,
    .vtbl         = &HTMLBlockElement_event_target_vtbl.dispex_vtbl,
    .disp_tid     = DispHTMLHeaderElement_tid,
    .init_info    = HTMLHeadingElement_init_dispex_info,
};

static HRESULT create_block_element(HTMLDocumentNode *doc, nsIDOMElement *nselem, block_kind_t kind,
        const NodeImplVtbl *vtbl, dispex_static_data_t *dispex, HTMLElement **elem)
{
    HTMLBlockElement *ret;

    ret = calloc(1, sizeof(*ret));
    if(!ret)
        return E_OUTOFMEMORY;

    ret->IHTMLBlockElement_iface.lpVtbl = &HTMLBlockElementVtbl;
    ret->IHTMLBlockElement2_iface.lpVtbl = &HTMLBlockElement2Vtbl;
    ret->IHTMLBlockElement3_iface.lpVtbl = &HTMLBlockElement3Vtbl;
    ret->IHTMLParaElement_iface.lpVtbl = &HTMLParaElementVtbl;
    ret->IHTMLHeaderElement_iface.lpVtbl = &HTMLHeaderElementVtbl;
    ret->kind = kind;
    ret->element.node.vtbl = vtbl;

    HTMLElement_Init(&ret->element, doc, nselem, dispex);

    *elem = &ret->element;
    return S_OK;
}

HRESULT HTMLBlockElement_Create(HTMLDocumentNode *doc, nsIDOMElement *nselem, HTMLElement **elem)
{
    return create_block_element(doc, nselem, BLOCK_PLAIN, &HTMLBlockElementImplVtbl, &HTMLBlockElement_dispex, elem);
}

HRESULT HTMLPreElement_Create(HTMLDocumentNode *doc, nsIDOMElement *nselem, HTMLElement **elem)
{
    return create_block_element(doc, nselem, BLOCK_PLAIN, &HTMLBlockElementImplVtbl, &HTMLPreElement_dispex, elem);
}

HRESULT HTMLParaElement_Create(HTMLDocumentNode *doc, nsIDOMElement *nselem, HTMLElement **elem)
{
    return create_block_element(doc, nselem, BLOCK_PARA, &HTMLParaElementImplVtbl, &HTMLParagraphElement_dispex, elem);
}

HRESULT HTMLHeaderElement_Create(HTMLDocumentNode *doc, nsIDOMElement *nselem, HTMLElement **elem)
{
    return create_block_element(doc, nselem, BLOCK_HEADER, &HTMLHeaderElementImplVtbl, &HTMLHeadingElement_dispex, elem);
}

struct HTMLBRElement {
    HTMLElement element;

    IHTMLBRElement IHTMLBRElement_iface;
};

static inline HTMLBRElement *impl_from_IHTMLBRElement(IHTMLBRElement *iface)
{
    return CONTAINING_RECORD(iface, HTMLBRElement, IHTMLBRElement_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLBRElement, IHTMLBRElement,
                      impl_from_IHTMLBRElement(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLBRElement_put_clear(IHTMLBRElement *iface, BSTR v)
{
    return put_keyword_attr(&impl_from_IHTMLBRElement(iface)->element, L"clear", v, clear_keywords);
}

static HRESULT WINAPI HTMLBRElement_get_clear(IHTMLBRElement *iface, BSTR *p)
{
    return get_keyword_attr(&impl_from_IHTMLBRElement(iface)->element, L"clear", p, clear_keywords);
}

static const IHTMLBRElementVtbl HTMLBRElementVtbl = {
    HTMLBRElement_QueryInterface,
    HTMLBRElement_AddRef,
    HTMLBRElement_Release,
    HTMLBRElement_GetTypeInfoCount,
    HTMLBRElement_GetTypeInfo,
    HTMLBRElement_GetIDsOfNames,
    HTMLBRElement_Invoke,
    HTMLBRElement_put_clear,
    HTMLBRElement_get_clear
};

static inline HTMLBRElement *HTMLBRElement_from_DispatchEx(DispatchEx *iface)
{
    return CONTAINING_RECORD(iface, HTMLBRElement, element.node.event_target.dispex);
}

static void *HTMLBRElement_query_interface(DispatchEx *dispex, REFIID riid)
{
    HTMLBRElement *This = HTMLBRElement_from_DispatchEx(dispex);

    if(IsEqualGUID(&IID_IHTMLBRElement, riid))
        return &This->IHTMLBRElement_iface;

    return HTMLElement_query_interface(&This->element.node.event_target.dispex, riid);
}

static const NodeImplVtbl HTMLBRElementImplVtbl = {
    .clsid                 = &CLSID_HTMLBRElement,
    .cpc_entries           = HTMLElement_cpc,
    .clone                 = HTMLElement_clone,
    .get_attr_col          = HTMLElement_get_attr_col
};

static const event_target_vtbl_t HTMLBRElement_event_target_vtbl = {
    {
        HTMLELEMENT_DISPEX_VTBL_ENTRIES,
        .query_interface= HTMLBRElement_query_interface,
        .destructor     = HTMLElement_destructor,
        .traverse       = HTMLElement_traverse,
        .unlink         = HTMLElement_unlink
    },
    HTMLELEMENT_EVENT_TARGET_VTBL_ENTRIES,
    .handle_event       = HTMLElement_handle_event
};

static void HTMLBRElement_init_dispex_info(dispex_data_t *info, compat_mode_t mode)
{
    HTMLElement_init_dispex_info(info, mode);
    dispex_info_add_interface(info, IHTMLBRElement_tid, mode >= COMPAT_MODE_IE9 ? clear_ie9_hooks : NULL);
}

dispex_static_data_t HTMLBRElement_dispex = {
    .id           = OBJID_HTMLBRElement,
    .prototype_id = OBJID_HTMLElement,
    .vtbl         = &HTMLBRElement_event_target_vtbl.dispex_vtbl,
    .disp_tid     = DispHTMLBRElement_tid,
    .init_info    = HTMLBRElement_init_dispex_info,
};

HRESULT HTMLBRElement_Create(HTMLDocumentNode *doc, nsIDOMElement *nselem, HTMLElement **elem)
{
    HTMLBRElement *ret;

    ret = calloc(1, sizeof(*ret));
    if(!ret)
        return E_OUTOFMEMORY;

    ret->IHTMLBRElement_iface.lpVtbl = &HTMLBRElementVtbl;
    ret->element.node.vtbl = &HTMLBRElementImplVtbl;

    HTMLElement_Init(&ret->element, doc, nselem, &HTMLBRElement_dispex);

    *elem = &ret->element;
    return S_OK;
}

struct HTMLDivElement {
    HTMLElement element;

    IHTMLDivElement IHTMLDivElement_iface;
};

static inline HTMLDivElement *impl_from_IHTMLDivElement(IHTMLDivElement *iface)
{
    return CONTAINING_RECORD(iface, HTMLDivElement, IHTMLDivElement_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLDivElement, IHTMLDivElement,
                      impl_from_IHTMLDivElement(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLDivElement_put_align(IHTMLDivElement *iface, BSTR v)
{
    return put_keyword_attr(&impl_from_IHTMLDivElement(iface)->element, L"align", v, align_keywords);
}

static HRESULT WINAPI HTMLDivElement_get_align(IHTMLDivElement *iface, BSTR *p)
{
    /* FIXME: Gecko serializes the align alias middle as center, so it reads back as center instead of empty */
    return get_keyword_attr(&impl_from_IHTMLDivElement(iface)->element, L"align", p, align_keywords);
}

static HRESULT WINAPI HTMLDivElement_put_noWrap(IHTMLDivElement *iface, VARIANT_BOOL v)
{
    return elem_put_bool_attr(&impl_from_IHTMLDivElement(iface)->element, L"nowrap", v);
}

static HRESULT WINAPI HTMLDivElement_get_noWrap(IHTMLDivElement *iface, VARIANT_BOOL *p)
{
    return elem_get_bool_attr(&impl_from_IHTMLDivElement(iface)->element, L"nowrap", p);
}

static const IHTMLDivElementVtbl HTMLDivElementVtbl = {
    HTMLDivElement_QueryInterface,
    HTMLDivElement_AddRef,
    HTMLDivElement_Release,
    HTMLDivElement_GetTypeInfoCount,
    HTMLDivElement_GetTypeInfo,
    HTMLDivElement_GetIDsOfNames,
    HTMLDivElement_Invoke,
    HTMLDivElement_put_align,
    HTMLDivElement_get_align,
    HTMLDivElement_put_noWrap,
    HTMLDivElement_get_noWrap
};

static inline HTMLDivElement *HTMLDivElement_from_DispatchEx(DispatchEx *iface)
{
    return CONTAINING_RECORD(iface, HTMLDivElement, element.node.event_target.dispex);
}

static void *HTMLDivElement_query_interface(DispatchEx *dispex, REFIID riid)
{
    HTMLDivElement *This = HTMLDivElement_from_DispatchEx(dispex);

    if(IsEqualGUID(&IID_IHTMLDivElement, riid))
        return &This->IHTMLDivElement_iface;
    if(IsEqualGUID(&IID_IHTMLDatabinding, riid))
        return &This->element.IHTMLDatabinding_iface;

    return HTMLElement_query_interface(&This->element.node.event_target.dispex, riid);
}

static const NodeImplVtbl HTMLDivElementImplVtbl = {
    .clsid                 = &CLSID_HTMLDivElement,
    .cpc_entries           = HTMLElement_cpc,
    .clone                 = HTMLElement_clone,
    .get_attr_col          = HTMLElement_get_attr_col
};

static const event_target_vtbl_t HTMLDivElement_event_target_vtbl = {
    {
        HTMLELEMENT_DISPEX_VTBL_ENTRIES,
        .query_interface= HTMLDivElement_query_interface,
        .destructor     = HTMLElement_destructor,
        .traverse       = HTMLElement_traverse,
        .unlink         = HTMLElement_unlink
    },
    HTMLELEMENT_EVENT_TARGET_VTBL_ENTRIES,
    .handle_event       = HTMLElement_handle_event
};

static void HTMLDivElement_init_dispex_info(dispex_data_t *info, compat_mode_t mode)
{
    if(mode < COMPAT_MODE_IE11)
        dispex_info_add_interface(info, IHTMLDatabinding_tid, NULL);
    HTMLElement_init_dispex_info(info, mode);
    dispex_info_add_interface(info, IHTMLDivElement_tid, mode >= COMPAT_MODE_IE9 ? align_ie9_hooks : NULL);
}

dispex_static_data_t HTMLDivElement_dispex = {
    .id           = OBJID_HTMLDivElement,
    .prototype_id = OBJID_HTMLElement,
    .vtbl         = &HTMLDivElement_event_target_vtbl.dispex_vtbl,
    .disp_tid     = DispHTMLDivElement_tid,
    .init_info    = HTMLDivElement_init_dispex_info,
};

HRESULT HTMLDivElement_Create(HTMLDocumentNode *doc, nsIDOMElement *nselem, HTMLElement **elem)
{
    HTMLDivElement *ret;

    ret = calloc(1, sizeof(*ret));
    if(!ret)
        return E_OUTOFMEMORY;

    ret->IHTMLDivElement_iface.lpVtbl = &HTMLDivElementVtbl;
    ret->element.node.vtbl = &HTMLDivElementImplVtbl;

    HTMLElement_Init(&ret->element, doc, nselem, &HTMLDivElement_dispex);

    *elem = &ret->element;
    return S_OK;
}

typedef enum {
    LENGTH_EMPTY,
    LENGTH_INVALID,
    LENGTH_VALID
} length_parse_t;

#define MAX_LENGTH_VALUE 21474836
#define MAX_LENGTH_MANTISSA 100000000000000000ull

static BOOL is_length_space(WCHAR c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static length_parse_t parse_length(const WCHAR *str, BOOL allow_percent, LONGLONG *hundredths, BOOL *percent)
{
    BOOL negative = FALSE, digits = FALSE;
    ULONGLONG mantissa = 0;
    const WCHAR *ptr;
    int exp = 2;

    *percent = FALSE;
    if(!*str)
        return LENGTH_EMPTY;

    while(is_length_space(*str))
        str++;
    if(*str == '-' || *str == '+')
        negative = *str++ == '-';
    for(; iswdigit(*str); str++, digits = TRUE) {
        if(mantissa < MAX_LENGTH_MANTISSA)
            mantissa = mantissa * 10 + (*str - '0');
        else
            exp++;
    }
    if(*str == '.') {
        for(str++; iswdigit(*str); str++, digits = TRUE) {
            if(mantissa < MAX_LENGTH_MANTISSA) {
                mantissa = mantissa * 10 + (*str - '0');
                exp--;
            }
        }
    }
    if(!digits)
        return LENGTH_INVALID;

    if(*str == 'e' || *str == 'E') {
        BOOL exp_negative = FALSE;
        int e = 0;

        ptr = str + 1;
        if(*ptr == '-' || *ptr == '+')
            exp_negative = *ptr++ == '-';
        if(iswdigit(*ptr)) {
            for(; iswdigit(*ptr); ptr++) {
                if(e < 1000)
                    e = e * 10 + (*ptr - '0');
            }
            exp += exp_negative ? -e : e;
            str = ptr;
        }
    }

    for(; exp > 0 && mantissa; exp--) {
        if(mantissa >= MAX_LENGTH_MANTISSA)
            break;
        mantissa *= 10;
    }
    for(; exp < 0 && mantissa; exp++)
        mantissa /= 10;
    if(mantissa > MAX_LENGTH_MANTISSA)
        mantissa = MAX_LENGTH_MANTISSA;

    while(is_length_space(*str))
        str++;
    *percent = allow_percent && *str == '%';
    *hundredths = negative ? -(LONGLONG)mantissa : (LONGLONG)mantissa;
    return LENGTH_VALID;
}

static void format_length(LONGLONG hundredths, BOOL percent, WCHAR *buf, unsigned size)
{
    LONGLONG abs_val;

    if(!percent) {
        hundredths /= 100;
        if(hundredths > MAX_LENGTH_VALUE)
            hundredths = MAX_LENGTH_VALUE;
        else if(hundredths < -MAX_LENGTH_VALUE)
            hundredths = -MAX_LENGTH_VALUE;
        swprintf(buf, size, L"%ld", (LONG)hundredths);
        return;
    }

    if(hundredths > 2147483647)
        hundredths = 2147483647;
    else if(hundredths < -2147483647 - 1)
        hundredths = -2147483647 - 1;
    abs_val = hundredths < 0 ? -hundredths : hundredths;
    if(!(abs_val % 100))
        swprintf(buf, size, L"%s%ld%%", hundredths < 0 ? L"-" : L"", (LONG)(abs_val / 100));
    else if(!(abs_val % 10))
        swprintf(buf, size, L"%s%ld.%ld%%", hundredths < 0 ? L"-" : L"", (LONG)(abs_val / 100), (LONG)(abs_val % 100 / 10));
    else
        swprintf(buf, size, L"%s%ld.%02ld%%", hundredths < 0 ? L"-" : L"", (LONG)(abs_val / 100), (LONG)(abs_val % 100));
}

static const WCHAR *canonical_length(const WCHAR *value, BOOL allow_percent, const WCHAR *empty, WCHAR *buf, unsigned size)
{
    LONGLONG hundredths;
    BOOL percent;

    switch(parse_length(value, allow_percent, &hundredths, &percent)) {
    case LENGTH_EMPTY:
        return empty;
    case LENGTH_INVALID:
        return NULL;
    default:
        format_length(hundredths, percent, buf, size);
        return buf;
    }
}

static HRESULT put_length_attr(HTMLElement *elem, const WCHAR *name, BOOL allow_percent, const WCHAR *empty, VARIANT *v)
{
    LONGLONG hundredths;
    BOOL percent = FALSE;
    WCHAR buf[32];

    switch(V_VT(v)) {
    case VT_NULL:
        return element_remove_attribute(elem, name);
    case VT_I4:
        hundredths = V_I4(v) * 100ll;
        break;
    case VT_R8:
        if(!isfinite(V_R8(v)))
            return E_INVALIDARG;
        hundredths = V_R8(v) > 1e15 ? 100000000000000000ll : V_R8(v) < -1e15 ? -100000000000000000ll : (LONGLONG)V_R8(v) * 100;
        break;
    case VT_BSTR:
        switch(parse_length(V_BSTR(v) ? V_BSTR(v) : L"", allow_percent, &hundredths, &percent)) {
        case LENGTH_EMPTY:
            if(!empty)
                return element_remove_attribute(elem, name);
            return elem_put_reflected_string(elem, name, (BSTR)empty);
        case LENGTH_INVALID:
            return E_INVALIDARG;
        default:
            break;
        }
        break;
    default:
        return E_INVALIDARG;
    }

    format_length(hundredths, percent, buf, ARRAY_SIZE(buf));
    return elem_put_reflected_string(elem, name, buf);
}

static HRESULT get_length_attr(HTMLElement *elem, const WCHAR *name, BOOL allow_percent, const WCHAR *empty, VARIANT *p)
{
    const WCHAR *canon;
    WCHAR buf[32];
    HRESULT hres;
    BSTR str;

    V_VT(p) = VT_BSTR;
    V_BSTR(p) = NULL;
    hres = elem_get_attr_string(elem, name, &str);
    if(FAILED(hres) || !str)
        return hres;
    canon = canonical_length(str, allow_percent, empty, buf, ARRAY_SIZE(buf));
    SysFreeString(str);
    if(canon && *canon && !(V_BSTR(p) = SysAllocString(canon)))
        return E_OUTOFMEMORY;
    return S_OK;
}

static const struct {
    const WCHAR *name;
    BOOL allow_percent;
    const WCHAR *empty;
} hr_length_attrs[] = {
    { L"width", TRUE,  L"1" },
    { L"size",  FALSE, NULL }
};

static int find_hr_length_attr(HTMLElement *elem, const WCHAR *name);

static HRESULT ie9_hr_length_get(DispatchEx *dispex, WORD flags, DISPPARAMS *dp, VARIANT *res, unsigned idx)
{
    HRESULT hres;

    if(!(flags & DISPATCH_PROPERTYGET) || (dp && dp->cArgs))
        return S_FALSE;

    hres = get_length_attr(elem_from_dispex(dispex), hr_length_attrs[idx].name, hr_length_attrs[idx].allow_percent,
                           hr_length_attrs[idx].empty, res);
    if(SUCCEEDED(hres) && !V_BSTR(res) && !(V_BSTR(res) = SysAllocString(L"")))
        hres = E_OUTOFMEMORY;
    return hres;
}

static HRESULT ie9_hr_width_hook(DispatchEx *dispex, WORD flags, DISPPARAMS *dp, VARIANT *res,
        EXCEPINFO *ei, IServiceProvider *caller)
{
    return ie9_hr_length_get(dispex, flags, dp, res, 0);
}

static HRESULT ie9_hr_size_hook(DispatchEx *dispex, WORD flags, DISPPARAMS *dp, VARIANT *res,
        EXCEPINFO *ei, IServiceProvider *caller)
{
    return ie9_hr_length_get(dispex, flags, dp, res, 1);
}

struct HTMLHRElement {
    HTMLElement element;

    IHTMLHRElement IHTMLHRElement_iface;
};

static inline HTMLHRElement *impl_from_IHTMLHRElement(IHTMLHRElement *iface)
{
    return CONTAINING_RECORD(iface, HTMLHRElement, IHTMLHRElement_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLHRElement, IHTMLHRElement,
                      impl_from_IHTMLHRElement(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLHRElement_put_align(IHTMLHRElement *iface, BSTR v)
{
    return put_keyword_attr(&impl_from_IHTMLHRElement(iface)->element, L"align", v, align_keywords);
}

static HRESULT WINAPI HTMLHRElement_get_align(IHTMLHRElement *iface, BSTR *p)
{
    return get_keyword_attr(&impl_from_IHTMLHRElement(iface)->element, L"align", p, align_keywords);
}

static HRESULT WINAPI HTMLHRElement_put_color(IHTMLHRElement *iface, VARIANT v)
{
    return elem_put_color_attr(&impl_from_IHTMLHRElement(iface)->element, L"color", &v);
}

static HRESULT WINAPI HTMLHRElement_get_color(IHTMLHRElement *iface, VARIANT *p)
{
    return elem_get_color_attr(&impl_from_IHTMLHRElement(iface)->element, L"color", p);
}

static HRESULT WINAPI HTMLHRElement_put_noShade(IHTMLHRElement *iface, VARIANT_BOOL v)
{
    return elem_put_bool_attr(&impl_from_IHTMLHRElement(iface)->element, L"noshade", v);
}

static HRESULT WINAPI HTMLHRElement_get_noShade(IHTMLHRElement *iface, VARIANT_BOOL *p)
{
    return elem_get_bool_attr(&impl_from_IHTMLHRElement(iface)->element, L"noshade", p);
}

static HRESULT WINAPI HTMLHRElement_put_width(IHTMLHRElement *iface, VARIANT v)
{
    return put_length_attr(&impl_from_IHTMLHRElement(iface)->element, L"width", TRUE, L"1", &v);
}

static HRESULT WINAPI HTMLHRElement_get_width(IHTMLHRElement *iface, VARIANT *p)
{
    return get_length_attr(&impl_from_IHTMLHRElement(iface)->element, L"width", TRUE, L"1", p);
}

static HRESULT WINAPI HTMLHRElement_put_size(IHTMLHRElement *iface, VARIANT v)
{
    return put_length_attr(&impl_from_IHTMLHRElement(iface)->element, L"size", FALSE, NULL, &v);
}

static HRESULT WINAPI HTMLHRElement_get_size(IHTMLHRElement *iface, VARIANT *p)
{
    return get_length_attr(&impl_from_IHTMLHRElement(iface)->element, L"size", FALSE, NULL, p);
}

static const IHTMLHRElementVtbl HTMLHRElementVtbl = {
    HTMLHRElement_QueryInterface,
    HTMLHRElement_AddRef,
    HTMLHRElement_Release,
    HTMLHRElement_GetTypeInfoCount,
    HTMLHRElement_GetTypeInfo,
    HTMLHRElement_GetIDsOfNames,
    HTMLHRElement_Invoke,
    HTMLHRElement_put_align,
    HTMLHRElement_get_align,
    HTMLHRElement_put_color,
    HTMLHRElement_get_color,
    HTMLHRElement_put_noShade,
    HTMLHRElement_get_noShade,
    HTMLHRElement_put_width,
    HTMLHRElement_get_width,
    HTMLHRElement_put_size,
    HTMLHRElement_get_size
};

static inline HTMLHRElement *HTMLHRElement_from_DispatchEx(DispatchEx *iface)
{
    return CONTAINING_RECORD(iface, HTMLHRElement, element.node.event_target.dispex);
}

static void *HTMLHRElement_query_interface(DispatchEx *dispex, REFIID riid)
{
    HTMLHRElement *This = HTMLHRElement_from_DispatchEx(dispex);

    if(IsEqualGUID(&IID_IHTMLHRElement, riid))
        return &This->IHTMLHRElement_iface;

    return HTMLElement_query_interface(&This->element.node.event_target.dispex, riid);
}

static const NodeImplVtbl HTMLHRElementImplVtbl = {
    .clsid                 = &CLSID_HTMLHRElement,
    .cpc_entries           = HTMLElement_cpc,
    .clone                 = HTMLElement_clone,
    .get_attr_col          = HTMLElement_get_attr_col
};

static const event_target_vtbl_t HTMLHRElement_event_target_vtbl = {
    {
        HTMLELEMENT_DISPEX_VTBL_ENTRIES,
        .query_interface= HTMLHRElement_query_interface,
        .destructor     = HTMLElement_destructor,
        .traverse       = HTMLElement_traverse,
        .unlink         = HTMLElement_unlink
    },
    HTMLELEMENT_EVENT_TARGET_VTBL_ENTRIES,
    .handle_event       = HTMLElement_handle_event
};

static void HTMLHRElement_init_dispex_info(dispex_data_t *info, compat_mode_t mode)
{
    static const dispex_hook_t hr_ie9_hooks[] = {
        {DISPID_IHTMLHRELEMENT_ALIGN, ie9_align_hook},
        {DISPID_IHTMLHRELEMENT_COLOR, ie9_color_hook},
        {DISPID_IHTMLHRELEMENT_WIDTH, ie9_hr_width_hook},
        {DISPID_IHTMLHRELEMENT_SIZE, ie9_hr_size_hook},
        {DISPID_UNKNOWN}
    };

    HTMLElement_init_dispex_info(info, mode);
    dispex_info_add_interface(info, IHTMLHRElement_tid, mode >= COMPAT_MODE_IE9 ? hr_ie9_hooks : NULL);
}

dispex_static_data_t HTMLHRElement_dispex = {
    .id           = OBJID_HTMLHRElement,
    .prototype_id = OBJID_HTMLElement,
    .vtbl         = &HTMLHRElement_event_target_vtbl.dispex_vtbl,
    .disp_tid     = DispHTMLHRElement_tid,
    .init_info    = HTMLHRElement_init_dispex_info,
};

static int find_hr_length_attr(HTMLElement *elem, const WCHAR *name)
{
    unsigned i;

    if(elem->node.vtbl != &HTMLHRElementImplVtbl)
        return -1;
    for(i = 0; i < ARRAY_SIZE(hr_length_attrs); i++) {
        if(!wcsicmp(hr_length_attrs[i].name, name))
            return i;
    }
    return -1;
}

static BOOL is_block_align_attr(HTMLElement *elem, const WCHAR *name)
{
    return (elem->node.vtbl == &HTMLHRElementImplVtbl || elem->node.vtbl == &HTMLDivElementImplVtbl) && !wcsicmp(name, L"align");
}

BOOL is_text_block_attr(HTMLElement *elem, const WCHAR *name)
{
    return is_block_align_attr(elem, name) || find_hr_length_attr(elem, name) >= 0;
}

const WCHAR *text_block_canonical_attr(HTMLElement *elem, const WCHAR *name, const WCHAR *value, WCHAR *buf, unsigned size)
{
    int idx = find_hr_length_attr(elem, name);

    if(idx < 0)
        return NULL;
    return canonical_length(value, hr_length_attrs[idx].allow_percent, hr_length_attrs[idx].empty, buf, size);
}

BOOL text_block_attr_is_invalid(HTMLElement *elem, const WCHAR *name)
{
    int idx = find_hr_length_attr(elem, name);
    LONGLONG hundredths;
    BOOL percent, ret;
    BSTR str;

    if((idx < 0 && !is_block_align_attr(elem, name)) || FAILED(elem_get_attr_string(elem, name, &str)) || !str)
        return FALSE;
    if(idx < 0)
        ret = *str && !find_keyword(str, align_keywords);
    else
        ret = parse_length(str, hr_length_attrs[idx].allow_percent, &hundredths, &percent) == LENGTH_INVALID;
    SysFreeString(str);
    return ret;
}

HRESULT HTMLHRElement_Create(HTMLDocumentNode *doc, nsIDOMElement *nselem, HTMLElement **elem)
{
    HTMLHRElement *ret;

    ret = calloc(1, sizeof(*ret));
    if(!ret)
        return E_OUTOFMEMORY;

    ret->IHTMLHRElement_iface.lpVtbl = &HTMLHRElementVtbl;
    ret->element.node.vtbl = &HTMLHRElementImplVtbl;

    HTMLElement_Init(&ret->element, doc, nselem, &HTMLHRElement_dispex);

    *elem = &ret->element;
    return S_OK;
}

struct HTMLFontElement {
    HTMLElement element;

    IHTMLFontElement IHTMLFontElement_iface;
};

static inline HTMLFontElement *impl_from_IHTMLFontElement(IHTMLFontElement *iface)
{
    return CONTAINING_RECORD(iface, HTMLFontElement, IHTMLFontElement_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLFontElement, IHTMLFontElement,
                      impl_from_IHTMLFontElement(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLFontElement_put_color(IHTMLFontElement *iface, VARIANT v)
{
    return elem_put_color_attr(&impl_from_IHTMLFontElement(iface)->element, L"color", &v);
}

static HRESULT WINAPI HTMLFontElement_get_color(IHTMLFontElement *iface, VARIANT *p)
{
    return elem_get_color_attr(&impl_from_IHTMLFontElement(iface)->element, L"color", p);
}

static HRESULT WINAPI HTMLFontElement_put_face(IHTMLFontElement *iface, BSTR v)
{
    return elem_put_reflected_string(&impl_from_IHTMLFontElement(iface)->element, L"face", v);
}

static HRESULT WINAPI HTMLFontElement_get_face(IHTMLFontElement *iface, BSTR *p)
{
    return elem_get_attr_string(&impl_from_IHTMLFontElement(iface)->element, L"face", p);
}

static HRESULT WINAPI HTMLFontElement_put_size(IHTMLFontElement *iface, VARIANT v)
{
    return put_font_size_attr(&impl_from_IHTMLFontElement(iface)->element, &v);
}

static HRESULT WINAPI HTMLFontElement_get_size(IHTMLFontElement *iface, VARIANT *p)
{
    return get_font_size_attr(&impl_from_IHTMLFontElement(iface)->element, p);
}

static const IHTMLFontElementVtbl HTMLFontElementVtbl = {
    HTMLFontElement_QueryInterface,
    HTMLFontElement_AddRef,
    HTMLFontElement_Release,
    HTMLFontElement_GetTypeInfoCount,
    HTMLFontElement_GetTypeInfo,
    HTMLFontElement_GetIDsOfNames,
    HTMLFontElement_Invoke,
    HTMLFontElement_put_color,
    HTMLFontElement_get_color,
    HTMLFontElement_put_face,
    HTMLFontElement_get_face,
    HTMLFontElement_put_size,
    HTMLFontElement_get_size
};

static inline HTMLFontElement *HTMLFontElement_from_DispatchEx(DispatchEx *iface)
{
    return CONTAINING_RECORD(iface, HTMLFontElement, element.node.event_target.dispex);
}

static void *HTMLFontElement_query_interface(DispatchEx *dispex, REFIID riid)
{
    HTMLFontElement *This = HTMLFontElement_from_DispatchEx(dispex);

    if(IsEqualGUID(&IID_IHTMLFontElement, riid))
        return &This->IHTMLFontElement_iface;

    return HTMLElement_query_interface(&This->element.node.event_target.dispex, riid);
}

static const NodeImplVtbl HTMLFontElementImplVtbl = {
    .clsid                 = &CLSID_HTMLFontElement,
    .cpc_entries           = HTMLElement_cpc,
    .clone                 = HTMLElement_clone,
    .get_attr_col          = HTMLElement_get_attr_col
};

static const event_target_vtbl_t HTMLFontElement_event_target_vtbl = {
    {
        HTMLELEMENT_DISPEX_VTBL_ENTRIES,
        .query_interface= HTMLFontElement_query_interface,
        .destructor     = HTMLElement_destructor,
        .traverse       = HTMLElement_traverse,
        .unlink         = HTMLElement_unlink
    },
    HTMLELEMENT_EVENT_TARGET_VTBL_ENTRIES,
    .handle_event       = HTMLElement_handle_event
};

static void HTMLFontElement_init_dispex_info(dispex_data_t *info, compat_mode_t mode)
{
    static const dispex_hook_t font_ie9_hooks[] = {
        {DISPID_IHTMLFONTELEMENT_COLOR, ie9_color_hook},
        {DISPID_IHTMLFONTELEMENT_FACE, ie9_face_hook},
        {DISPID_IHTMLFONTELEMENT_SIZE, ie9_font_size_hook},
        {DISPID_UNKNOWN}
    };

    HTMLElement_init_dispex_info(info, mode);
    dispex_info_add_interface(info, IHTMLFontElement_tid, mode >= COMPAT_MODE_IE9 ? font_ie9_hooks : NULL);
}

dispex_static_data_t HTMLFontElement_dispex = {
    .id           = OBJID_HTMLFontElement,
    .prototype_id = OBJID_HTMLElement,
    .vtbl         = &HTMLFontElement_event_target_vtbl.dispex_vtbl,
    .disp_tid     = DispHTMLFontElement_tid,
    .init_info    = HTMLFontElement_init_dispex_info,
};

HRESULT HTMLFontElement_Create(HTMLDocumentNode *doc, nsIDOMElement *nselem, HTMLElement **elem)
{
    HTMLFontElement *ret;

    ret = calloc(1, sizeof(*ret));
    if(!ret)
        return E_OUTOFMEMORY;

    ret->IHTMLFontElement_iface.lpVtbl = &HTMLFontElementVtbl;
    ret->element.node.vtbl = &HTMLFontElementImplVtbl;

    HTMLElement_Init(&ret->element, doc, nselem, &HTMLFontElement_dispex);

    *elem = &ret->element;
    return S_OK;
}

struct HTMLBaseFontElement {
    HTMLElement element;

    IHTMLBaseFontElement IHTMLBaseFontElement_iface;
};

static inline HTMLBaseFontElement *impl_from_IHTMLBaseFontElement(IHTMLBaseFontElement *iface)
{
    return CONTAINING_RECORD(iface, HTMLBaseFontElement, IHTMLBaseFontElement_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLBaseFontElement, IHTMLBaseFontElement,
                      impl_from_IHTMLBaseFontElement(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLBaseFontElement_put_color(IHTMLBaseFontElement *iface, VARIANT v)
{
    return elem_put_color_attr(&impl_from_IHTMLBaseFontElement(iface)->element, L"color", &v);
}

static HRESULT WINAPI HTMLBaseFontElement_get_color(IHTMLBaseFontElement *iface, VARIANT *p)
{
    return elem_get_color_attr(&impl_from_IHTMLBaseFontElement(iface)->element, L"color", p);
}

static HRESULT WINAPI HTMLBaseFontElement_put_face(IHTMLBaseFontElement *iface, BSTR v)
{
    return elem_put_reflected_string(&impl_from_IHTMLBaseFontElement(iface)->element, L"face", v);
}

static HRESULT WINAPI HTMLBaseFontElement_get_face(IHTMLBaseFontElement *iface, BSTR *p)
{
    return elem_get_attr_string(&impl_from_IHTMLBaseFontElement(iface)->element, L"face", p);
}

static HRESULT WINAPI HTMLBaseFontElement_put_size(IHTMLBaseFontElement *iface, LONG v)
{
    WCHAR buf[12];

    swprintf(buf, ARRAY_SIZE(buf), L"%ld", v);
    return elem_put_reflected_string(&impl_from_IHTMLBaseFontElement(iface)->element, L"size", buf);
}

static HRESULT WINAPI HTMLBaseFontElement_get_size(IHTMLBaseFontElement *iface, LONG *p)
{
    HTMLBaseFontElement *This = impl_from_IHTMLBaseFontElement(iface);
    const WCHAR *ptr;
    HRESULT hres;
    BSTR val;

    *p = 3;
    hres = elem_get_attr_string(&This->element, L"size", &val);
    if(FAILED(hres) || !val)
        return hres;

    ptr = val;
    if(*ptr == '+' || *ptr == '-')
        ptr++;
    if(is_digit(*ptr))
        *p = wcstol(val, NULL, 10);
    SysFreeString(val);
    return S_OK;
}

static const IHTMLBaseFontElementVtbl HTMLBaseFontElementVtbl = {
    HTMLBaseFontElement_QueryInterface,
    HTMLBaseFontElement_AddRef,
    HTMLBaseFontElement_Release,
    HTMLBaseFontElement_GetTypeInfoCount,
    HTMLBaseFontElement_GetTypeInfo,
    HTMLBaseFontElement_GetIDsOfNames,
    HTMLBaseFontElement_Invoke,
    HTMLBaseFontElement_put_color,
    HTMLBaseFontElement_get_color,
    HTMLBaseFontElement_put_face,
    HTMLBaseFontElement_get_face,
    HTMLBaseFontElement_put_size,
    HTMLBaseFontElement_get_size
};

static inline HTMLBaseFontElement *HTMLBaseFontElement_from_DispatchEx(DispatchEx *iface)
{
    return CONTAINING_RECORD(iface, HTMLBaseFontElement, element.node.event_target.dispex);
}

static void *HTMLBaseFontElement_query_interface(DispatchEx *dispex, REFIID riid)
{
    HTMLBaseFontElement *This = HTMLBaseFontElement_from_DispatchEx(dispex);

    if(IsEqualGUID(&IID_IHTMLBaseFontElement, riid))
        return &This->IHTMLBaseFontElement_iface;

    return HTMLElement_query_interface(&This->element.node.event_target.dispex, riid);
}

static const NodeImplVtbl HTMLBaseFontElementImplVtbl = {
    .clsid                 = &CLSID_HTMLBaseFontElement,
    .cpc_entries           = HTMLElement_cpc,
    .clone                 = HTMLElement_clone,
    .get_attr_col          = HTMLElement_get_attr_col
};

static const event_target_vtbl_t HTMLBaseFontElement_event_target_vtbl = {
    {
        HTMLELEMENT_DISPEX_VTBL_ENTRIES,
        .query_interface= HTMLBaseFontElement_query_interface,
        .destructor     = HTMLElement_destructor,
        .traverse       = HTMLElement_traverse,
        .unlink         = HTMLElement_unlink
    },
    HTMLELEMENT_EVENT_TARGET_VTBL_ENTRIES,
    .handle_event       = HTMLElement_handle_event
};

static void HTMLBaseFontElement_init_dispex_info(dispex_data_t *info, compat_mode_t mode)
{
    static const dispex_hook_t basefont_ie9_hooks[] = {
        {DISPID_IHTMLBASEFONTELEMENT_COLOR, ie9_color_hook},
        {DISPID_IHTMLBASEFONTELEMENT_FACE, ie9_face_hook},
        {DISPID_UNKNOWN}
    };

    HTMLElement_init_dispex_info(info, mode);
    dispex_info_add_interface(info, IHTMLBaseFontElement_tid, mode >= COMPAT_MODE_IE9 ? basefont_ie9_hooks : NULL);
}

dispex_static_data_t HTMLBaseFontElement_dispex = {
    .id           = OBJID_HTMLBaseFontElement,
    .prototype_id = OBJID_HTMLElement,
    .vtbl         = &HTMLBaseFontElement_event_target_vtbl.dispex_vtbl,
    .disp_tid     = DispHTMLBaseFontElement_tid,
    .init_info    = HTMLBaseFontElement_init_dispex_info,
};

HRESULT HTMLBaseFontElement_Create(HTMLDocumentNode *doc, nsIDOMElement *nselem, HTMLElement **elem)
{
    HTMLBaseFontElement *ret;

    ret = calloc(1, sizeof(*ret));
    if(!ret)
        return E_OUTOFMEMORY;

    ret->IHTMLBaseFontElement_iface.lpVtbl = &HTMLBaseFontElementVtbl;
    ret->element.node.vtbl = &HTMLBaseFontElementImplVtbl;

    HTMLElement_Init(&ret->element, doc, nselem, &HTMLBaseFontElement_dispex);

    *elem = &ret->element;
    return S_OK;
}

struct HTMLBaseElement {
    HTMLElement element;

    IHTMLBaseElement IHTMLBaseElement_iface;
    IHTMLBaseElement2 IHTMLBaseElement2_iface;
};

static inline HTMLBaseElement *impl_from_IHTMLBaseElement(IHTMLBaseElement *iface)
{
    return CONTAINING_RECORD(iface, HTMLBaseElement, IHTMLBaseElement_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLBaseElement, IHTMLBaseElement,
                      impl_from_IHTMLBaseElement(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLBaseElement_put_href(IHTMLBaseElement *iface, BSTR v)
{
    return elem_put_reflected_string(&impl_from_IHTMLBaseElement(iface)->element, L"href", v);
}

static HRESULT WINAPI HTMLBaseElement_get_href(IHTMLBaseElement *iface, BSTR *p)
{
    return elem_get_attr_string(&impl_from_IHTMLBaseElement(iface)->element, L"href", p);
}

static HRESULT WINAPI HTMLBaseElement_put_target(IHTMLBaseElement *iface, BSTR v)
{
    return elem_put_reflected_string(&impl_from_IHTMLBaseElement(iface)->element, L"target", v);
}

static HRESULT WINAPI HTMLBaseElement_get_target(IHTMLBaseElement *iface, BSTR *p)
{
    return elem_get_attr_string(&impl_from_IHTMLBaseElement(iface)->element, L"target", p);
}

static const IHTMLBaseElementVtbl HTMLBaseElementVtbl = {
    HTMLBaseElement_QueryInterface,
    HTMLBaseElement_AddRef,
    HTMLBaseElement_Release,
    HTMLBaseElement_GetTypeInfoCount,
    HTMLBaseElement_GetTypeInfo,
    HTMLBaseElement_GetIDsOfNames,
    HTMLBaseElement_Invoke,
    HTMLBaseElement_put_href,
    HTMLBaseElement_get_href,
    HTMLBaseElement_put_target,
    HTMLBaseElement_get_target
};

static inline HTMLBaseElement *impl_from_IHTMLBaseElement2(IHTMLBaseElement2 *iface)
{
    return CONTAINING_RECORD(iface, HTMLBaseElement, IHTMLBaseElement2_iface);
}

DISPEX_IDISPATCH_IMPL(HTMLBaseElement2, IHTMLBaseElement2,
                      impl_from_IHTMLBaseElement2(iface)->element.node.event_target.dispex)

static HRESULT WINAPI HTMLBaseElement2_put_href(IHTMLBaseElement2 *iface, BSTR v)
{
    return elem_put_reflected_string(&impl_from_IHTMLBaseElement2(iface)->element, L"href", v);
}

static HRESULT WINAPI HTMLBaseElement2_get_href(IHTMLBaseElement2 *iface, BSTR *p)
{
    return get_url_attr(&impl_from_IHTMLBaseElement2(iface)->element, L"href", p);
}

static const IHTMLBaseElement2Vtbl HTMLBaseElement2Vtbl = {
    HTMLBaseElement2_QueryInterface,
    HTMLBaseElement2_AddRef,
    HTMLBaseElement2_Release,
    HTMLBaseElement2_GetTypeInfoCount,
    HTMLBaseElement2_GetTypeInfo,
    HTMLBaseElement2_GetIDsOfNames,
    HTMLBaseElement2_Invoke,
    HTMLBaseElement2_put_href,
    HTMLBaseElement2_get_href
};

static inline HTMLBaseElement *HTMLBaseElement_from_DispatchEx(DispatchEx *iface)
{
    return CONTAINING_RECORD(iface, HTMLBaseElement, element.node.event_target.dispex);
}

static void *HTMLBaseElement_query_interface(DispatchEx *dispex, REFIID riid)
{
    HTMLBaseElement *This = HTMLBaseElement_from_DispatchEx(dispex);

    if(IsEqualGUID(&IID_IHTMLBaseElement, riid))
        return &This->IHTMLBaseElement_iface;
    if(IsEqualGUID(&IID_IHTMLBaseElement2, riid))
        return &This->IHTMLBaseElement2_iface;

    return HTMLElement_query_interface(&This->element.node.event_target.dispex, riid);
}

static const NodeImplVtbl HTMLBaseElementImplVtbl = {
    .clsid                 = &CLSID_HTMLBaseElement,
    .cpc_entries           = HTMLElement_cpc,
    .clone                 = HTMLElement_clone,
    .get_attr_col          = HTMLElement_get_attr_col
};

static const event_target_vtbl_t HTMLBaseElement_event_target_vtbl = {
    {
        HTMLELEMENT_DISPEX_VTBL_ENTRIES,
        .query_interface= HTMLBaseElement_query_interface,
        .destructor     = HTMLElement_destructor,
        .traverse       = HTMLElement_traverse,
        .unlink         = HTMLElement_unlink
    },
    HTMLELEMENT_EVENT_TARGET_VTBL_ENTRIES,
    .handle_event       = HTMLElement_handle_event
};

static void HTMLBaseElement_init_dispex_info(dispex_data_t *info, compat_mode_t mode)
{
    static const dispex_hook_t base_ie9_hooks[] = {
        {DISPID_IHTMLBASEELEMENT_HREF, ie9_href_hook},
        {DISPID_IHTMLBASEELEMENT_TARGET, ie9_target_hook},
        {DISPID_UNKNOWN}
    };

    HTMLElement_init_dispex_info(info, mode);
    dispex_info_add_interface(info, IHTMLBaseElement_tid, mode >= COMPAT_MODE_IE9 ? base_ie9_hooks : NULL);
}

dispex_static_data_t HTMLBaseElement_dispex = {
    .id           = OBJID_HTMLBaseElement,
    .prototype_id = OBJID_HTMLElement,
    .vtbl         = &HTMLBaseElement_event_target_vtbl.dispex_vtbl,
    .disp_tid     = DispHTMLBaseElement_tid,
    .init_info    = HTMLBaseElement_init_dispex_info,
};

HRESULT HTMLBaseElement_Create(HTMLDocumentNode *doc, nsIDOMElement *nselem, HTMLElement **elem)
{
    HTMLBaseElement *ret;

    ret = calloc(1, sizeof(*ret));
    if(!ret)
        return E_OUTOFMEMORY;

    ret->IHTMLBaseElement_iface.lpVtbl = &HTMLBaseElementVtbl;
    ret->IHTMLBaseElement2_iface.lpVtbl = &HTMLBaseElement2Vtbl;
    ret->element.node.vtbl = &HTMLBaseElementImplVtbl;

    HTMLElement_Init(&ret->element, doc, nselem, &HTMLBaseElement_dispex);

    *elem = &ret->element;
    return S_OK;
}
