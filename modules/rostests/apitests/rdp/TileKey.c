/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the keyed hash that names a tile in the graphics cache
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include "tilekey.h"

#define WIDTH 128
#define HEIGHT 64

static UINT32 Pixels[HEIGHT][WIDTH];

START_TEST(TileKey)
{
    TILE_KEY_SECRET First, Second;
    UINT64 Key, Again;
    UINT32 x, y, Seed = 0x13579BDF;

    for (y = 0; y < HEIGHT; y++)
    {
        for (x = 0; x < WIDTH / 2; x++)
        {
            Seed = Seed * 1103515245u + 12345u;
            Pixels[y][x] = Pixels[y][x + WIDTH / 2] = Seed;
        }
    }
    ok(TileKeySecret(&First) && TileKeySecret(&Second), "TileKeySecret failed\n");
    ok(First.K0 != Second.K0 || First.K1 != Second.K1, "Two connections got the same secret\n");

    Key = TileKeyOf(&First, (BYTE *)Pixels, WIDTH * 4, 0, 0, 64, 64);
    ok(TileKeyOf(&First, (BYTE *)Pixels, WIDTH * 4, 0, 0, 64, 64) == Key, "The same tile got another key\n");
    ok(TileKeyOf(&First, (BYTE *)Pixels, WIDTH * 4, 64, 0, 64, 64) == Key,
       "The same pixels elsewhere on the screen got another key\n");
    ok(TileKeyOf(&Second, (BYTE *)Pixels, WIDTH * 4, 0, 0, 64, 64) != Key,
       "The key does not depend on the connection's secret\n");
    ok(TileKeyOf(&First, (BYTE *)Pixels, WIDTH * 4, 0, 0, 64, 32) != Key, "A smaller tile got the same key\n");

    Pixels[10][10] ^= 0xFF000000;
    ok(TileKeyOf(&First, (BYTE *)Pixels, WIDTH * 4, 0, 0, 64, 64) == Key, "The alpha byte changed the key\n");
    Pixels[63][63] ^= 1;
    Again = TileKeyOf(&First, (BYTE *)Pixels, WIDTH * 4, 0, 0, 64, 64);
    ok(Again != Key, "Changing the last pixel kept the key\n");
    Pixels[63][63] ^= 1;
    Pixels[0][0] ^= 0x00800000;
    ok(TileKeyOf(&First, (BYTE *)Pixels, WIDTH * 4, 0, 0, 64, 64) != Key, "Changing the first pixel kept the key\n");
}
