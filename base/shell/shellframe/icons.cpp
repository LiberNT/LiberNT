/*
 * PROJECT:     LiberNT Shell Frame
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Fluent UI System Icons rendered from their SVG path data
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "shellframe.h"
#include <stdlib.h>
#include <string.h>

namespace ShellFrame
{

struct ICON_SOURCE
{
    LPCWSTR Name;
    const char *Svg;
};

extern const ICON_SOURCE IconSources[];
extern const unsigned IconSourceCount;

struct ICON_CACHE
{
    Gdiplus::GraphicsPath *Path;
    float ViewBox;
};

static ICON_CACHE *Cache;
static SRWLOCK CacheLock = SRWLOCK_INIT;
static LONG StartupCount;
static ULONG_PTR GdiplusToken;

static const char *
SkipSeparators(const char *p)
{
    while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    return p;
}

static BOOL
ReadNumber(const char **cursor, float *value)
{
    const char *p = SkipSeparators(*cursor);
    char buffer[32];
    int length = 0;
    BOOL dot = FALSE, exponent = FALSE;

    if (*p == '-' || *p == '+')
        buffer[length++] = *p++;
    while (length < (int)sizeof(buffer) - 1)
    {
        if (*p >= '0' && *p <= '9')
            buffer[length++] = *p++;
        else if (*p == '.' && !dot && !exponent)
        {
            dot = TRUE;
            buffer[length++] = *p++;
        }
        else if ((*p == 'e' || *p == 'E') && !exponent && length)
        {
            exponent = TRUE;
            buffer[length++] = *p++;
            if (*p == '-' || *p == '+')
                buffer[length++] = *p++;
        }
        else
            break;
    }
    if (!length || (length == 1 && (buffer[0] == '-' || buffer[0] == '+')))
        return FALSE;
    buffer[length] = 0;
    *value = (float)atof(buffer);
    *cursor = p;
    return TRUE;
}

static void
ParsePathData(const char *p, Gdiplus::GraphicsPath *path)
{
    float x = 0.0f, y = 0.0f, startX = 0.0f, startY = 0.0f;
    char command = 0;

    for (;;)
    {
        float a[6];

        p = SkipSeparators(p);
        if (!*p || *p == '"')
            break;
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z'))
            command = *p++;
        switch (command)
        {
            case 'M':
            case 'm':
                if (!ReadNumber(&p, &a[0]) || !ReadNumber(&p, &a[1]))
                    return;
                if (command == 'm')
                {
                    a[0] += x;
                    a[1] += y;
                }
                path->StartFigure();
                x = startX = a[0];
                y = startY = a[1];
                command = command == 'm' ? 'l' : 'L';
                break;
            case 'L':
            case 'l':
                if (!ReadNumber(&p, &a[0]) || !ReadNumber(&p, &a[1]))
                    return;
                if (command == 'l')
                {
                    a[0] += x;
                    a[1] += y;
                }
                path->AddLine(x, y, a[0], a[1]);
                x = a[0];
                y = a[1];
                break;
            case 'H':
            case 'h':
                if (!ReadNumber(&p, &a[0]))
                    return;
                if (command == 'h')
                    a[0] += x;
                path->AddLine(x, y, a[0], y);
                x = a[0];
                break;
            case 'V':
            case 'v':
                if (!ReadNumber(&p, &a[0]))
                    return;
                if (command == 'v')
                    a[0] += y;
                path->AddLine(x, y, x, a[0]);
                y = a[0];
                break;
            case 'C':
            case 'c':
                for (int i = 0; i < 6; i++)
                {
                    if (!ReadNumber(&p, &a[i]))
                        return;
                    if (command == 'c')
                        a[i] += (i & 1) ? y : x;
                }
                path->AddBezier(x, y, a[0], a[1], a[2], a[3], a[4], a[5]);
                x = a[4];
                y = a[5];
                break;
            case 'Z':
            case 'z':
                path->CloseFigure();
                x = startX;
                y = startY;
                command = 0;
                break;
            default:
                return;
        }
    }
}

static float
ParseViewBox(const char *svg)
{
    const char *p = strstr(svg, "viewBox=\"");
    float value[4];

    if (!p)
        return 0.0f;
    p += 9;
    for (int i = 0; i < 4; i++)
    {
        if (!ReadNumber(&p, &value[i]))
            return 0.0f;
    }
    return max(value[2], value[3]);
}

static Gdiplus::GraphicsPath *
BuildPath(const char *svg)
{
    Gdiplus::GraphicsPath *path = new Gdiplus::GraphicsPath(Gdiplus::FillModeWinding);
    const char *p = svg;

    while ((p = strstr(p, " d=\"")) != NULL)
    {
        p += 4;
        ParsePathData(p, path);
    }
    return path;
}

static Gdiplus::GraphicsPath *
LookupIcon(LPCWSTR name, float *viewBox)
{
    for (unsigned i = 0; i < IconSourceCount; i++)
    {
        if (lstrcmpW(IconSources[i].Name, name))
            continue;
        if (!Cache)
            Cache = (ICON_CACHE *)calloc(IconSourceCount, sizeof(*Cache));
        if (!Cache)
            return NULL;
        if (!Cache[i].Path)
        {
            Cache[i].ViewBox = ParseViewBox(IconSources[i].Svg);
            Cache[i].Path = BuildPath(IconSources[i].Svg);
        }
        *viewBox = Cache[i].ViewBox;
        return Cache[i].Path;
    }
    return NULL;
}

Gdiplus::GraphicsPath *
IconPath(LPCWSTR name, float *viewBox)
{
    Gdiplus::GraphicsPath *path;

    AcquireSRWLockExclusive(&CacheLock);
    path = LookupIcon(name, viewBox);
    ReleaseSRWLockExclusive(&CacheLock);
    return path;
}

void
FillIcon(Gdiplus::Graphics &graphics, LPCWSTR name, const Gdiplus::RectF &rc, COLORREF color)
{
    float viewBox = 0.0f;
    Gdiplus::GraphicsPath *path;
    Gdiplus::SolidBrush brush(Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color)));
    Gdiplus::GraphicsState state;
    float scale;

    AcquireSRWLockExclusive(&CacheLock);
    path = LookupIcon(name, &viewBox);
    if (path && viewBox > 0.0f)
    {
        scale = min(rc.Width, rc.Height) / viewBox;
        state = graphics.Save();
        graphics.TranslateTransform(rc.X + (rc.Width - viewBox * scale) / 2.0f,
                                    rc.Y + (rc.Height - viewBox * scale) / 2.0f);
        graphics.ScaleTransform(scale, scale);
        graphics.FillPath(&brush, path);
        graphics.Restore(state);
    }
    ReleaseSRWLockExclusive(&CacheLock);
}

HICON
CreateGlyphIcon(LPCWSTR name, int size, COLORREF color)
{
    Gdiplus::Bitmap bitmap(size, size, PixelFormat32bppARGB);
    HICON icon = NULL;

    {
        Gdiplus::Graphics graphics(&bitmap);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        FillIcon(graphics, name, Gdiplus::RectF(0.0f, 0.0f, (float)size, (float)size), color);
    }
    bitmap.GetHICON(&icon);
    return icon;
}

BOOL
Startup()
{
    BOOL started = TRUE;

    AcquireSRWLockExclusive(&CacheLock);
    if (!StartupCount)
    {
        Gdiplus::GdiplusStartupInput input;
        started = Gdiplus::GdiplusStartup(&GdiplusToken, &input, NULL) == Gdiplus::Ok;
    }
    if (started)
        StartupCount++;
    ReleaseSRWLockExclusive(&CacheLock);
    return started;
}

void
Shutdown()
{
    AcquireSRWLockExclusive(&CacheLock);
    if (StartupCount && !--StartupCount)
    {
        if (Cache)
        {
            for (unsigned i = 0; i < IconSourceCount; i++)
                delete Cache[i].Path;
            free(Cache);
            Cache = NULL;
        }
        Gdiplus::GdiplusShutdown(GdiplusToken);
        GdiplusToken = 0;
    }
    ReleaseSRWLockExclusive(&CacheLock);
}

}
