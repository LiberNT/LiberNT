/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test how the graphics pipeline server groups the PDUs of a frame into channel messages
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include <stdlib.h>
#include <winpr/wtsapi.h>
#include <freerdp/server/rdpgfx.h>
#include <freerdp/codec/zgfx.h>
#include <freerdp/codec/color.h>

#define MAX_WRITES 64
#define MAX_PDUS 256
#define BIG_WIDTH 32
#define BIG_HEIGHT 40
#define BIG_COUNT 40

typedef struct MESSAGE
{
    UINT16 Commands[MAX_PDUS];
    UINT32 Count, Size;
} MESSAGE;

static BYTE ChannelObject[4096];
static HANDLE ChannelEvent;
static MESSAGE Messages[MAX_WRITES];
static UINT32 MessageCount, BadMessages;
static ZGFX_CONTEXT *Decompressor;
static BYTE BigPixels[BIG_WIDTH * BIG_HEIGHT * 4];

static BOOL WINAPI FakeQuerySessionInformationA(HANDLE server, DWORD session, WTS_INFO_CLASS infoClass, LPSTR *buffer,
                                                DWORD *returned)
{
    ULONG *id = malloc(sizeof(*id));
    if (!id) return FALSE;
    *id = 1;
    *buffer = (LPSTR)id;
    *returned = sizeof(*id);
    return TRUE;
}

static HANDLE WINAPI FakeVirtualChannelOpenEx(DWORD session, LPSTR name, DWORD flags)
{
    return ChannelObject;
}

static BOOL WINAPI FakeVirtualChannelClose(HANDLE channel)
{
    return channel == ChannelObject;
}

static BOOL WINAPI FakeVirtualChannelQuery(HANDLE channel, WTS_VIRTUAL_CLASS virtualClass, PVOID *buffer, DWORD *returned)
{
    HANDLE *event;
    if (channel != ChannelObject || virtualClass != WTSVirtualEventHandle) return FALSE;
    event = malloc(sizeof(*event));
    if (!event) return FALSE;
    *event = ChannelEvent;
    *buffer = event;
    *returned = sizeof(*event);
    return TRUE;
}

static BOOL WINAPI FakeVirtualChannelWrite(HANDLE channel, PCHAR buffer, ULONG length, PULONG written)
{
    BYTE *data = NULL;
    UINT32 size = 0, offset = 0;
    MESSAGE *message;
    if (channel != ChannelObject || MessageCount >= MAX_WRITES) return FALSE;
    message = &Messages[MessageCount++];
    memset(message, 0, sizeof(*message));
    if (zgfx_decompress(Decompressor, (const BYTE *)buffer, length, &data, &size, 0) < 0)
    {
        ++BadMessages;
        return FALSE;
    }
    message->Size = size;
    while (offset + 8 <= size && message->Count < MAX_PDUS)
    {
        UINT32 pduLength = data[offset + 4] | data[offset + 5] << 8 | data[offset + 6] << 16 | (UINT32)data[offset + 7] << 24;
        if (pduLength < 8 || pduLength > size - offset) break;
        message->Commands[message->Count++] = (UINT16)(data[offset] | data[offset + 1] << 8);
        offset += pduLength;
    }
    if (offset != size) ++BadMessages;
    free(data);
    *written = length;
    return TRUE;
}

static VOID WINAPI FakeFreeMemory(PVOID memory)
{
    free(memory);
}

static UINT SendStart(RdpgfxServerContext *context, UINT32 frameId)
{
    RDPGFX_START_FRAME_PDU start = {0};
    start.frameId = frameId;
    return context->StartFrame(context, &start);
}

static UINT SendEnd(RdpgfxServerContext *context, UINT32 frameId)
{
    RDPGFX_END_FRAME_PDU end = {0};
    end.frameId = frameId;
    return context->EndFrame(context, &end);
}

static UINT SendFill(RdpgfxServerContext *context)
{
    RDPGFX_SOLID_FILL_PDU fill = {0};
    RECTANGLE_16 rect = { 0, 0, 16, 16 };
    fill.surfaceId = 1;
    fill.fillRectCount = 1;
    fill.fillRects = &rect;
    return context->SolidFill(context, &fill);
}

static UINT SendEvict(RdpgfxServerContext *context)
{
    RDPGFX_EVICT_CACHE_ENTRY_PDU evict = {0};
    evict.cacheSlot = 1;
    return context->EvictCacheEntry(context, &evict);
}

static UINT SendBig(RdpgfxServerContext *context)
{
    RDPGFX_SURFACE_COMMAND command = {0};
    command.surfaceId = 1;
    command.codecId = RDPGFX_CODECID_UNCOMPRESSED;
    command.format = PIXEL_FORMAT_BGRX32;
    command.right = command.width = BIG_WIDTH;
    command.bottom = command.height = BIG_HEIGHT;
    command.data = BigPixels;
    command.length = sizeof(BigPixels);
    return context->SurfaceCommand(context, &command);
}

static BOOL HasCommands(const MESSAGE *message, const UINT16 *commands, UINT32 count)
{
    return message->Count == count && !memcmp(message->Commands, commands, count * sizeof(*commands));
}

START_TEST(GfxBatch)
{
    static WtsApiFunctionTable table;
    RdpgfxServerContext *context;
    RDPGFX_CAPSET capsSet = {0};
    RDPGFX_CAPS_CONFIRM_PDU confirm = {0};
    UINT32 i, first, total, largest;
    UINT32 random = 0x13579BDF;
    BOOL ordered;

    table.pQuerySessionInformationA = FakeQuerySessionInformationA;
    table.pVirtualChannelOpenEx = FakeVirtualChannelOpenEx;
    table.pVirtualChannelClose = FakeVirtualChannelClose;
    table.pVirtualChannelQuery = FakeVirtualChannelQuery;
    table.pVirtualChannelWrite = FakeVirtualChannelWrite;
    table.pFreeMemory = FakeFreeMemory;
    ok(WTSRegisterWtsApiFunctionTable(&table), "WTSRegisterWtsApiFunctionTable failed\n");
    ChannelEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Decompressor = zgfx_context_new(FALSE);
    context = rdpgfx_server_context_new((HANDLE)ChannelObject);
    ok(ChannelEvent && Decompressor && context, "setup failed\n");
    if (!ChannelEvent || !Decompressor || !context) return;
    for (i = 0; i < sizeof(BigPixels); ++i)
    {
        random = random * 1103515245u + 12345u;
        BigPixels[i] = (BYTE)(random >> 16);
    }

    ok(context->Initialize(context, TRUE), "Initialize failed\n");
    ok(context->Open(context), "Open failed\n");
    capsSet.version = RDPGFX_CAPVERSION_107;
    capsSet.length = 4;
    confirm.capsSet = &capsSet;
    ok(context->CapsConfirm(context, &confirm) == CHANNEL_RC_OK, "CapsConfirm failed\n");
    ok(MessageCount == 1, "caps confirm went out in %lu messages\n", (ULONG)MessageCount);
    ok(MessageCount >= 1 && Messages[0].Count == 1 && Messages[0].Commands[0] == RDPGFX_CMDID_CAPSCONFIRM,
       "the first message is not a lone caps confirm\n");

    {
        static const UINT16 expected[] = { RDPGFX_CMDID_STARTFRAME, RDPGFX_CMDID_SOLIDFILL, RDPGFX_CMDID_SOLIDFILL,
                                           RDPGFX_CMDID_EVICTCACHEENTRY, RDPGFX_CMDID_SOLIDFILL, RDPGFX_CMDID_ENDFRAME };
        first = MessageCount;
        ok(SendStart(context, 1) == CHANNEL_RC_OK, "StartFrame failed\n");
        ok(SendFill(context) == CHANNEL_RC_OK && SendFill(context) == CHANNEL_RC_OK, "SolidFill failed\n");
        ok(SendEvict(context) == CHANNEL_RC_OK, "EvictCacheEntry failed\n");
        ok(SendFill(context) == CHANNEL_RC_OK, "SolidFill failed\n");
        ok(MessageCount == first, "%lu messages went out before the end of the frame\n", (ULONG)(MessageCount - first));
        ok(SendEnd(context, 1) == CHANNEL_RC_OK, "EndFrame failed\n");
        ok(MessageCount == first + 1, "a small frame went out in %lu messages\n", (ULONG)(MessageCount - first));
        ok(MessageCount == first + 1 && HasCommands(&Messages[first], expected, ARRAYSIZE(expected)),
           "the frame message does not hold the frame's PDUs in order\n");
    }

    first = MessageCount;
    ok(SendEvict(context) == CHANNEL_RC_OK, "EvictCacheEntry failed\n");
    ok(MessageCount == first + 1 && Messages[first].Count == 1 && Messages[first].Commands[0] == RDPGFX_CMDID_EVICTCACHEENTRY,
       "a PDU outside a frame was not sent at once\n");

    first = MessageCount;
    ok(SendStart(context, 2) == CHANNEL_RC_OK, "StartFrame failed\n");
    for (i = 0; i < BIG_COUNT; ++i)
        ok(SendBig(context) == CHANNEL_RC_OK, "SurfaceCommand %lu failed\n", (ULONG)i);
    ok(SendEnd(context, 2) == CHANNEL_RC_OK, "EndFrame failed\n");
    ok(MessageCount - first >= (BIG_COUNT * sizeof(BigPixels)) / ZGFX_SEGMENTED_MAXSIZE + 1,
       "a large frame went out in only %lu messages\n", (ULONG)(MessageCount - first));
    for (total = 0, largest = 0, ordered = TRUE, i = first; i < MessageCount; ++i)
    {
        UINT32 j;
        largest = max(largest, Messages[i].Size);
        for (j = 0; j < Messages[i].Count; ++j, ++total)
        {
            UINT16 expected = total == 0 ? RDPGFX_CMDID_STARTFRAME : total == BIG_COUNT + 1 ? RDPGFX_CMDID_ENDFRAME
                                                                                         : RDPGFX_CMDID_WIRETOSURFACE_1;
            if (Messages[i].Commands[j] != expected) ordered = FALSE;
        }
    }
    ok(total == BIG_COUNT + 2, "the large frame carried %lu PDUs\n", (ULONG)total);
    ok(ordered, "the large frame's PDUs are out of order\n");
    ok(largest <= ZGFX_SEGMENTED_MAXSIZE, "a message of a large frame holds %lu bytes\n", (ULONG)largest);

    {
        static const UINT16 unfinished[] = { RDPGFX_CMDID_STARTFRAME, RDPGFX_CMDID_SOLIDFILL };
        static const UINT16 next[] = { RDPGFX_CMDID_STARTFRAME, RDPGFX_CMDID_ENDFRAME };
        first = MessageCount;
        ok(SendStart(context, 3) == CHANNEL_RC_OK && SendFill(context) == CHANNEL_RC_OK, "frame 3 failed\n");
        ok(SendStart(context, 4) == CHANNEL_RC_OK, "StartFrame failed\n");
        ok(MessageCount == first + 1 && HasCommands(&Messages[first], unfinished, ARRAYSIZE(unfinished)),
           "a new frame did not send the unfinished one first\n");
        ok(SendEnd(context, 4) == CHANNEL_RC_OK, "EndFrame failed\n");
        ok(MessageCount == first + 2 && HasCommands(&Messages[first + 1], next, ARRAYSIZE(next)),
           "the next frame did not go out alone\n");
    }

    first = MessageCount;
    ok(SendStart(context, 5) == CHANNEL_RC_OK && SendFill(context) == CHANNEL_RC_OK, "frame 5 failed\n");
    ok(context->Close(context), "Close failed\n");
    ok(MessageCount == first, "closing the channel sent %lu messages\n", (ULONG)(MessageCount - first));
    ok(BadMessages == 0, "%lu messages did not decompress into whole PDUs\n", (ULONG)BadMessages);

    rdpgfx_server_context_free(context);
    zgfx_context_free(Decompressor);
    CloseHandle(ChannelEvent);
}
