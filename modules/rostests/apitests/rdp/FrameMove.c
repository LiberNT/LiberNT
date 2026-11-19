/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test how the RDP console server checks and applies moves of desktop areas
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include "framemove.h"

#define WIDTH  97
#define HEIGHT 61
#define STRIDE (WIDTH * 4 + 12)

static BYTE Pixels[STRIDE * HEIGHT];
static BYTE Expected[STRIDE * HEIGHT];
static BYTE Original[STRIDE * HEIGHT];

static void Fill(BYTE *pixels)
{
    UINT32 x, y;
    for (y = 0; y < HEIGHT; ++y)
        for (x = 0; x < WIDTH; ++x)
            *(UINT32 *)(pixels + y * STRIDE + x * 4) = 0xFF000000u | (y << 12) | (x << 2) | ((x * y) & 3);
}

static FRAME_MOVE Move(LONG sourceLeft, LONG sourceTop, LONG left, LONG top, LONG right, LONG bottom)
{
    FRAME_MOVE move;
    move.SourceLeft = sourceLeft;
    move.SourceTop = sourceTop;
    SetRect(&move.Dest, left, top, right, bottom);
    return move;
}

static void TestClip(FRAME_MOVE move, BOOL kept, LONG sourceLeft, LONG sourceTop, LONG left, LONG top, LONG right,
                     LONG bottom, const char *name)
{
    BOOL result = FrameMoveClip(&move, WIDTH, HEIGHT);
    ok(result == kept, "%s: clip returned %d\n", name, result);
    if (!result || !kept) return;
    ok(move.SourceLeft == sourceLeft && move.SourceTop == sourceTop && move.Dest.left == left &&
       move.Dest.top == top && move.Dest.right == right && move.Dest.bottom == bottom,
       "%s: (%ld,%ld) to (%ld,%ld)-(%ld,%ld)\n", name, move.SourceLeft, move.SourceTop, move.Dest.left,
       move.Dest.top, move.Dest.right, move.Dest.bottom);
}

static void TestApply(LONG dx, LONG dy, const char *name)
{
    FRAME_MOVE move = Move(20, 15, 20 + dx, 15 + dy, 20 + dx + 50, 15 + dy + 30);
    LONG x, y;
    Fill(Original);
    memcpy(Pixels, Original, sizeof(Pixels));
    memcpy(Expected, Original, sizeof(Expected));
    for (y = 0; y < 30; ++y)
        for (x = 0; x < 50; ++x)
            *(UINT32 *)(Expected + (15 + dy + y) * STRIDE + (20 + dx + x) * 4) =
                *(const UINT32 *)(Original + (15 + y) * STRIDE + (20 + x) * 4);
    ok(FrameMoveMatches(Original, Expected, STRIDE, &move), "%s: the moved frame does not match\n", name);
    FrameMoveApply(Pixels, STRIDE, &move);
    ok(!memcmp(Pixels, Expected, sizeof(Pixels)), "%s: the moved pixels differ\n", name);
    ok(!FrameMoveMatches(Original, Original, STRIDE, &move), "%s: an unmoved frame matches\n", name);
}

START_TEST(FrameMove)
{
    FRAME_MOVE move;
    LONG row;

    TestClip(Move(10, 10, 20, 30, 40, 50), TRUE, 10, 10, 20, 30, 40, 50, "inside");
    TestClip(Move(-5, 10, 5, 10, 25, 20), TRUE, 0, 10, 10, 10, 25, 20, "source left of the frame");
    TestClip(Move(80, 0, 90, 5, 120, 15), TRUE, 80, 0, 90, 5, 97, 15, "destination right of the frame");
    TestClip(Move(60, 50, 0, 0, 40, 30), TRUE, 60, 50, 0, 0, 37, 11, "source past the right and bottom");
    TestClip(Move(0, 0, -10, -20, 30, 30), TRUE, 10, 20, 0, 0, 30, 30, "destination above and left of the frame");
    TestClip(Move(10, 10, 10, 10, 30, 30), FALSE, 0, 0, 0, 0, 0, 0, "no offset");
    TestClip(Move(0, 0, 100, 0, 120, 10), FALSE, 0, 0, 0, 0, 0, 0, "destination outside");
    TestClip(Move(0, 70, 0, 0, 10, 10), FALSE, 0, 0, 0, 0, 0, 0, "source outside");
    TestClip(Move(0, 0, 10, 10, 10, 20), FALSE, 0, 0, 0, 0, 0, 0, "empty");
    TestClip(Move(-2000000000, 0, 2000000000, 0, 2000000010, 10), FALSE, 0, 0, 0, 0, 0, 0, "extreme offset");

    TestApply(7, 0, "right");
    TestApply(-7, 0, "left");
    TestApply(0, 9, "down");
    TestApply(0, -9, "up");
    TestApply(5, 6, "down right");
    TestApply(-5, -6, "up left");
    TestApply(3, -11, "up right");
    TestApply(-15, 16, "down left");

    Fill(Original);
    memcpy(Expected, Original, sizeof(Expected));
    move = Move(20, 10, 30, 10, 70, 30);
    for (row = 0; row < 20; ++row)
        memcpy(Expected + (10 + row) * STRIDE + 30 * 4, Original + (10 + row) * STRIDE + 20 * 4, 40 * 4);
    for (row = 0; row < 11; ++row)
        memset(Expected + (10 + row) * STRIDE + 30 * 4, 0x55, 40 * 4);
    ok(!FrameMoveMatches(Original, Expected, STRIDE, &move), "a move with most rows repainted matches\n");
    for (row = 0; row < 20; ++row)
        memcpy(Expected + (10 + row) * STRIDE + 30 * 4, Original + (10 + row) * STRIDE + 20 * 4, 40 * 4);
    for (row = 0; row < 10; ++row)
        memset(Expected + (10 + row) * STRIDE + 30 * 4, 0x55, 40 * 4);
    ok(FrameMoveMatches(Original, Expected, STRIDE, &move), "a move with half of its rows repainted does not match\n");
}
