/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests geometry widening, widened bounds and boolean combination
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <apitest.h>
#include <initguid.h>
#include <d2d1.h>
#include <math.h>

typedef struct _GEOMETRY_SET
{
    ID2D1Factory *Factory;
    ID2D1EllipseGeometry *Ellipse;
    ID2D1EllipseGeometry *Ellipse2;
    ID2D1RectangleGeometry *Rectangle;
    ID2D1TransformedGeometry *Transformed;
    ID2D1StrokeStyle *Round;
    ID2D1StrokeStyle *Bevel;
    D2D1_MATRIX_3X2_F Rotation;
    D2D1_MATRIX_3X2_F Scale;
} GEOMETRY_SET;

static void
CheckWidened(ID2D1Geometry *Geometry, float Width, ID2D1StrokeStyle *Style, const D2D1_MATRIX_3X2_F *Transform,
             float Left, float Top, float Right, float Bottom, float Tolerance, const char *Name)
{
    D2D1_RECT_F Bounds = { 0 };
    HRESULT hr;

    hr = ID2D1Geometry_GetWidenedBounds(Geometry, Width, Style, Transform, 0.25f, &Bounds);
    ok(hr == S_OK, "%s: hr 0x%lx\n", Name, hr);
    ok(fabsf(Bounds.left - Left) <= Tolerance && fabsf(Bounds.top - Top) <= Tolerance &&
       fabsf(Bounds.right - Right) <= Tolerance && fabsf(Bounds.bottom - Bottom) <= Tolerance,
       "%s: bounds (%.3f, %.3f, %.3f, %.3f)\n", Name, Bounds.left, Bounds.top, Bounds.right, Bounds.bottom);
}

static ID2D1PathGeometry *
CreatePolyline(ID2D1Factory *Factory, const D2D1_POINT_2F *Points, UINT Count)
{
    ID2D1PathGeometry *Path = NULL;
    ID2D1GeometrySink *Sink = NULL;

    if (FAILED(ID2D1Factory_CreatePathGeometry(Factory, &Path)))
        return NULL;
    if (SUCCEEDED(ID2D1PathGeometry_Open(Path, &Sink)))
    {
        ID2D1GeometrySink_BeginFigure(Sink, Points[0], D2D1_FIGURE_BEGIN_HOLLOW);
        ID2D1GeometrySink_AddLines(Sink, Points + 1, Count - 1);
        ID2D1GeometrySink_EndFigure(Sink, D2D1_FIGURE_END_OPEN);
        ID2D1GeometrySink_Close(Sink);
        ID2D1GeometrySink_Release(Sink);
    }
    return Path;
}

static ID2D1StrokeStyle *
CreateStyle(ID2D1Factory *Factory, D2D1_CAP_STYLE Cap, D2D1_LINE_JOIN Join)
{
    D2D1_STROKE_STYLE_PROPERTIES Props = { Cap, Cap, Cap, Join, 10.0f, D2D1_DASH_STYLE_SOLID, 0.0f };
    ID2D1StrokeStyle *Style = NULL;

    ok(ID2D1Factory_CreateStrokeStyle(Factory, &Props, NULL, 0, &Style) == S_OK, "stroke style\n");
    return Style;
}

static void
TestWidenedBounds(GEOMETRY_SET *Set)
{
    static const D2D1_POINT_2F Corner[] = { { 0.0f, 0.0f }, { 10.0f, 0.0f }, { 10.0f, 10.0f } };
    ID2D1Geometry *Rectangle = (ID2D1Geometry *)Set->Rectangle, *Ellipse = (ID2D1Geometry *)Set->Ellipse;
    ID2D1Geometry *Transformed = (ID2D1Geometry *)Set->Transformed;
    ID2D1PathGeometry *Polyline;
    ID2D1StrokeStyle *Style;

    /* A corner takes its join; the transform applies to the widened outline. */
    CheckWidened(Rectangle, 10.0f, NULL, NULL, -5.0f, -5.0f, 105.0f, 55.0f, 0.01f, "rect miter");
    CheckWidened(Rectangle, 10.0f, Set->Round, NULL, -5.0f, -5.0f, 105.0f, 55.0f, 0.01f, "rect round");
    CheckWidened(Rectangle, 10.0f, Set->Bevel, NULL, -5.0f, -5.0f, 105.0f, 55.0f, 0.01f, "rect bevel");
    CheckWidened(Rectangle, 10.0f, NULL, &Set->Rotation, -31.830f, -6.830f, 93.433f, 100.131f, 0.01f, "rect rot miter");
    CheckWidened(Rectangle, 10.0f, Set->Round, &Set->Rotation, -30.001f, -5.001f, 91.603f, 98.302f, 0.01f,
                 "rect rot round");
    CheckWidened(Rectangle, 10.0f, NULL, &Set->Scale, 0.0f, 17.5f, 220.0f, 47.5f, 0.01f, "rect scale");

    /* A curve has no joins inside it, whatever the join style. */
    CheckWidened(Ellipse, 10.0f, NULL, NULL, 45.0f, 65.0f, 155.0f, 135.0f, 0.01f, "ellipse");
    CheckWidened(Ellipse, 0.0f, NULL, NULL, 50.0f, 70.0f, 150.0f, 130.0f, 0.01f, "ellipse w0");
    CheckWidened(Ellipse, 10.0f, NULL, &Set->Scale, 100.0f, 52.5f, 320.0f, 87.5f, 0.01f, "ellipse scale");
    CheckWidened(Ellipse, 10.0f, NULL, &Set->Rotation, -14.207f, 95.554f, 87.412f, 177.651f, 0.25f, "ellipse rot");

    /* A transformed geometry is widened after its own transform. */
    CheckWidened(Transformed, 10.0f, NULL, NULL, -14.207f, 95.554f, 87.412f, 177.651f, 0.25f, "xformed ellipse");
    CheckWidened(Transformed, 10.0f, NULL, &Set->Scale, -18.415f, 67.777f, 184.825f, 108.825f, 0.5f,
                 "xformed ellipse scale");

    /* Open figures take their caps. */
    Polyline = CreatePolyline(Set->Factory, Corner, ARRAYSIZE(Corner));
    ok(Polyline != NULL, "polyline\n");
    if (!Polyline)
        return;
    CheckWidened((ID2D1Geometry *)Polyline, 2.0f, NULL, NULL, 0.0f, -1.0f, 11.0f, 10.0f, 0.01f, "corner flat");
    Style = CreateStyle(Set->Factory, D2D1_CAP_STYLE_SQUARE, D2D1_LINE_JOIN_MITER);
    if (Style)
    {
        CheckWidened((ID2D1Geometry *)Polyline, 2.0f, Style, NULL, -1.0f, -1.0f, 11.0f, 11.0f, 0.01f, "corner square");
        ID2D1StrokeStyle_Release(Style);
    }
    Style = CreateStyle(Set->Factory, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
    if (Style)
    {
        CheckWidened((ID2D1Geometry *)Polyline, 2.0f, Style, NULL, -1.0f, -1.0f, 11.0f, 11.0f, 0.01f, "corner round");
        ID2D1StrokeStyle_Release(Style);
    }
    ID2D1PathGeometry_Release(Polyline);
}

static void
CheckCombine(ID2D1Factory *Factory, ID2D1Geometry *First, ID2D1Geometry *Second, D2D1_COMBINE_MODE Mode,
             UINT32 ExpectedFigures, float ExpectedArea, float AreaTolerance, const char *ExpectedInside,
             const char *Name)
{
    static const D2D1_POINT_2F Points[] =
        { { 60.0f, 100.0f }, { 120.0f, 100.0f }, { 180.0f, 100.0f }, { 120.0f, 75.0f } };
    ID2D1PathGeometry *Path = NULL;
    ID2D1GeometrySink *Sink = NULL;
    char Inside[ARRAYSIZE(Points) + 1] = { 0 };
    UINT32 Figures = 0;
    float Area = -1.0f;
    BOOL Contains;
    HRESULT hr;
    UINT i;

    hr = ID2D1Factory_CreatePathGeometry(Factory, &Path);
    ok(hr == S_OK, "%s: path hr 0x%lx\n", Name, hr);
    if (FAILED(hr))
        return;
    hr = ID2D1PathGeometry_Open(Path, &Sink);
    ok(hr == S_OK, "%s: open hr 0x%lx\n", Name, hr);
    if (SUCCEEDED(hr))
    {
        hr = ID2D1Geometry_CombineWithGeometry(First, Second, Mode, NULL, 0.25f, (ID2D1SimplifiedGeometrySink *)Sink);
        ok(hr == S_OK, "%s: hr 0x%lx\n", Name, hr);
        hr = ID2D1GeometrySink_Close(Sink);
        ok(hr == S_OK, "%s: close hr 0x%lx\n", Name, hr);
        ID2D1GeometrySink_Release(Sink);
        ok(ID2D1PathGeometry_GetFigureCount(Path, &Figures) == S_OK, "%s: figure count\n", Name);
        ok(Figures == ExpectedFigures, "%s: %u figures\n", Name, Figures);
        hr = ID2D1PathGeometry_ComputeArea(Path, NULL, 0.25f, &Area);
        ok(hr == S_OK, "%s: area hr 0x%lx\n", Name, hr);
        ok(fabsf(Area - ExpectedArea) <= AreaTolerance, "%s: area %.2f\n", Name, Area);
        if (ExpectedInside)
        {
            for (i = 0; i < ARRAYSIZE(Points); ++i)
            {
                Contains = FALSE;
                ID2D1PathGeometry_FillContainsPoint(Path, Points[i], NULL, 0.25f, &Contains);
                Inside[i] = Contains ? '1' : '0';
            }
            ok(!strcmp(Inside, ExpectedInside), "%s: inside %s\n", Name, Inside);
        }
    }
    ID2D1PathGeometry_Release(Path);
}

static void
TestCombine(GEOMETRY_SET *Set)
{
    ID2D1Geometry *First = (ID2D1Geometry *)Set->Ellipse, *Second = (ID2D1Geometry *)Set->Ellipse2;
    D2D1_RECT_F Left = { 0.0f, 0.0f, 10.0f, 10.0f }, Right = { 10.0f, 0.0f, 20.0f, 10.0f };
    ID2D1RectangleGeometry *R1 = NULL, *R2 = NULL;

    /* Two overlapping ellipses; Windows 11 reports these areas for the flattened result. */
    CheckCombine(Set->Factory, First, Second, D2D1_COMBINE_MODE_UNION, 1, 7018.90f, 15.0f, "1111", "union");
    CheckCombine(Set->Factory, First, Second, D2D1_COMBINE_MODE_INTERSECT, 1, 2368.03f, 15.0f, "0101", "intersect");
    CheckCombine(Set->Factory, First, Second, D2D1_COMBINE_MODE_XOR, 2, 4650.87f, 15.0f, "1010", "xor");
    CheckCombine(Set->Factory, First, Second, D2D1_COMBINE_MODE_EXCLUDE, 1, 2325.43f, 15.0f, "1000", "exclude");

    /* Two squares sharing an edge. */
    ok(ID2D1Factory_CreateRectangleGeometry(Set->Factory, &Left, &R1) == S_OK, "rectangle 1\n");
    ok(ID2D1Factory_CreateRectangleGeometry(Set->Factory, &Right, &R2) == S_OK, "rectangle 2\n");
    if (R1 && R2)
    {
        CheckCombine(Set->Factory, (ID2D1Geometry *)R1, (ID2D1Geometry *)R2, D2D1_COMBINE_MODE_UNION, 1, 200.0f, 0.01f,
                     NULL, "squares union");
        CheckCombine(Set->Factory, (ID2D1Geometry *)R1, (ID2D1Geometry *)R2, D2D1_COMBINE_MODE_INTERSECT, 1, 0.0f,
                     0.01f, NULL, "squares intersect");
    }
    if (R1)
        ID2D1RectangleGeometry_Release(R1);
    if (R2)
        ID2D1RectangleGeometry_Release(R2);
}

#define MAX_FIGURES 4

typedef struct _RECORD_SINK
{
    ID2D1SimplifiedGeometrySink ID2D1SimplifiedGeometrySink_iface;
    UINT FillModeCalls;
    D2D1_FILL_MODE FillMode;
    BOOL FillModeAfterFigure;
    UINT CloseCalls;
    UINT Figures;
    D2D1_FIGURE_BEGIN Begin[MAX_FIGURES];
    D2D1_FIGURE_END End[MAX_FIGURES];
    UINT Lines[MAX_FIGURES];
    UINT Beziers[MAX_FIGURES];
    D2D1_POINT_2F Start[MAX_FIGURES];
    D2D1_POINT_2F Last[MAX_FIGURES];
    float SignedArea[MAX_FIGURES];
} RECORD_SINK;

static RECORD_SINK *
RecordSinkFromIface(ID2D1SimplifiedGeometrySink *Iface)
{
    return CONTAINING_RECORD(Iface, RECORD_SINK, ID2D1SimplifiedGeometrySink_iface);
}

static HRESULT STDMETHODCALLTYPE
RecordSink_QueryInterface(ID2D1SimplifiedGeometrySink *Iface, REFIID Riid, void **Out)
{
    *Out = Iface;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE
RecordSink_AddRef(ID2D1SimplifiedGeometrySink *Iface)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE
RecordSink_Release(ID2D1SimplifiedGeometrySink *Iface)
{
    return 1;
}

static void STDMETHODCALLTYPE
RecordSink_SetFillMode(ID2D1SimplifiedGeometrySink *Iface, D2D1_FILL_MODE Mode)
{
    RECORD_SINK *Sink = RecordSinkFromIface(Iface);

    Sink->FillModeCalls++;
    Sink->FillMode = Mode;
    if (Sink->Figures)
        Sink->FillModeAfterFigure = TRUE;
}

static void STDMETHODCALLTYPE
RecordSink_SetSegmentFlags(ID2D1SimplifiedGeometrySink *Iface, D2D1_PATH_SEGMENT Flags)
{
}

static void STDMETHODCALLTYPE
RecordSink_BeginFigure(ID2D1SimplifiedGeometrySink *Iface, D2D1_POINT_2F Start, D2D1_FIGURE_BEGIN Begin)
{
    RECORD_SINK *Sink = RecordSinkFromIface(Iface);

    if (Sink->Figures < MAX_FIGURES)
    {
        Sink->Begin[Sink->Figures] = Begin;
        Sink->Start[Sink->Figures] = Start;
        Sink->Last[Sink->Figures] = Start;
    }
    Sink->Figures++;
}

static void STDMETHODCALLTYPE
RecordSink_AddLines(ID2D1SimplifiedGeometrySink *Iface, const D2D1_POINT_2F *Points, UINT32 Count)
{
    RECORD_SINK *Sink = RecordSinkFromIface(Iface);
    UINT Figure = Sink->Figures - 1;
    UINT32 i;

    if (!Sink->Figures || Figure >= MAX_FIGURES)
        return;
    for (i = 0; i < Count; ++i)
    {
        D2D1_POINT_2F *Last = &Sink->Last[Figure];

        Sink->SignedArea[Figure] += (Last->x * Points[i].y - Points[i].x * Last->y) / 2.0f;
        *Last = Points[i];
    }
    Sink->Lines[Figure] += Count;
}

static void STDMETHODCALLTYPE
RecordSink_AddBeziers(ID2D1SimplifiedGeometrySink *Iface, const D2D1_BEZIER_SEGMENT *Beziers, UINT32 Count)
{
    RECORD_SINK *Sink = RecordSinkFromIface(Iface);

    if (Sink->Figures && Sink->Figures <= MAX_FIGURES)
    {
        Sink->Beziers[Sink->Figures - 1] += Count;
        Sink->Last[Sink->Figures - 1] = Beziers[Count - 1].point3;
    }
}

static void STDMETHODCALLTYPE
RecordSink_EndFigure(ID2D1SimplifiedGeometrySink *Iface, D2D1_FIGURE_END End)
{
    RECORD_SINK *Sink = RecordSinkFromIface(Iface);

    if (Sink->Figures && Sink->Figures <= MAX_FIGURES)
        Sink->End[Sink->Figures - 1] = End;
}

static HRESULT STDMETHODCALLTYPE
RecordSink_Close(ID2D1SimplifiedGeometrySink *Iface)
{
    RecordSinkFromIface(Iface)->CloseCalls++;
    return S_OK;
}

static ID2D1SimplifiedGeometrySinkVtbl RecordSinkVtbl =
{
    RecordSink_QueryInterface,
    RecordSink_AddRef,
    RecordSink_Release,
    RecordSink_SetFillMode,
    RecordSink_SetSegmentFlags,
    RecordSink_BeginFigure,
    RecordSink_AddLines,
    RecordSink_AddBeziers,
    RecordSink_EndFigure,
    RecordSink_Close,
};

static void
RecordCombine(ID2D1Factory *Factory, D2D1_RECT_F First, D2D1_RECT_F Second, D2D1_COMBINE_MODE Mode,
              RECORD_SINK *Sink, const char *Name)
{
    ID2D1RectangleGeometry *A = NULL, *B = NULL;
    HRESULT hr;
    UINT i;

    memset(Sink, 0, sizeof(*Sink));
    Sink->ID2D1SimplifiedGeometrySink_iface.lpVtbl = &RecordSinkVtbl;
    ok(ID2D1Factory_CreateRectangleGeometry(Factory, &First, &A) == S_OK, "%s: first\n", Name);
    ok(ID2D1Factory_CreateRectangleGeometry(Factory, &Second, &B) == S_OK, "%s: second\n", Name);
    if (A && B)
    {
        hr = ID2D1RectangleGeometry_CombineWithGeometry(A, (ID2D1Geometry *)B, Mode, NULL, 0.25f,
                                                        &Sink->ID2D1SimplifiedGeometrySink_iface);
        ok(hr == S_OK, "%s: hr 0x%lx\n", Name, hr);
    }
    if (A)
        ID2D1RectangleGeometry_Release(A);
    if (B)
        ID2D1RectangleGeometry_Release(B);

    ok(Sink->FillModeCalls == 1, "%s: %u fill mode calls\n", Name, Sink->FillModeCalls);
    ok(Sink->FillMode == D2D1_FILL_MODE_ALTERNATE, "%s: fill mode %u\n", Name, Sink->FillMode);
    ok(!Sink->FillModeAfterFigure, "%s: fill mode set after a figure\n", Name);
    ok(Sink->CloseCalls == 0, "%s: %u close calls\n", Name, Sink->CloseCalls);
    for (i = 0; i < Sink->Figures && i < MAX_FIGURES; ++i)
    {
        ok(Sink->Beziers[i] == 0, "%s: figure %u has %u beziers\n", Name, i, Sink->Beziers[i]);
        if (Sink->Begin[i] == D2D1_FIGURE_BEGIN_FILLED)
        {
            ok(Sink->End[i] == D2D1_FIGURE_END_CLOSED, "%s: figure %u end %u\n", Name, i, Sink->End[i]);
            ok(Sink->Last[i].x == Sink->Start[i].x && Sink->Last[i].y == Sink->Start[i].y,
               "%s: figure %u ends at (%.3f, %.3f), starts at (%.3f, %.3f)\n", Name, i, Sink->Last[i].x,
               Sink->Last[i].y, Sink->Start[i].x, Sink->Start[i].y);
        }
    }
}

static void
TestCombineOutput(ID2D1Factory *Factory)
{
    D2D1_RECT_F Left = { 0.0f, 0.0f, 10.0f, 10.0f }, Right = { 10.0f, 0.0f, 20.0f, 10.0f };
    D2D1_RECT_F Far = { 30.0f, 0.0f, 40.0f, 10.0f }, Inner = { 2.0f, 2.0f, 5.0f, 5.0f };
    D2D1_RECT_F Below = { 5.0f, 10.0f, 15.0f, 20.0f };
    RECORD_SINK Sink;

    /* Operands touching along an edge intersect in that edge. */
    RecordCombine(Factory, Left, Right, D2D1_COMBINE_MODE_INTERSECT, &Sink, "adjacent intersect");
    ok(Sink.Figures == 1, "adjacent intersect: %u figures\n", Sink.Figures);
    ok(Sink.Begin[0] == D2D1_FIGURE_BEGIN_HOLLOW, "adjacent intersect: begin %u\n", Sink.Begin[0]);
    ok(Sink.End[0] == D2D1_FIGURE_END_OPEN, "adjacent intersect: end %u\n", Sink.End[0]);
    ok(Sink.Lines[0] == 1, "adjacent intersect: %u lines\n", Sink.Lines[0]);
    ok(Sink.Start[0].x == 10.0f && Sink.Last[0].x == 10.0f && fabsf(Sink.Start[0].y - Sink.Last[0].y) == 10.0f,
       "adjacent intersect: (%.3f, %.3f) to (%.3f, %.3f)\n", Sink.Start[0].x, Sink.Start[0].y, Sink.Last[0].x,
       Sink.Last[0].y);

    RecordCombine(Factory, Left, Right, D2D1_COMBINE_MODE_UNION, &Sink, "adjacent union");
    ok(Sink.Figures == 1, "adjacent union: %u figures\n", Sink.Figures);
    ok(Sink.Lines[0] == 6, "adjacent union: %u lines\n", Sink.Lines[0]);
    ok(Sink.Start[0].x == 0.0f && Sink.Start[0].y == 0.0f, "adjacent union: start (%.3f, %.3f)\n", Sink.Start[0].x,
       Sink.Start[0].y);
    RecordCombine(Factory, Left, Right, D2D1_COMBINE_MODE_EXCLUDE, &Sink, "adjacent exclude");
    ok(Sink.Figures == 1 && Sink.Lines[0] == 4, "adjacent exclude: %u figures, %u lines\n", Sink.Figures,
       Sink.Lines[0]);
    RecordCombine(Factory, Left, Right, D2D1_COMBINE_MODE_XOR, &Sink, "adjacent xor");
    ok(Sink.Figures == 1 && Sink.Lines[0] == 6, "adjacent xor: %u figures, %u lines\n", Sink.Figures, Sink.Lines[0]);

    RecordCombine(Factory, Left, Far, D2D1_COMBINE_MODE_INTERSECT, &Sink, "disjoint intersect");
    ok(Sink.Figures == 0, "disjoint intersect: %u figures\n", Sink.Figures);
    RecordCombine(Factory, Left, Far, D2D1_COMBINE_MODE_UNION, &Sink, "disjoint union");
    ok(Sink.Figures == 2 && Sink.Lines[0] == 4 && Sink.Lines[1] == 4, "disjoint union: %u figures, %u and %u lines\n",
       Sink.Figures, Sink.Lines[0], Sink.Lines[1]);

    /* A hole runs against its outline. */
    RecordCombine(Factory, Left, Inner, D2D1_COMBINE_MODE_EXCLUDE, &Sink, "nested exclude");
    ok(Sink.Figures == 2, "nested exclude: %u figures\n", Sink.Figures);
    ok(fabsf(Sink.SignedArea[0]) + fabsf(Sink.SignedArea[1]) == 109.0f, "nested exclude: areas %.3f and %.3f\n",
       Sink.SignedArea[0], Sink.SignedArea[1]);
    ok(Sink.SignedArea[0] * Sink.SignedArea[1] < 0.0f, "nested exclude: areas %.3f and %.3f\n", Sink.SignedArea[0],
       Sink.SignedArea[1]);
    RecordCombine(Factory, Left, Inner, D2D1_COMBINE_MODE_INTERSECT, &Sink, "nested intersect");
    ok(Sink.Figures == 1 && fabsf(Sink.SignedArea[0]) == 9.0f, "nested intersect: %u figures, area %.3f\n",
       Sink.Figures, Sink.SignedArea[0]);

    RecordCombine(Factory, Left, Below, D2D1_COMBINE_MODE_UNION, &Sink, "t-junction union");
    ok(Sink.Figures == 1 && Sink.Lines[0] == 8, "t-junction union: %u figures, %u lines\n", Sink.Figures,
       Sink.Lines[0]);
    ok(fabsf(Sink.SignedArea[0]) == 200.0f, "t-junction union: area %.3f\n", Sink.SignedArea[0]);

    RecordCombine(Factory, Left, Left, D2D1_COMBINE_MODE_UNION, &Sink, "identical union");
    ok(Sink.Figures == 1 && Sink.Lines[0] == 4, "identical union: %u figures, %u lines\n", Sink.Figures, Sink.Lines[0]);
    RecordCombine(Factory, Left, Left, D2D1_COMBINE_MODE_EXCLUDE, &Sink, "identical exclude");
    ok(Sink.Figures == 0, "identical exclude: %u figures\n", Sink.Figures);
}

static void
CheckWiden(ID2D1Factory *Factory, ID2D1Geometry *Geometry, float Width, ID2D1StrokeStyle *Style,
           const D2D1_MATRIX_3X2_F *Transform, float ExpectedArea, float AreaTolerance, const char *ExpectedInside,
           const char *Name)
{
    static const D2D1_POINT_2F Points[] =
        { { 50.0f, 25.0f }, { 0.0f, 0.0f }, { 2.0f, 2.0f }, { 100.0f, 25.0f }, { 50.0f, 50.0f }, { 103.0f, 25.0f } };
    char Inside[ARRAYSIZE(Points) + 1] = { 0 };
    ID2D1PathGeometry *Path = NULL;
    ID2D1GeometrySink *Sink = NULL;
    RECORD_SINK Record;
    float Area = -1.0f;
    BOOL Contains;
    HRESULT hr;
    UINT i;

    memset(&Record, 0, sizeof(Record));
    Record.ID2D1SimplifiedGeometrySink_iface.lpVtbl = &RecordSinkVtbl;
    hr = ID2D1Geometry_Widen(Geometry, Width, Style, Transform, 0.25f, &Record.ID2D1SimplifiedGeometrySink_iface);
    ok(hr == S_OK, "%s: hr 0x%lx\n", Name, hr);
    ok(Record.FillModeCalls == 1, "%s: %u fill mode calls\n", Name, Record.FillModeCalls);
    ok(!Record.FillModeAfterFigure, "%s: fill mode set after a figure\n", Name);
    ok(Record.CloseCalls == 0, "%s: %u close calls\n", Name, Record.CloseCalls);
    ok(Record.Figures != 0, "%s: no figures\n", Name);

    hr = ID2D1Factory_CreatePathGeometry(Factory, &Path);
    ok(hr == S_OK, "%s: path hr 0x%lx\n", Name, hr);
    if (FAILED(hr))
        return;
    hr = ID2D1PathGeometry_Open(Path, &Sink);
    ok(hr == S_OK, "%s: open hr 0x%lx\n", Name, hr);
    if (SUCCEEDED(hr))
    {
        hr = ID2D1Geometry_Widen(Geometry, Width, Style, Transform, 0.25f, (ID2D1SimplifiedGeometrySink *)Sink);
        ok(hr == S_OK, "%s: widen into path hr 0x%lx\n", Name, hr);
        hr = ID2D1GeometrySink_Close(Sink);
        ok(hr == S_OK, "%s: close hr 0x%lx\n", Name, hr);
        ID2D1GeometrySink_Release(Sink);
        hr = ID2D1PathGeometry_ComputeArea(Path, NULL, 0.25f, &Area);
        ok(hr == S_OK, "%s: area hr 0x%lx\n", Name, hr);
        ok(fabsf(Area - ExpectedArea) <= AreaTolerance, "%s: area %.2f\n", Name, Area);
        if (ExpectedInside)
        {
            for (i = 0; i < ARRAYSIZE(Points); ++i)
            {
                Contains = FALSE;
                ID2D1PathGeometry_FillContainsPoint(Path, Points[i], NULL, 0.25f, &Contains);
                Inside[i] = Contains ? '1' : '0';
            }
            ok(!strcmp(Inside, ExpectedInside), "%s: inside %s\n", Name, Inside);
        }
    }
    ID2D1PathGeometry_Release(Path);
}

static void
TestWiden(GEOMETRY_SET *Set)
{
    static const D2D1_POINT_2F Open[] = { { 0.0f, 0.0f }, { 100.0f, 0.0f }, { 100.0f, 50.0f } };
    static const D2D1_POINT_2F Bow[] = { { 0.0f, 0.0f }, { 100.0f, 50.0f }, { 100.0f, 0.0f }, { 0.0f, 50.0f } };
    D2D1_ELLIPSE E = { { 50.0f, 25.0f }, 50.0f, 25.0f };
    ID2D1Geometry *Rectangle = (ID2D1Geometry *)Set->Rectangle;
    ID2D1TransformedGeometry *Transformed = NULL;
    ID2D1PathGeometry *Polyline, *Bowtie = NULL;
    ID2D1EllipseGeometry *Ellipse = NULL;
    ID2D1GeometrySink *Sink = NULL;
    ID2D1StrokeStyle *Style;

    /* Windows 11 reports these areas and hits for the widened geometries. */
    CheckWiden(Set->Factory, Rectangle, 10.0f, NULL, NULL, 3000.0f, 0.1f, "011111", "rect miter");
    CheckWiden(Set->Factory, Rectangle, 10.0f, Set->Round, NULL, 2976.55f, 3.0f, "011111", "rect round");
    CheckWiden(Set->Factory, Rectangle, 10.0f, Set->Bevel, NULL, 2950.0f, 0.1f, "011111", "rect bevel");
    CheckWiden(Set->Factory, Rectangle, 10.0f, NULL, &Set->Scale, 3000.0f, 0.1f, "000000", "rect scale");
    CheckWiden(Set->Factory, Rectangle, 0.0f, NULL, NULL, 0.0f, 0.1f, NULL, "rect w0");
    CheckWiden(Set->Factory, Rectangle, 60.0f, NULL, NULL, 17600.0f, 0.1f, "111111", "rect w60");

    ok(ID2D1Factory_CreateEllipseGeometry(Set->Factory, &E, &Ellipse) == S_OK, "widen ellipse\n");
    if (Ellipse)
    {
        CheckWiden(Set->Factory, (ID2D1Geometry *)Ellipse, 10.0f, NULL, NULL, 2415.01f, 15.0f, "000111", "ellipse");
        ok(ID2D1Factory_CreateTransformedGeometry(Set->Factory, (ID2D1Geometry *)Ellipse, &Set->Scale, &Transformed) ==
           S_OK, "widen transformed\n");
        if (Transformed)
        {
            CheckWiden(Set->Factory, (ID2D1Geometry *)Transformed, 1.0f, NULL, NULL, 408.64f, 5.0f, "000000",
                       "xformed ellipse");
            ID2D1TransformedGeometry_Release(Transformed);
        }
        ID2D1EllipseGeometry_Release(Ellipse);
    }

    Polyline = CreatePolyline(Set->Factory, Open, ARRAYSIZE(Open));
    ok(Polyline != NULL, "widen polyline\n");
    if (Polyline)
    {
        CheckWiden(Set->Factory, (ID2D1Geometry *)Polyline, 10.0f, NULL, NULL, 1500.0f, 0.1f, "011101", "open flat");
        Style = CreateStyle(Set->Factory, D2D1_CAP_STYLE_SQUARE, D2D1_LINE_JOIN_MITER);
        if (Style)
        {
            CheckWiden(Set->Factory, (ID2D1Geometry *)Polyline, 10.0f, Style, NULL, 1600.0f, 0.1f, "011101",
                       "open square");
            ID2D1StrokeStyle_Release(Style);
        }
        Style = CreateStyle(Set->Factory, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
        if (Style)
        {
            CheckWiden(Set->Factory, (ID2D1Geometry *)Polyline, 10.0f, Style, NULL, 1570.69f, 5.0f, "011101",
                       "open round");
            ID2D1StrokeStyle_Release(Style);
        }
        Style = CreateStyle(Set->Factory, D2D1_CAP_STYLE_TRIANGLE, D2D1_LINE_JOIN_BEVEL);
        if (Style)
        {
            CheckWiden(Set->Factory, (ID2D1Geometry *)Polyline, 10.0f, Style, NULL, 1537.5f, 0.1f, "011101",
                       "open triangle");
            ID2D1StrokeStyle_Release(Style);
        }
        ID2D1PathGeometry_Release(Polyline);
    }

    /* A self-crossing figure. */
    ok(ID2D1Factory_CreatePathGeometry(Set->Factory, &Bowtie) == S_OK, "bowtie\n");
    if (Bowtie && SUCCEEDED(ID2D1PathGeometry_Open(Bowtie, &Sink)))
    {
        ID2D1GeometrySink_BeginFigure(Sink, Bow[0], D2D1_FIGURE_BEGIN_FILLED);
        ID2D1GeometrySink_AddLines(Sink, Bow + 1, ARRAYSIZE(Bow) - 1);
        ID2D1GeometrySink_EndFigure(Sink, D2D1_FIGURE_END_CLOSED);
        ID2D1GeometrySink_Close(Sink);
        ID2D1GeometrySink_Release(Sink);
        CheckWiden(Set->Factory, (ID2D1Geometry *)Bowtie, 10.0f, NULL, NULL, 3111.07f, 0.5f, "111101", "bowtie");
    }
    if (Bowtie)
        ID2D1PathGeometry_Release(Bowtie);
}

START_TEST(GeometryOps)
{
    D2D1_ELLIPSE E1 = { { 100.0f, 100.0f }, 50.0f, 30.0f }, E2 = { { 140.0f, 100.0f }, 50.0f, 30.0f };
    D2D1_RECT_F Rect = { 0.0f, 0.0f, 100.0f, 50.0f };
    float Angle = 30.0f * 3.14159265f / 180.0f;
    GEOMETRY_SET Set = { 0 };
    HRESULT hr;

    Set.Rotation._11 = cosf(Angle);
    Set.Rotation._12 = sinf(Angle);
    Set.Rotation._21 = -sinf(Angle);
    Set.Rotation._22 = cosf(Angle);
    Set.Scale._11 = 2.0f;
    Set.Scale._22 = 0.5f;
    Set.Scale._31 = 10.0f;
    Set.Scale._32 = 20.0f;

    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory, NULL, (void **)&Set.Factory);
    ok(hr == S_OK, "factory hr 0x%lx\n", hr);
    if (!Set.Factory)
        return;
    ok(ID2D1Factory_CreateEllipseGeometry(Set.Factory, &E1, &Set.Ellipse) == S_OK, "ellipse\n");
    ok(ID2D1Factory_CreateEllipseGeometry(Set.Factory, &E2, &Set.Ellipse2) == S_OK, "ellipse 2\n");
    ok(ID2D1Factory_CreateRectangleGeometry(Set.Factory, &Rect, &Set.Rectangle) == S_OK, "rectangle\n");
    if (Set.Ellipse)
        ok(ID2D1Factory_CreateTransformedGeometry(Set.Factory, (ID2D1Geometry *)Set.Ellipse, &Set.Rotation,
                                                  &Set.Transformed) == S_OK, "transformed\n");
    Set.Round = CreateStyle(Set.Factory, D2D1_CAP_STYLE_FLAT, D2D1_LINE_JOIN_ROUND);
    Set.Bevel = CreateStyle(Set.Factory, D2D1_CAP_STYLE_FLAT, D2D1_LINE_JOIN_BEVEL);

    if (Set.Ellipse && Set.Ellipse2 && Set.Rectangle && Set.Transformed && Set.Round && Set.Bevel)
    {
        TestWidenedBounds(&Set);
        TestCombine(&Set);
        TestWiden(&Set);
    }
    TestCombineOutput(Set.Factory);

    if (Set.Bevel)
        ID2D1StrokeStyle_Release(Set.Bevel);
    if (Set.Round)
        ID2D1StrokeStyle_Release(Set.Round);
    if (Set.Transformed)
        ID2D1TransformedGeometry_Release(Set.Transformed);
    if (Set.Rectangle)
        ID2D1RectangleGeometry_Release(Set.Rectangle);
    if (Set.Ellipse2)
        ID2D1EllipseGeometry_Release(Set.Ellipse2);
    if (Set.Ellipse)
        ID2D1EllipseGeometry_Release(Set.Ellipse);
    ID2D1Factory_Release(Set.Factory);
}
