/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the RDP 8.0 bulk compressor (MS-RDPEGFX 3.1.9.1) against the FreeRDP decompressor
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include <stdlib.h>
#include <freerdp/codec/zgfx.h>

#define LARGE_SIZE (200 * 1024)

static BYTE Data[LARGE_SIZE];
static UINT32 Random = 0x2468ACE1;

static BYTE NextRandom(void)
{
    Random = Random * 1103515245u + 12345u;
    return (BYTE)(Random >> 16);
}

static UINT32 RoundTrip(ZGFX_CONTEXT *compressor, ZGFX_CONTEXT *decompressor, const BYTE *data, UINT32 size,
                        const char *name)
{
    BYTE *compressed = NULL, *decompressed = NULL;
    UINT32 compressedSize = 0, decompressedSize = 0, flags = 0;
    int status = zgfx_compress(compressor, data, size, &compressed, &compressedSize, &flags);
    ok(status >= 0, "%s: zgfx_compress returned %d\n", name, status);
    if (status < 0) return 0;
    status = zgfx_decompress(decompressor, compressed, compressedSize, &decompressed, &decompressedSize, flags);
    ok(status >= 0, "%s: zgfx_decompress returned %d\n", name, status);
    ok(decompressedSize == size, "%s: %lu bytes decompressed, expected %lu\n", name, (ULONG)decompressedSize, (ULONG)size);
    ok(decompressed && decompressedSize == size && !memcmp(decompressed, data, size), "%s: data differs\n", name);
    free(compressed);
    free(decompressed);
    return compressedSize;
}

START_TEST(Zgfx)
{
    ZGFX_CONTEXT *compressor = zgfx_context_new(TRUE), *decompressor = zgfx_context_new(FALSE);
    UINT32 i, size, total;
    char name[64];
    ok(compressor && decompressor, "zgfx_context_new failed\n");
    if (!compressor || !decompressor) return;

    for (i = 0; i < 256; ++i) Data[i] = (BYTE)i;
    RoundTrip(compressor, decompressor, Data, 256, "every byte value");

    for (i = 0; i < 4096; ++i) Data[i] = (BYTE)("the remote desktop "[i % 19]);
    size = RoundTrip(compressor, decompressor, Data, 4096, "repeated text");
    ok(size < 200, "repeated text compressed to %lu bytes\n", (ULONG)size);
    size = RoundTrip(compressor, decompressor, Data, 4096, "the same text again");
    ok(size < 32, "a message repeated from the history compressed to %lu bytes\n", (ULONG)size);

    for (i = 0; i < 60000; ++i) Data[i] = NextRandom();
    size = RoundTrip(compressor, decompressor, Data, 60000, "random bytes");
    ok(size <= 60000 + 16, "random bytes grew to %lu bytes\n", (ULONG)size);
    size = RoundTrip(compressor, decompressor, Data, 60000, "the random bytes again");
    ok(size < 2000, "random bytes repeated from the history compressed to %lu bytes\n", (ULONG)size);

    {
        ZGFX_CONTEXT *fresh = zgfx_context_new(TRUE), *freshDecompressor = zgfx_context_new(FALSE);
        ok(fresh && freshDecompressor, "zgfx_context_new failed\n");
        if (fresh && freshDecompressor)
        {
            UINT32 saved = Random;
            Random = 0x2468ACE1;
            for (i = 0; i < 60000; ++i) Data[i] = NextRandom() & 1;
            size = RoundTrip(fresh, freshDecompressor, Data, 60000, "two byte values");
            ok(size < 16300, "two byte values compressed to %lu bytes\n", (ULONG)size);
            for (i = 0; i < 60000; ++i) Data[i] = NextRandom() & 3;
            size = RoundTrip(fresh, freshDecompressor, Data, 60000, "four byte values");
            ok(size < 26000, "four byte values compressed to %lu bytes\n", (ULONG)size);
            Random = saved;
        }
        zgfx_context_free(fresh);
        zgfx_context_free(freshDecompressor);
    }

    memset(Data, 0x5A, 100000);
    size = RoundTrip(compressor, decompressor, Data, 100000, "a long run across segments");
    ok(size < 200, "a long run compressed to %lu bytes\n", (ULONG)size);

    for (i = 0; i < LARGE_SIZE; ++i)
        Data[i] = (i / 64) % 3 ? (BYTE)(i * 7) : NextRandom();
    RoundTrip(compressor, decompressor, Data, LARGE_SIZE, "mixed literals and matches across segments");

    for (total = 0, i = 0; total < 3 * 2500000; ++i)
    {
        UINT32 j;
        for (j = 0; j < 65535; ++j) Data[j] = (j % 300) < 200 ? NextRandom() : Data[(j + 4096) % 65535];
        sprintf(name, "history wrap message %lu", (ULONG)i);
        RoundTrip(compressor, decompressor, Data, 65535, name);
        total += 65535;
    }

    RoundTrip(compressor, decompressor, Data, 1, "a single byte");
    zgfx_context_free(compressor);
    zgfx_context_free(decompressor);
}
