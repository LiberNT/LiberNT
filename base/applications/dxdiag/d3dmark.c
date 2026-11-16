/*
 * PROJECT:     LiberNT DirectX Diagnostic Tool
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Rotating LiberNT mark drawn by the Direct3D tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include <math.h>
#include <windef.h>

#include "d3dmark.h"

#define MARK_POLYGONS 2
#define MARK_CORNERS 6
#define MARK_INNER_CORNER 2
#define MARK_WALLS (MARK_POLYGONS * MARK_CORNERS)
#define MARK_CENTER_X 203.5f
#define MARK_CENTER_Y 178.0f
#define MARK_STROKE 88.0f
#define MARK_SCALE (2.2f / 407.0f)
#define MARK_CAMERA_DISTANCE 4.0f
#define MARK_AMBIENT 0.55f
#define MARK_DIFFUSE 0.45f

typedef struct
{
    FLOAT x;
    FLOAT y;
} MARK_POINT;

typedef struct
{
    FLOAT x;
    FLOAT y;
    FLOAT z;
} MARK_VECTOR;

typedef struct
{
    MARK_POINT Start;
    MARK_POINT End;
    MARK_POINT Normal;
} MARK_WALL;

typedef struct
{
    FLOAT CosX;
    FLOAT SinX;
    FLOAT CosY;
    FLOAT SinY;
    FLOAT CenterX;
    FLOAT CenterY;
    FLOAT Focal;
    PD3DTEST_VERTEX Vertices;
    UINT Count;
} MARK_FRAME;

/* The two strokes of media/graphics/branding/libernt-mark.svg; the inner corner of each comes third */
static const MARK_POINT MarkOutline[MARK_POLYGONS][MARK_CORNERS] =
{
    {{0.0f, 60.0f}, {88.0f, 60.0f}, {88.0f, 268.0f}, {299.0f, 268.0f}, {299.0f, 356.0f}, {0.0f, 356.0f}},
    {{407.0f, 296.0f}, {319.0f, 296.0f}, {319.0f, 89.0f}, {108.0f, 89.0f}, {108.0f, 0.0f}, {407.0f, 0.0f}},
};

static const MARK_VECTOR MarkLight = {-0.2491f, 0.4983f, -0.8305f};
static const BYTE MarkColor[3] = {0x01, 0x61, 0xB6};

static MARK_POINT MarkCorner(UINT Polygon, UINT Corner)
{
    MARK_POINT Point;

    Point.x = (MarkOutline[Polygon][Corner % MARK_CORNERS].x - MARK_CENTER_X) * MARK_SCALE;
    Point.y = (MARK_CENTER_Y - MarkOutline[Polygon][Corner % MARK_CORNERS].y) * MARK_SCALE;
    return Point;
}

static MARK_VECTOR MarkRotate(const MARK_FRAME *Frame, FLOAT x, FLOAT y, FLOAT z)
{
    MARK_VECTOR Result;
    FLOAT Turned = -x * Frame->SinY + z * Frame->CosY;

    Result.x = x * Frame->CosY + z * Frame->SinY;
    Result.y = y * Frame->CosX - Turned * Frame->SinX;
    Result.z = y * Frame->SinX + Turned * Frame->CosX;
    return Result;
}

static DWORD MarkShade(const MARK_FRAME *Frame, FLOAT x, FLOAT y, FLOAT z)
{
    MARK_VECTOR Normal = MarkRotate(Frame, x, y, z);
    FLOAT Lit = Normal.x * MarkLight.x + Normal.y * MarkLight.y + Normal.z * MarkLight.z;
    FLOAT Intensity = (MARK_AMBIENT + MARK_DIFFUSE * Lit) / (MARK_AMBIENT - MARK_DIFFUSE * MarkLight.z);
    DWORD Color = 0xff000000;
    UINT i;

    for (i = 0; i < 3; ++i)
    {
        FLOAT Channel = MarkColor[i] * Intensity + 0.5f;

        Color |= (DWORD)(Channel > 255.0f ? 255.0f : Channel) << (16 - 8 * i);
    }
    return Color;
}

static VOID MarkEmit(MARK_FRAME *Frame, MARK_POINT Point, FLOAT z, DWORD Color)
{
    PD3DTEST_VERTEX Vertex = &Frame->Vertices[Frame->Count++];
    MARK_VECTOR View = MarkRotate(Frame, Point.x, Point.y, z);
    FLOAT Depth = View.z + MARK_CAMERA_DISTANCE;

    Vertex->x = Frame->CenterX + View.x * Frame->Focal / Depth;
    Vertex->y = Frame->CenterY - View.y * Frame->Focal / Depth;
    Vertex->z = (Depth - 2.0f) * 0.25f;
    Vertex->rhw = 1.0f / Depth;
    Vertex->color = Color;
}

static FLOAT MarkCross(MARK_POINT Origin, MARK_POINT A, MARK_POINT B)
{
    return (A.x - Origin.x) * (B.y - Origin.y) - (A.y - Origin.y) * (B.x - Origin.x);
}

static BOOL MarkSeesWithin(const MARK_WALL *Wall, MARK_POINT Eye, MARK_POINT Point)
{
    return MarkCross(Eye, Wall->Start, Point) > 0.0f && MarkCross(Eye, Point, Wall->End) > 0.0f;
}

static BOOL MarkWallsOverlap(const MARK_WALL *A, const MARK_WALL *B, MARK_POINT Eye)
{
    MARK_POINT MiddleA = {(A->Start.x + A->End.x) * 0.5f, (A->Start.y + A->End.y) * 0.5f};
    MARK_POINT MiddleB = {(B->Start.x + B->End.x) * 0.5f, (B->Start.y + B->End.y) * 0.5f};

    return MarkSeesWithin(A, Eye, B->Start) || MarkSeesWithin(A, Eye, B->End) || MarkSeesWithin(A, Eye, MiddleB) ||
           MarkSeesWithin(B, Eye, A->Start) || MarkSeesWithin(B, Eye, A->End) || MarkSeesWithin(B, Eye, MiddleA);
}

static FLOAT MarkSide(const MARK_WALL *Wall, MARK_POINT Point)
{
    return Wall->Normal.x * (Point.x - Wall->Start.x) + Wall->Normal.y * (Point.y - Wall->Start.y);
}

static BOOL MarkWallInFront(const MARK_WALL *Front, const MARK_WALL *Back)
{
    FLOAT Start = MarkSide(Back, Front->Start);
    FLOAT End = MarkSide(Back, Front->End);

    if (Start >= 0.0f && End >= 0.0f)
        return TRUE;
    if (Start <= 0.0f && End <= 0.0f)
        return FALSE;
    return MarkSide(Front, Back->Start) <= 0.0f && MarkSide(Front, Back->End) <= 0.0f;
}

static UINT MarkVisibleWalls(MARK_WALL *Walls, MARK_POINT Eye)
{
    UINT Polygon, Corner, Count = 0;

    for (Polygon = 0; Polygon < MARK_POLYGONS; ++Polygon)
    {
        FLOAT Area = 0.0f;

        for (Corner = 0; Corner < MARK_CORNERS; ++Corner)
            Area += MarkCross(MarkCorner(Polygon, 0), MarkCorner(Polygon, Corner), MarkCorner(Polygon, Corner + 1));

        for (Corner = 0; Corner < MARK_CORNERS; ++Corner)
        {
            MARK_WALL *Wall = &Walls[Count];
            MARK_POINT Start = MarkCorner(Polygon, Corner);
            MARK_POINT End = MarkCorner(Polygon, Corner + 1);
            FLOAT Length = (FLOAT)sqrt((End.x - Start.x) * (End.x - Start.x) + (End.y - Start.y) * (End.y - Start.y));
            FLOAT Outward = Area > 0.0f ? 1.0f / Length : -1.0f / Length;

            Wall->Normal.x = (End.y - Start.y) * Outward;
            Wall->Normal.y = (Start.x - End.x) * Outward;
            Wall->Start = Start;
            if (MarkSide(Wall, Eye) <= 0.0f)
                continue;
            if (MarkCross(Eye, Start, End) > 0.0f)
            {
                Wall->End = End;
            }
            else
            {
                Wall->Start = End;
                Wall->End = Start;
            }
            ++Count;
        }
    }
    return Count;
}

static VOID MarkEmitWalls(MARK_FRAME *Frame, MARK_POINT Eye)
{
    MARK_WALL Walls[MARK_WALLS];
    UINT Hidden[MARK_WALLS] = {0};
    UINT Count, Drawn = 0, Step, i, j;
    FLOAT HalfDepth = MARK_STROKE * MARK_SCALE * 0.5f;

    Count = MarkVisibleWalls(Walls, Eye);
    for (i = 0; i < Count; ++i)
    {
        for (j = i + 1; j < Count; ++j)
        {
            if (!MarkWallsOverlap(&Walls[i], &Walls[j], Eye))
                continue;
            if (MarkWallInFront(&Walls[j], &Walls[i]))
                Hidden[j] |= 1u << i;
            else
                Hidden[i] |= 1u << j;
        }
    }

    for (Step = 0; Step < Count; ++Step)
    {
        UINT Next = Count;
        DWORD Color;

        for (i = 0; i < Count && Next == Count; ++i)
        {
            if (!(Drawn & (1u << i)) && !(Hidden[i] & ~Drawn))
                Next = i;
        }
        for (i = 0; i < Count && Next == Count; ++i)
        {
            if (!(Drawn & (1u << i)))
                Next = i;
        }
        Drawn |= 1u << Next;

        Color = MarkShade(Frame, Walls[Next].Normal.x, Walls[Next].Normal.y, 0.0f);
        MarkEmit(Frame, Walls[Next].Start, -HalfDepth, Color);
        MarkEmit(Frame, Walls[Next].End, -HalfDepth, Color);
        MarkEmit(Frame, Walls[Next].End, HalfDepth, Color);
        MarkEmit(Frame, Walls[Next].Start, -HalfDepth, Color);
        MarkEmit(Frame, Walls[Next].End, HalfDepth, Color);
        MarkEmit(Frame, Walls[Next].Start, HalfDepth, Color);
    }
}

static VOID MarkEmitFace(MARK_FRAME *Frame, FLOAT z)
{
    DWORD Color = MarkShade(Frame, 0.0f, 0.0f, z < 0.0f ? -1.0f : 1.0f);
    UINT Polygon, Corner;

    for (Polygon = 0; Polygon < MARK_POLYGONS; ++Polygon)
    {
        for (Corner = MARK_INNER_CORNER + 1; Corner < MARK_INNER_CORNER + MARK_CORNERS - 1; ++Corner)
        {
            MarkEmit(Frame, MarkCorner(Polygon, MARK_INNER_CORNER), z, Color);
            MarkEmit(Frame, MarkCorner(Polygon, Corner), z, Color);
            MarkEmit(Frame, MarkCorner(Polygon, Corner + 1), z, Color);
        }
    }
}

UINT D3DTestBuildMark(PD3DTEST_VERTEX Vertices, DWORD Elapsed, UINT Width, UINT Height)
{
    MARK_FRAME Frame;
    MARK_POINT Eye;
    FLOAT Angle, EyeDepth;
    FLOAT HalfDepth = MARK_STROKE * MARK_SCALE * 0.5f;

    Angle = (FLOAT)Elapsed * 0.0015f;
    Frame.CosX = (FLOAT)cos(Angle * 0.7f);
    Frame.SinX = (FLOAT)sin(Angle * 0.7f);
    Frame.CosY = (FLOAT)cos(Angle);
    Frame.SinY = (FLOAT)sin(Angle);
    Frame.CenterX = (FLOAT)Width * 0.5f;
    Frame.CenterY = (FLOAT)Height * 0.5f;
    Frame.Focal = (FLOAT)(Width < Height ? Width : Height) * 1.2f;
    Frame.Vertices = Vertices;
    Frame.Count = 0;

    Eye.x = MARK_CAMERA_DISTANCE * Frame.CosX * Frame.SinY;
    Eye.y = -MARK_CAMERA_DISTANCE * Frame.SinX;
    EyeDepth = -MARK_CAMERA_DISTANCE * Frame.CosX * Frame.CosY;

    MarkEmitWalls(&Frame, Eye);
    if (EyeDepth < -HalfDepth)
        MarkEmitFace(&Frame, -HalfDepth);
    else if (EyeDepth > HalfDepth)
        MarkEmitFace(&Frame, HalfDepth);
    return Frame.Count;
}

VOID D3DTestToClipSpace(PD3DTEST_CLIP_VERTEX Output, const D3DTEST_VERTEX *Input, UINT Count, UINT Width, UINT Height)
{
    UINT i;

    for (i = 0; i < Count; ++i)
    {
        Output[i].Position[0] = Input[i].x * 2.0f / (FLOAT)Width - 1.0f;
        Output[i].Position[1] = 1.0f - Input[i].y * 2.0f / (FLOAT)Height;
        Output[i].Position[2] = Input[i].z;
        Output[i].Position[3] = 1.0f;
        Output[i].Color[0] = (FLOAT)((Input[i].color >> 16) & 0xff) / 255.0f;
        Output[i].Color[1] = (FLOAT)((Input[i].color >> 8) & 0xff) / 255.0f;
        Output[i].Color[2] = (FLOAT)(Input[i].color & 0xff) / 255.0f;
        Output[i].Color[3] = (FLOAT)(Input[i].color >> 24) / 255.0f;
    }
}
