/*
 * PROJECT:     LiberNT RDP console server
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Graphics pipeline (MS-RDPEGFX) output of the remote desktop
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <freerdp/channels/wtsvc.h>
#include <freerdp/codec/color.h>
#include <freerdp/codec/progressive.h>
#include <freerdp/codec/region.h>
#include <freerdp/server/rdpgfx.h>
#include "gfx.h"
#include "clearenc.h"

#define GFX_SURFACE_ID 0
#define GFX_TILE_SIZE 64
#define GFX_FRAMES_IN_FLIGHT 3
#define GFX_CACHE_BUCKETS 32768
#define GFX_CLEAR_BYTES (GFX_TILE_SIZE * GFX_TILE_SIZE * 4 + 64)
#define GFX_CACHE_SLOTS 25600
#define GFX_SMALL_CACHE_SLOTS 4096
#define GFX_CACHE_BYTES (100u * 1024 * 1024)
#define GFX_SMALL_CACHE_BYTES (16u * 1024 * 1024)

typedef struct GFX_CACHE_ENTRY
{
    UINT64 Key;
    UINT32 Bytes;
    UINT16 HashNext, Newer, Older;
    BOOL Used;
} GFX_CACHE_ENTRY;

struct RDP_GFX
{
    RdpgfxServerContext *Context;
    GFX_LOG Log;
    UINT32 ChannelId;
    BOOL Opened, Active, SurfaceCreated;
    RDPGFX_CAPSET CapSet;
    UINT32 Width, Height;
    UINT32 NextFrameId, LastAckedFrameId;
    BOOL AckSuspended;
    PROGRESSIVE_CONTEXT *Progressive;
    UINT32 ProgressiveContextId;
    GFX_TILE *Pending;
    UINT64 *PendingKeys;
    UINT32 PendingCapacity;
    BYTE *Encoded, *Clear;
    BYTE ClearSequence;
    GFX_CACHE_ENTRY *Cache;
    UINT16 *Buckets, *FreeSlots;
    UINT16 CacheSlots, NextFreeSlot, FreeCount, Newest, Oldest;
    UINT32 CacheBytes, CacheLimit;
};

static const UINT32 KnownVersions[] =
{
    RDPGFX_CAPVERSION_107, RDPGFX_CAPVERSION_106, RDPGFX_CAPVERSION_105, RDPGFX_CAPVERSION_104,
    RDPGFX_CAPVERSION_103, RDPGFX_CAPVERSION_102, RDPGFX_CAPVERSION_101, RDPGFX_CAPVERSION_10,
    RDPGFX_CAPVERSION_81, RDPGFX_CAPVERSION_8
};

static UINT CapsAdvertise(RdpgfxServerContext *context, const RDPGFX_CAPS_ADVERTISE_PDU *pdu)
{
    RDP_GFX *gfx = context->custom;
    RDPGFX_CAPS_CONFIRM_PDU confirm = {0};
    const RDPGFX_CAPSET *chosen = NULL;
    UINT32 i, j;
    UINT error;
    for (i = 0; !chosen && i < ARRAYSIZE(KnownVersions); ++i)
        for (j = 0; j < pdu->capsSetCount; ++j)
            if (pdu->capsSets[j].version == KnownVersions[i])
            {
                chosen = &pdu->capsSets[j];
                break;
            }
    if (!chosen)
    {
        gfx->Log("Client offered no graphics pipeline version this server knows");
        return CHANNEL_RC_UNSUPPORTED_VERSION;
    }
    gfx->CapSet = *chosen;
    gfx->CapSet.length = 4;
    confirm.capsSet = &gfx->CapSet;
    error = context->CapsConfirm(context, &confirm);
    if (error != CHANNEL_RC_OK) return error;
    gfx->Active = TRUE;
    gfx->SurfaceCreated = FALSE;
    gfx->ClearSequence = 0;
    memset(gfx->Cache, 0, (GFX_CACHE_SLOTS + 1) * sizeof(*gfx->Cache));
    memset(gfx->Buckets, 0, GFX_CACHE_BUCKETS * sizeof(*gfx->Buckets));
    gfx->NextFreeSlot = 1;
    gfx->FreeCount = 0;
    gfx->Newest = gfx->Oldest = 0;
    gfx->CacheBytes = 0;
    if ((gfx->CapSet.flags & (RDPGFX_CAPS_FLAG_THINCLIENT | RDPGFX_CAPS_FLAG_SMALL_CACHE)) ||
        gfx->CapSet.version == RDPGFX_CAPVERSION_103)
    {
        gfx->CacheSlots = GFX_SMALL_CACHE_SLOTS;
        gfx->CacheLimit = GFX_SMALL_CACHE_BYTES;
    }
    else
    {
        gfx->CacheSlots = GFX_CACHE_SLOTS;
        gfx->CacheLimit = GFX_CACHE_BYTES;
    }
    gfx->Log("Graphics pipeline version 0x%08lx flags 0x%08lx", (ULONG)gfx->CapSet.version, (ULONG)gfx->CapSet.flags);
    return CHANNEL_RC_OK;
}

static UINT FrameAcknowledge(RdpgfxServerContext *context, const RDPGFX_FRAME_ACKNOWLEDGE_PDU *pdu)
{
    RDP_GFX *gfx = context->custom;
    if (pdu->frameId <= gfx->NextFrameId && pdu->frameId > gfx->LastAckedFrameId) gfx->LastAckedFrameId = pdu->frameId;
    gfx->AckSuspended = pdu->queueDepth == SUSPEND_FRAME_ACKNOWLEDGEMENT;
    return CHANNEL_RC_OK;
}

static BOOL ChannelIdAssigned(RdpgfxServerContext *context, UINT32 channelId)
{
    ((RDP_GFX *)context->custom)->ChannelId = channelId;
    return TRUE;
}

RDP_GFX *GfxCreate(HANDLE vcm, rdpContext *rdp, GFX_LOG log)
{
    RDP_GFX *gfx = calloc(1, sizeof(*gfx));
    if (!gfx) return NULL;
    gfx->Log = log;
    gfx->Context = rdpgfx_server_context_new(vcm);
    gfx->Progressive = progressive_context_new(TRUE);
    gfx->Encoded = malloc(GFX_TILE_SIZE * GFX_TILE_SIZE * 4 + 2);
    gfx->Clear = malloc(GFX_CLEAR_BYTES);
    gfx->Cache = calloc(GFX_CACHE_SLOTS + 1, sizeof(*gfx->Cache));
    gfx->Buckets = calloc(GFX_CACHE_BUCKETS, sizeof(*gfx->Buckets));
    gfx->FreeSlots = calloc(GFX_CACHE_SLOTS, sizeof(*gfx->FreeSlots));
    if (!gfx->Context || !gfx->Progressive || !gfx->Encoded || !gfx->Clear || !gfx->Cache || !gfx->Buckets || !gfx->FreeSlots)
    {
        GfxFree(gfx);
        return NULL;
    }
    gfx->Context->custom = gfx;
    gfx->Context->rdpcontext = rdp;
    gfx->Context->CapsAdvertise = CapsAdvertise;
    gfx->Context->FrameAcknowledge = FrameAcknowledge;
    gfx->Context->ChannelIdAssigned = ChannelIdAssigned;
    if (!gfx->Context->Initialize(gfx->Context, TRUE))
    {
        GfxFree(gfx);
        return NULL;
    }
    return gfx;
}

void GfxFree(RDP_GFX *gfx)
{
    if (!gfx) return;
    if (gfx->Context)
    {
        if (gfx->Opened) (void)gfx->Context->Close(gfx->Context);
        rdpgfx_server_context_free(gfx->Context);
    }
    progressive_context_free(gfx->Progressive);
    free(gfx->Pending);
    free(gfx->PendingKeys);
    free(gfx->Encoded);
    free(gfx->Clear);
    free(gfx->Cache);
    free(gfx->Buckets);
    free(gfx->FreeSlots);
    free(gfx);
}

BOOL GfxOpen(RDP_GFX *gfx)
{
    if (gfx->Opened) return TRUE;
    gfx->Opened = gfx->Context->Open(gfx->Context);
    if (!gfx->Opened) gfx->Log("Cannot open the graphics pipeline channel");
    return gfx->Opened;
}

UINT32 GfxChannelId(const RDP_GFX *gfx)
{
    return gfx->ChannelId;
}

BOOL GfxChannelCreated(RDP_GFX *gfx, INT32 status)
{
    if (status >= 0) return TRUE;
    gfx->Log("Client declined the graphics pipeline channel status=%ld", (long)status);
    return TRUE;
}

HANDLE GfxEventHandle(const RDP_GFX *gfx)
{
    return gfx->Opened ? rdpgfx_server_get_event_handle(gfx->Context) : NULL;
}

BOOL GfxHandleMessages(RDP_GFX *gfx)
{
    UINT error;
    if (!gfx->Opened) return TRUE;
    error = rdpgfx_server_handle_messages(gfx->Context);
    if (error == CHANNEL_RC_OK || error == ERROR_NO_DATA) return TRUE;
    gfx->Log("Graphics pipeline channel failed error=%lu", (ULONG)error);
    return FALSE;
}

BOOL GfxActive(const RDP_GFX *gfx)
{
    return gfx && gfx->Active;
}

BOOL GfxSurfaceReady(const RDP_GFX *gfx, UINT32 width, UINT32 height)
{
    return gfx->SurfaceCreated && gfx->Width == width && gfx->Height == height;
}

BOOL GfxResetSurface(RDP_GFX *gfx, UINT32 width, UINT32 height)
{
    RdpgfxServerContext *context = gfx->Context;
    RDPGFX_RESET_GRAPHICS_PDU reset = {0};
    RDPGFX_CREATE_SURFACE_PDU create = {0};
    MONITOR_DEF monitor = {0};
    assert(gfx->Active && width && height && width <= 0xFFFF && height <= 0xFFFF);
    if (gfx->SurfaceCreated)
    {
        RDPGFX_DELETE_SURFACE_PDU remove = {0};
        remove.surfaceId = GFX_SURFACE_ID;
        if (context->DeleteSurface(context, &remove) != CHANNEL_RC_OK) return FALSE;
        gfx->SurfaceCreated = FALSE;
    }
    monitor.right = (INT32)width - 1;
    monitor.bottom = (INT32)height - 1;
    monitor.flags = MONITOR_PRIMARY;
    reset.width = width;
    reset.height = height;
    reset.monitorCount = 1;
    reset.monitorDefArray = &monitor;
    if (context->ResetGraphics(context, &reset) != CHANNEL_RC_OK) return FALSE;
    create.surfaceId = GFX_SURFACE_ID;
    create.width = (UINT16)width;
    create.height = (UINT16)height;
    create.pixelFormat = GFX_PIXEL_FORMAT_XRGB_8888;
    if (context->CreateSurface(context, &create) != CHANNEL_RC_OK) return FALSE;
    if (gfx->CapSet.version >= RDPGFX_CAPVERSION_107 && !(gfx->CapSet.flags & RDPGFX_CAPS_FLAG_SCALEDMAP_DISABLE))
    {
        RDPGFX_MAP_SURFACE_TO_SCALED_OUTPUT_PDU map = {0};
        map.surfaceId = GFX_SURFACE_ID;
        map.targetWidth = width;
        map.targetHeight = height;
        if (context->MapSurfaceToScaledOutput(context, &map) != CHANNEL_RC_OK) return FALSE;
    }
    else
    {
        RDPGFX_MAP_SURFACE_TO_OUTPUT_PDU map = {0};
        map.surfaceId = GFX_SURFACE_ID;
        if (context->MapSurfaceToOutput(context, &map) != CHANNEL_RC_OK) return FALSE;
    }
    gfx->Width = width;
    gfx->Height = height;
    gfx->SurfaceCreated = TRUE;
    gfx->LastAckedFrameId = gfx->NextFrameId;
    return TRUE;
}

BOOL GfxFrameAllowed(const RDP_GFX *gfx)
{
    return gfx->AckSuspended || gfx->NextFrameId - gfx->LastAckedFrameId <= GFX_FRAMES_IN_FLIGHT;
}

static UINT32 FrameTimestamp(void)
{
    SYSTEMTIME now;
    GetLocalTime(&now);
    return ((UINT32)now.wHour << 22) | ((UINT32)now.wMinute << 16) | ((UINT32)now.wSecond << 10) | now.wMilliseconds;
}

static UINT64 TileKey(const BYTE *pixels, UINT32 stride, const GFX_TILE *tile)
{
    UINT64 key = 0xCBF29CE484222325ull ^ ((UINT64)tile->Width << 16 | tile->Height);
    UINT32 x, y;
    for (y = 0; y < tile->Height; ++y)
    {
        const UINT32 *row = (const UINT32 *)(pixels + (size_t)(tile->Top + y) * stride + (size_t)tile->Left * 4);
        for (x = 0; x < tile->Width; ++x)
        {
            key ^= row[x] & 0x00FFFFFF;
            key *= 0x100000001B3ull;
        }
    }
    return key;
}

static BOOL TileSolid(const BYTE *pixels, UINT32 stride, const GFX_TILE *tile, UINT32 *color)
{
    UINT32 x, y, first = *(const UINT32 *)(pixels + (size_t)tile->Top * stride + (size_t)tile->Left * 4) & 0x00FFFFFF;
    for (y = 0; y < tile->Height; ++y)
    {
        const UINT32 *row = (const UINT32 *)(pixels + (size_t)(tile->Top + y) * stride + (size_t)tile->Left * 4);
        for (x = 0; x < tile->Width; ++x)
            if ((row[x] & 0x00FFFFFF) != first) return FALSE;
    }
    *color = first;
    return TRUE;
}

static UINT16 *CacheBucket(RDP_GFX *gfx, UINT64 key)
{
    return &gfx->Buckets[(key ^ (key >> 29)) & (GFX_CACHE_BUCKETS - 1)];
}

static void CacheUnlink(RDP_GFX *gfx, UINT16 slot)
{
    GFX_CACHE_ENTRY *entry = &gfx->Cache[slot];
    if (entry->Newer) gfx->Cache[entry->Newer].Older = entry->Older; else gfx->Newest = entry->Older;
    if (entry->Older) gfx->Cache[entry->Older].Newer = entry->Newer; else gfx->Oldest = entry->Newer;
    entry->Newer = entry->Older = 0;
}

static void CacheLinkNewest(RDP_GFX *gfx, UINT16 slot)
{
    GFX_CACHE_ENTRY *entry = &gfx->Cache[slot];
    assert(entry->Used && !entry->Newer && !entry->Older && gfx->Newest != slot && gfx->Oldest != slot);
    entry->Older = gfx->Newest;
    if (gfx->Newest) gfx->Cache[gfx->Newest].Newer = slot;
    gfx->Newest = slot;
    if (!gfx->Oldest) gfx->Oldest = slot;
}

static void CacheTouch(RDP_GFX *gfx, UINT16 slot)
{
    assert(gfx->Cache[slot].Used);
    if (gfx->Newest == slot) return;
    CacheUnlink(gfx, slot);
    CacheLinkNewest(gfx, slot);
}

static UINT16 CacheFind(RDP_GFX *gfx, UINT64 key)
{
    UINT16 slot = *CacheBucket(gfx, key);
    while (slot && gfx->Cache[slot].Key != key) slot = gfx->Cache[slot].HashNext;
    return slot;
}

static BOOL CacheEvict(RDP_GFX *gfx)
{
    RDPGFX_EVICT_CACHE_ENTRY_PDU evict = {0};
    UINT16 slot = gfx->Oldest, *link;
    GFX_CACHE_ENTRY *entry = &gfx->Cache[slot];
    assert(slot && entry->Used);
    for (link = CacheBucket(gfx, entry->Key); *link != slot; link = &gfx->Cache[*link].HashNext) assert(*link);
    *link = entry->HashNext;
    CacheUnlink(gfx, slot);
    gfx->CacheBytes -= entry->Bytes;
    memset(entry, 0, sizeof(*entry));
    assert(gfx->FreeCount < gfx->CacheSlots);
    gfx->FreeSlots[gfx->FreeCount++] = slot;
    evict.cacheSlot = slot;
    return gfx->Context->EvictCacheEntry(gfx->Context, &evict) == CHANNEL_RC_OK;
}

static BOOL CacheStore(RDP_GFX *gfx, UINT64 key, const GFX_TILE *tile)
{
    RDPGFX_SURFACE_TO_CACHE_PDU store = {0};
    UINT32 bytes = (UINT32)tile->Width * tile->Height * 4;
    UINT16 slot, *bucket;
    GFX_CACHE_ENTRY *entry;
    while (gfx->Oldest && (gfx->CacheBytes + bytes > gfx->CacheLimit || (!gfx->FreeCount && gfx->NextFreeSlot > gfx->CacheSlots)))
        if (!CacheEvict(gfx)) return FALSE;
    if (gfx->FreeCount) slot = gfx->FreeSlots[--gfx->FreeCount];
    else slot = gfx->NextFreeSlot++;
    assert(slot && slot <= gfx->CacheSlots && !gfx->Cache[slot].Used && gfx->CacheBytes + bytes <= gfx->CacheLimit);
    entry = &gfx->Cache[slot];
    bucket = CacheBucket(gfx, key);
    entry->Key = key;
    entry->Bytes = bytes;
    entry->Used = TRUE;
    entry->HashNext = *bucket;
    *bucket = slot;
    gfx->CacheBytes += bytes;
    CacheLinkNewest(gfx, slot);
    store.surfaceId = GFX_SURFACE_ID;
    store.cacheKey = key;
    store.cacheSlot = slot;
    store.rectSrc.left = tile->Left;
    store.rectSrc.top = tile->Top;
    store.rectSrc.right = (UINT16)(tile->Left + tile->Width);
    store.rectSrc.bottom = (UINT16)(tile->Top + tile->Height);
    return gfx->Context->SurfaceToCache(gfx->Context, &store) == CHANNEL_RC_OK;
}

static BOOL SendCachedTile(RDP_GFX *gfx, UINT16 slot, const GFX_TILE *tile)
{
    RDPGFX_CACHE_TO_SURFACE_PDU copy = {0};
    RDPGFX_POINT16 point;
    CacheTouch(gfx, slot);
    point.x = tile->Left;
    point.y = tile->Top;
    copy.cacheSlot = slot;
    copy.surfaceId = GFX_SURFACE_ID;
    copy.destPtsCount = 1;
    copy.destPts = &point;
    return gfx->Context->CacheToSurface(gfx->Context, &copy) == CHANNEL_RC_OK;
}

static BOOL SendSolidTile(RDP_GFX *gfx, UINT32 color, const GFX_TILE *tile)
{
    RDPGFX_SOLID_FILL_PDU fill = {0};
    RECTANGLE_16 rect;
    rect.left = tile->Left;
    rect.top = tile->Top;
    rect.right = (UINT16)(tile->Left + tile->Width);
    rect.bottom = (UINT16)(tile->Top + tile->Height);
    fill.surfaceId = GFX_SURFACE_ID;
    fill.fillPixel.B = (BYTE)color;
    fill.fillPixel.G = (BYTE)(color >> 8);
    fill.fillPixel.R = (BYTE)(color >> 16);
    fill.fillRectCount = 1;
    fill.fillRects = &rect;
    return gfx->Context->SolidFill(gfx->Context, &fill) == CHANNEL_RC_OK;
}

static BOOL SendClearTile(RDP_GFX *gfx, const BYTE *pixels, UINT32 stride, const GFX_TILE *tile, BOOL *sent)
{
    RDPGFX_SURFACE_COMMAND command = {0};
    const BYTE *origin = pixels + (size_t)tile->Top * stride + (size_t)tile->Left * 4;
    UINT32 rlexSize = 0, residualSize = 0;
    BOOL rlex, residual;
    *sent = FALSE;
    rlex = ClearPaletteSize(origin, stride, tile->Width, tile->Height, CLEAR_MAX_PALETTE) <= CLEAR_MAX_PALETTE &&
           ClearEncode(gfx->ClearSequence, ClearRlex, origin, stride, tile->Width, tile->Height,
                       gfx->Clear, GFX_CLEAR_BYTES, &rlexSize);
    residual = ClearEncode(gfx->ClearSequence, ClearResidual, origin, stride, tile->Width, tile->Height,
                           gfx->Encoded, GFX_TILE_SIZE * GFX_TILE_SIZE * 4 + 2, &residualSize);
    if (!rlex && residual && residualSize > (UINT32)tile->Width * tile->Height * 3 / 2) residual = FALSE;
    if (!rlex && !residual) return TRUE;
    command.surfaceId = GFX_SURFACE_ID;
    command.codecId = RDPGFX_CODECID_CLEARCODEC;
    command.format = PIXEL_FORMAT_BGRX32;
    command.left = tile->Left;
    command.top = tile->Top;
    command.right = (UINT32)tile->Left + tile->Width;
    command.bottom = (UINT32)tile->Top + tile->Height;
    command.width = tile->Width;
    command.height = tile->Height;
    if (rlex && (!residual || rlexSize <= residualSize)) { command.data = gfx->Clear; command.length = rlexSize; }
    else { command.data = gfx->Encoded; command.length = residualSize; }
    if (gfx->Context->SurfaceCommand(gfx->Context, &command) != CHANNEL_RC_OK) return FALSE;
    ++gfx->ClearSequence;
    *sent = TRUE;
    return TRUE;
}

static BOOL SendTile(RDP_GFX *gfx, const BYTE *pixels, UINT32 stride, const GFX_TILE *tile, UINT32 *pending)
{
    UINT32 color;
    UINT64 key;
    UINT16 slot;
    BOOL sent;
    if (TileSolid(pixels, stride, tile, &color)) return SendSolidTile(gfx, color, tile);
    key = TileKey(pixels, stride, tile);
    slot = CacheFind(gfx, key);
    if (slot) return SendCachedTile(gfx, slot, tile);
    if (!SendClearTile(gfx, pixels, stride, tile, &sent)) return FALSE;
    if (sent) return CacheStore(gfx, key, tile);
    assert(*pending < gfx->PendingCapacity);
    gfx->Pending[*pending] = *tile;
    gfx->PendingKeys[(*pending)++] = key;
    return TRUE;
}

static BOOL SendProgressiveTiles(RDP_GFX *gfx, const BYTE *pixels, UINT32 stride, UINT32 count)
{
    RDPGFX_SURFACE_COMMAND command = {0};
    RDPGFX_DELETE_ENCODING_CONTEXT_PDU remove = {0};
    REGION16 region;
    UINT32 i;
    INT32 status;
    BOOL ok = TRUE;
    if (!count) return TRUE;
    region16_init(&region);
    for (i = 0; ok && i < count; ++i)
    {
        RECTANGLE_16 rect;
        const GFX_TILE *tile = &gfx->Pending[i];
        assert(tile->Left % GFX_TILE_SIZE == 0 && tile->Top % GFX_TILE_SIZE == 0);
        rect.left = tile->Left;
        rect.top = tile->Top;
        rect.right = (UINT16)(tile->Left + tile->Width);
        rect.bottom = (UINT16)(tile->Top + tile->Height);
        ok = region16_union_rect(&region, &region, &rect);
    }
    status = ok ? progressive_compress(gfx->Progressive, pixels, stride * gfx->Height, PIXEL_FORMAT_BGRX32, gfx->Width,
                                       gfx->Height, stride, &region, &command.data, &command.length) : -1;
    region16_uninit(&region);
    if (status < 0)
    {
        gfx->Log("Progressive encoding of %lu tiles failed: %ld", (ULONG)count, (long)status);
        return FALSE;
    }
    if (status > 0)
    {
        /* TODO: send the tiles in several progressive passes, as Windows 11 does, instead of one full-quality pass */
        gfx->ProgressiveContextId = gfx->ProgressiveContextId % 2 + 1;
        command.surfaceId = GFX_SURFACE_ID;
        command.codecId = RDPGFX_CODECID_CAPROGRESSIVE;
        command.contextId = gfx->ProgressiveContextId;
        command.format = PIXEL_FORMAT_BGRX32;
        if (gfx->Context->SurfaceCommand(gfx->Context, &command) != CHANNEL_RC_OK) return FALSE;
        remove.surfaceId = GFX_SURFACE_ID;
        remove.codecContextId = gfx->ProgressiveContextId;
        if (gfx->Context->DeleteEncodingContext(gfx->Context, &remove) != CHANNEL_RC_OK) return FALSE;
    }
    for (i = 0; i < count; ++i)
        if (!CacheStore(gfx, gfx->PendingKeys[i], &gfx->Pending[i])) return FALSE;
    return TRUE;
}

static BOOL ReservePending(RDP_GFX *gfx, UINT32 count)
{
    GFX_TILE *tiles;
    UINT64 *keys;
    if (count <= gfx->PendingCapacity) return TRUE;
    tiles = realloc(gfx->Pending, (size_t)count * sizeof(*tiles));
    if (!tiles) return FALSE;
    gfx->Pending = tiles;
    keys = realloc(gfx->PendingKeys, (size_t)count * sizeof(*keys));
    if (!keys) return FALSE;
    gfx->PendingKeys = keys;
    gfx->PendingCapacity = count;
    return TRUE;
}

static BOOL SendMove(RDP_GFX *gfx, const FRAME_MOVE *move)
{
    RDPGFX_SURFACE_TO_SURFACE_PDU copy = {0};
    RDPGFX_POINT16 point;
    assert(move->Dest.left >= 0 && move->Dest.right <= (LONG)gfx->Width);
    assert(move->Dest.top >= 0 && move->Dest.bottom <= (LONG)gfx->Height);
    copy.surfaceIdSrc = copy.surfaceIdDest = GFX_SURFACE_ID;
    copy.rectSrc.left = (UINT16)move->SourceLeft;
    copy.rectSrc.top = (UINT16)move->SourceTop;
    copy.rectSrc.right = (UINT16)(move->SourceLeft + move->Dest.right - move->Dest.left);
    copy.rectSrc.bottom = (UINT16)(move->SourceTop + move->Dest.bottom - move->Dest.top);
    point.x = (UINT16)move->Dest.left;
    point.y = (UINT16)move->Dest.top;
    copy.destPtsCount = 1;
    copy.destPts = &point;
    return gfx->Context->SurfaceToSurface(gfx->Context, &copy) == CHANNEL_RC_OK;
}

BOOL GfxSendFrame(RDP_GFX *gfx, const BYTE *pixels, UINT32 stride, const FRAME_MOVE *moves, UINT32 moveCount,
                  const GFX_TILE *tiles, UINT32 count)
{
    RDPGFX_START_FRAME_PDU start = {0};
    RDPGFX_END_FRAME_PDU end = {0};
    UINT32 i, pending = 0;
    assert(gfx->Active && gfx->SurfaceCreated && GfxFrameAllowed(gfx));
    if (!count && !moveCount) return TRUE;
    if (!ReservePending(gfx, count)) return FALSE;
    start.frameId = end.frameId = ++gfx->NextFrameId;
    start.timestamp = FrameTimestamp();
    if (gfx->Context->StartFrame(gfx->Context, &start) != CHANNEL_RC_OK) return FALSE;
    for (i = 0; i < moveCount; ++i)
        if (!SendMove(gfx, &moves[i])) return FALSE;
    for (i = 0; i < count; ++i)
        if (!SendTile(gfx, pixels, stride, &tiles[i], &pending)) return FALSE;
    if (!SendProgressiveTiles(gfx, pixels, stride, pending)) return FALSE;
    return gfx->Context->EndFrame(gfx->Context, &end) == CHANNEL_RC_OK;
}
