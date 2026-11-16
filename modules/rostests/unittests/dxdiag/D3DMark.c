/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the rotating mark of the DirectX Diagnostic Tool's Direct3D tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <math.h>

#include "d3dmark.h"

#define VIEW_WIDTH 240
#define VIEW_HEIGHT 180
#define FRAME_STEP_MS 80
#define FRAME_LAST_MS 12000
#define MARK_BLUE 0xff0161b6
#define MARK_WIDTH 407.0
#define MARK_HEIGHT 356.0
#define MARK_STROKE 88.0

static DWORD Painted[VIEW_HEIGHT][VIEW_WIDTH];
static FLOAT PaintedRhw[VIEW_HEIGHT][VIEW_WIDTH];
static FLOAT NearestRhw[VIEW_HEIGHT][VIEW_WIDTH];

static FLOAT Edge(const D3DTEST_VERTEX *A, const D3DTEST_VERTEX *B, FLOAT x, FLOAT y)
{
    return (B->x - A->x) * (y - A->y) - (B->y - A->y) * (x - A->x);
}

static VOID RasterizeTriangle(const D3DTEST_VERTEX *Triangle)
{
    FLOAT Area = Edge(&Triangle[0], &Triangle[1], Triangle[2].x, Triangle[2].y);
    LONG Left = (LONG)floor(min(Triangle[0].x, min(Triangle[1].x, Triangle[2].x)));
    LONG Right = (LONG)ceil(max(Triangle[0].x, max(Triangle[1].x, Triangle[2].x)));
    LONG Top = (LONG)floor(min(Triangle[0].y, min(Triangle[1].y, Triangle[2].y)));
    LONG Bottom = (LONG)ceil(max(Triangle[0].y, max(Triangle[1].y, Triangle[2].y)));
    LONG x, y;

    if (fabs(Area) < 1e-6)
        return;
    for (y = max(Top, 0); y <= Bottom && y < VIEW_HEIGHT; ++y)
    {
        for (x = max(Left, 0); x <= Right && x < VIEW_WIDTH; ++x)
        {
            FLOAT px = x + 0.5f, py = y + 0.5f;
            FLOAT w0 = Edge(&Triangle[1], &Triangle[2], px, py) / Area;
            FLOAT w1 = Edge(&Triangle[2], &Triangle[0], px, py) / Area;
            FLOAT w2 = 1.0f - w0 - w1;
            FLOAT Rhw;

            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
                continue;
            Rhw = w0 * Triangle[0].rhw + w1 * Triangle[1].rhw + w2 * Triangle[2].rhw;
            Painted[y][x] = Triangle[0].color;
            PaintedRhw[y][x] = Rhw;
            if (Rhw > NearestRhw[y][x])
                NearestRhw[y][x] = Rhw;
        }
    }
}

static UINT Render(DWORD Elapsed)
{
    D3DTEST_VERTEX Vertices[D3DTEST_MARK_VERTEX_COUNT + 3];
    UINT Count, i, OutOfView = 0, Mixed = 0;

    ZeroMemory(Painted, sizeof(Painted));
    ZeroMemory(PaintedRhw, sizeof(PaintedRhw));
    ZeroMemory(NearestRhw, sizeof(NearestRhw));
    Count = D3DTestBuildMark(Vertices, Elapsed, VIEW_WIDTH, VIEW_HEIGHT);
    ok(Count > 0 && Count <= D3DTEST_MARK_VERTEX_COUNT && Count % 3 == 0, "%lu ms: %u vertices\n", Elapsed, Count);
    if (Count == 0 || Count > D3DTEST_MARK_VERTEX_COUNT || Count % 3 != 0)
        return 0;

    for (i = 0; i < Count; ++i)
    {
        if (Vertices[i].x < 0.0f || Vertices[i].x > VIEW_WIDTH || Vertices[i].y < 0.0f || Vertices[i].y > VIEW_HEIGHT ||
            Vertices[i].z < 0.0f || Vertices[i].z > 1.0f || Vertices[i].rhw <= 0.0f)
            OutOfView++;
        if (Vertices[i].color != Vertices[i - i % 3].color || (Vertices[i].color >> 24) != 0xff)
            Mixed++;
    }
    ok(OutOfView == 0, "%lu ms: %u vertices outside the view\n", Elapsed, OutOfView);
    ok(Mixed == 0, "%lu ms: %u vertices not opaque or not flat shaded\n", Elapsed, Mixed);

    for (i = 0; i < Count; i += 3)
        RasterizeTriangle(&Vertices[i]);
    return Count;
}

static VOID TestDrawOrder(VOID)
{
    DWORD Elapsed;
    UINT Frames = 0, Wrong = 0, Covered = 0, x, y;

    for (Elapsed = 0; Elapsed <= FRAME_LAST_MS; Elapsed += FRAME_STEP_MS)
    {
        UINT FrameWrong = 0;

        if (!Render(Elapsed))
            continue;
        Frames++;
        for (y = 0; y < VIEW_HEIGHT; ++y)
        {
            for (x = 0; x < VIEW_WIDTH; ++x)
            {
                if (NearestRhw[y][x] == 0.0f)
                    continue;
                Covered++;
                if (PaintedRhw[y][x] < NearestRhw[y][x] * (1.0f - 1e-3f))
                    FrameWrong++;
            }
        }
        ok(FrameWrong == 0, "%lu ms: %u pixels show a face hidden behind another\n", Elapsed, FrameWrong);
        Wrong += FrameWrong;
    }
    ok(Frames == FRAME_LAST_MS / FRAME_STEP_MS + 1, "%u frames rendered\n", Frames);
    ok(Covered > 0, "Nothing was drawn\n");
    trace("%u frames, %u pixels covered, %u drawn out of order\n", Frames, Covered, Wrong);
}

static VOID TestFacingTheViewer(VOID)
{
    LONG Left = VIEW_WIDTH, Right = -1, Top = VIEW_HEIGHT, Bottom = -1, Row, x, y;
    LONG Runs[4][2];
    UINT RunCount = 0;
    double Width, Height;

    if (!Render(0))
        return;
    for (y = 0; y < VIEW_HEIGHT; ++y)
    {
        for (x = 0; x < VIEW_WIDTH; ++x)
        {
            if (Painted[y][x] != MARK_BLUE)
                continue;
            Left = min(Left, x);
            Right = max(Right, x);
            Top = min(Top, y);
            Bottom = max(Bottom, y);
        }
    }
    ok(Right >= Left && Bottom >= Top, "No face in the mark's colour\n");
    if (Right < Left || Bottom < Top)
        return;

    Width = Right - Left + 1;
    Height = Bottom - Top + 1;
    ok(fabs(Height / Width - MARK_HEIGHT / MARK_WIDTH) < 0.02, "Mark is %.0f x %.0f pixels\n", Width, Height);
    ok(Painted[VIEW_HEIGHT / 2][VIEW_WIDTH / 2] == 0, "The middle of the mark is drawn: 0x%08lx\n", Painted[VIEW_HEIGHT / 2][VIEW_WIDTH / 2]);

    Row = (Top + Bottom) / 2;
    for (x = Left; x <= Right && RunCount < ARRAYSIZE(Runs); ++x)
    {
        if (Painted[Row][x] != MARK_BLUE || (x > Left && Painted[Row][x - 1] == MARK_BLUE))
            continue;
        Runs[RunCount][0] = x;
        while (x <= Right && Painted[Row][x] == MARK_BLUE)
            ++x;
        Runs[RunCount++][1] = x - 1;
    }
    ok(RunCount == 2, "%u strokes cross the middle row\n", RunCount);
    if (RunCount != 2)
        return;
    ok(fabs((Runs[0][1] - Runs[0][0] + 1) - Width * MARK_STROKE / MARK_WIDTH) <= 1.5,
       "Left stroke is %ld pixels of %.0f\n", Runs[0][1] - Runs[0][0] + 1, Width);
    ok(fabs((Runs[1][1] - Runs[1][0] + 1) - Width * MARK_STROKE / MARK_WIDTH) <= 1.5,
       "Right stroke is %ld pixels of %.0f\n", Runs[1][1] - Runs[1][0] + 1, Width);
    ok(Runs[0][0] == Left && Runs[1][1] == Right, "Strokes span %ld-%ld, mark %ld-%ld\n", Runs[0][0], Runs[1][1], Left, Right);
}

static VOID TestClipSpace(VOID)
{
    static const D3DTEST_VERTEX Input[] =
    {
        { 0.0f, 0.0f, 0.25f, 1.0f, MARK_BLUE },
        { VIEW_WIDTH, VIEW_HEIGHT, 0.75f, 1.0f, 0x80ff0000 },
        { VIEW_WIDTH / 2.0f, VIEW_HEIGHT / 4.0f, 0.5f, 1.0f, 0xff00ff00 },
    };
    static const FLOAT Expected[ARRAYSIZE(Input)][8] =
    {
        { -1.0f, 1.0f, 0.25f, 1.0f, 1.0f / 255.0f, 97.0f / 255.0f, 182.0f / 255.0f, 1.0f },
        { 1.0f, -1.0f, 0.75f, 1.0f, 1.0f, 0.0f, 0.0f, 128.0f / 255.0f },
        { 0.0f, 0.5f, 0.5f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f },
    };
    D3DTEST_CLIP_VERTEX Output[ARRAYSIZE(Input)];
    UINT i, j;

    D3DTestToClipSpace(Output, Input, ARRAYSIZE(Input), VIEW_WIDTH, VIEW_HEIGHT);
    for (i = 0; i < ARRAYSIZE(Input); ++i)
    {
        for (j = 0; j < 4; ++j)
        {
            ok(fabs(Output[i].Position[j] - Expected[i][j]) < 1e-6, "Vertex %u position %u is %f, expected %f\n", i, j, Output[i].Position[j], Expected[i][j]);
            ok(fabs(Output[i].Color[j] - Expected[i][4 + j]) < 1e-6, "Vertex %u colour %u is %f, expected %f\n", i, j, Output[i].Color[j], Expected[i][4 + j]);
        }
    }
}

static VOID TestStatsOverlay(VOID)
{
    static const PCSTR Lines[] = { "FPS 1234.5", "MARK 99999", "CPU 100%", "GPU N/A" };
    static const PCSTR Eight[] = { "8" };
    D3DTEST_VERTEX Vertices[D3DTEST_HUD_VERTEX_COUNT];
    RECT Rects[D3DTEST_HUD_VERTEX_COUNT / 6];
    UINT Count, Vertex, Outside = 0, Bad = 0, i;

    Count = D3DTestHudRects(Rects, ARRAYSIZE(Rects), Eight, 1, 600);
    ok(Count == 12, "The 8 glyph and its backdrop take %u rectangles, expected 12\n", Count);
    ok(Rects[0].left == 8 && Rects[0].top == 8 && Rects[0].right == 35 && Rects[0].bottom == 41,
       "Backdrop of one glyph at scale 3 is %ld,%ld-%ld,%ld\n", Rects[0].left, Rects[0].top, Rects[0].right, Rects[0].bottom);

    Count = D3DTestHudRects(Rects, ARRAYSIZE(Rects), Lines, ARRAYSIZE(Lines), 600);
    ok(Count > ARRAYSIZE(Lines) && Count < ARRAYSIZE(Rects), "Overlay takes %u rectangles\n", Count);
    for (i = 1; i < Count; ++i)
    {
        if (Rects[i].left < Rects[0].left || Rects[i].right > Rects[0].right || Rects[i].top < Rects[0].top ||
            Rects[i].bottom > Rects[0].bottom || Rects[i].right <= Rects[i].left || Rects[i].bottom <= Rects[i].top)
            Outside++;
    }
    ok(Outside == 0, "%u text rectangles fall outside the backdrop\n", Outside);
    ok(Rects[0].right < VIEW_WIDTH * 2 && Rects[0].bottom < 600 / 2, "Overlay covers %ld x %ld\n", Rects[0].right, Rects[0].bottom);

    Vertex = D3DTestBuildHud(Vertices, ARRAYSIZE(Vertices), Lines, ARRAYSIZE(Lines), 600);
    ok(Vertex == Count * 6, "Overlay has %u vertices for %u rectangles\n", Vertex, Count);
    for (i = 0; i < Vertex; ++i)
    {
        if (Vertices[i].z != 0.0f || Vertices[i].rhw != 1.0f ||
            Vertices[i].color != (i < 6 ? D3DTEST_HUD_BACK_COLOR : D3DTEST_HUD_TEXT_COLOR))
            Bad++;
    }
    ok(Bad == 0, "%u overlay vertices are not on the near plane in the overlay colours\n", Bad);
    ok(D3DTestBuildHud(Vertices, 11, Lines, ARRAYSIZE(Lines), 600) == 6, "A short buffer must hold whole quads only\n");
}

START_TEST(D3DMark)
{
    TestStatsOverlay();
    TestClipSpace();
    TestFacingTheViewer();
    TestDrawOrder();
}
