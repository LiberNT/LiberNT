/*
 * PROJECT:     LiberNT Intel HD Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Ring buffers and global GTT entries
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "hdgfx_core.h"
#include "hdgfx_regs.h"

#define HDGFX_PAGE_SIZE             4096u
#define HDGFX_RING_MAX_SIZE         0x200000u
#define HDGFX_RING_ADDRESS_LIMIT    0x20000000u
#define HDGFX_MI_NOOP               0x00u
#define HDGFX_MI_USER_INTERRUPT     0x02u
#define HDGFX_MI_FLUSH              0x04u
#define HDGFX_MI_BATCH_BUFFER_END   0x0Au
#define HDGFX_MI_STORE_DATA_INDEX   0x21u
#define HDGFX_MI_BATCH_BUFFER_START 0x31u
#define HDGFX_MI_BATCH_GTT          0x80u
#define HDGFX_MI_BATCH_NON_SECURE   0x100u
#define HDGFX_HWS_FIRST_FREE        0x10u
#define HDGFX_HWS_DWORDS            1024u
#define HDGFX_HWS_SCRATCH           0x3Fu
#define HDGFX_BREADCRUMB_STORES     8u
#define HDGFX_GDSR_RENDER           1u
#define HDGFX_GDSR_TIMEOUT_US       5000u
#define HDGFX_GDSR_POLL_US          10u
#define HDGFX_GTT_UNCACHED          1u
#define HDGFX_INSTPM_SYNC_FLUSH     0x20u
#define HDGFX_INSTPM_SYNC_TLB       0x220u
#define HDGFX_INSTPM_POLLS          200u
#define HDGFX_MI_MODE_STOP_RING     0x100u

uint32_t
HdgfxIlkGttEntry(
    uint64_t Physical)
{
    return HDGFX_SET(GTT_PTE_ADDRESS, (uint32_t)Physical / HDGFX_PAGE_SIZE) | HDGFX_SET(GTT_PTE_VALID, 1);
}

uint32_t
HdgfxIvbGttEntry(
    uint64_t Physical)
{
    return HDGFX_SET(GTT_PTE_ADDRESS, (uint32_t)Physical / HDGFX_PAGE_SIZE) |
           HDGFX_SET(GTT_PTE_ADDRESS_HIGH, (uint32_t)(Physical >> 32)) |
           HDGFX_SET(GTT_PTE_CACHE, HDGFX_GTT_UNCACHED) | HDGFX_SET(GTT_PTE_VALID, 1);
}

static uint32_t
HdgfxMiCommand(
    uint32_t Opcode,
    uint32_t Length)
{
    return HDGFX_SET(MI_OPCODE, Opcode) | HDGFX_SET(MI_LENGTH, Length);
}

uint32_t
HdgfxMiFlush(
    uint32_t *Dwords)
{
    Dwords[0] = HdgfxMiCommand(HDGFX_MI_FLUSH, 0);
    return 1;
}

uint32_t
HdgfxMiBatchStart(
    uint32_t *Dwords,
    uint32_t Address)
{
    if (Address % 8 != 0)
        return 0;
    Dwords[0] = HdgfxMiCommand(HDGFX_MI_BATCH_BUFFER_START, 0) | HDGFX_MI_BATCH_GTT;
    Dwords[1] = Address;
    Dwords[2] = HdgfxMiCommand(HDGFX_MI_NOOP, 0);
    Dwords[3] = HdgfxMiCommand(HDGFX_MI_NOOP, 0);
    return 4;
}

uint32_t
HdgfxMiUserBatchStart(
    uint32_t *Dwords,
    uint32_t Address)
{
    uint32_t Count = HdgfxMiBatchStart(Dwords, Address);

    if (Count != 0)
        Dwords[0] |= HDGFX_MI_BATCH_NON_SECURE;
    return Count;
}

uint32_t
HdgfxMiBatchEnd(
    uint32_t *Dwords)
{
    Dwords[0] = HdgfxMiCommand(HDGFX_MI_BATCH_BUFFER_END, 0);
    return 1;
}

uint32_t
HdgfxIlkRingHead(
    const HDGFX_MMIO *Mmio,
    const HDGFX_RING *Ring)
{
    return HDGFX_GET(Mmio->Read(Mmio->Context, HDGFX_REG(RING_HEAD, 0) + Ring->Base), RING_HEAD_OFFSET) * 4;
}

bool
HdgfxIlkStartRing(
    const HDGFX_MMIO *Mmio,
    HDGFX_RING *Ring)
{
    uint32_t Control;
    uint32_t Mask = HDGFX_MASK(RING_CTL_LENGTH) | HDGFX_MASK(RING_CTL_ENABLE);
    uint32_t Poll;

    if (Ring->Commands == NULL ||
        Ring->Size < HDGFX_PAGE_SIZE || Ring->Size > HDGFX_RING_MAX_SIZE || Ring->Size % HDGFX_PAGE_SIZE != 0 ||
        Ring->Address % HDGFX_PAGE_SIZE != 0 || Ring->Address >= HDGFX_RING_ADDRESS_LIMIT ||
        HDGFX_RING_ADDRESS_LIMIT - Ring->Address < Ring->Size ||
        Ring->StatusAddress % HDGFX_PAGE_SIZE != 0)
    {
        return false;
    }

    Mmio->Write(Mmio->Context, HDGFX_REG(RING_CTL, 0) + Ring->Base, 0);
    Mmio->Write(Mmio->Context, HDGFX_REG(RING_TAIL, 0) + Ring->Base, 0);
    Mmio->Write(Mmio->Context, HDGFX_REG(RING_HEAD, 0) + Ring->Base, 0);
    Mmio->Write(Mmio->Context, HDGFX_REG(RING_START, 0) + Ring->Base, Ring->Address);
    Mmio->Write(Mmio->Context, Ring->StatusRegister ? Ring->StatusRegister : HDGFX_REG(HWS_PGA, 0), Ring->StatusAddress);
    if (Ring->Gen >= 6)
    {
        Mmio->Read(Mmio->Context, Ring->StatusRegister);
        Mmio->Write(Mmio->Context, HDGFX_REG(RING_HWSTAM, 0) + Ring->Base, 0xFFFFFFFFu);
        Mmio->Write(Mmio->Context, HDGFX_REG(RING_INSTPM, 0) + Ring->Base,
                    (HDGFX_INSTPM_SYNC_TLB << 16) | HDGFX_INSTPM_SYNC_TLB);
        for (Poll = 0; Poll < HDGFX_INSTPM_POLLS &&
             (Mmio->Read(Mmio->Context, HDGFX_REG(RING_INSTPM, 0) + Ring->Base) & HDGFX_INSTPM_SYNC_FLUSH) != 0; Poll++)
        {
            Mmio->Stall(Mmio->Context, HDGFX_GDSR_POLL_US);
        }
    }
    Control = HDGFX_SET(RING_CTL_LENGTH, Ring->Size / HDGFX_PAGE_SIZE - 1) | HDGFX_SET(RING_CTL_ENABLE, 1);
    Mmio->Write(Mmio->Context, HDGFX_REG(RING_CTL, 0) + Ring->Base, Control);
    Ring->Tail = 0;
    if (Ring->Gen >= 6)
    {
        Mmio->Write(Mmio->Context, HDGFX_REG(RING_IMR, 0) + Ring->Base, ~Ring->UserInterrupt);
        Mmio->Write(Mmio->Context, HDGFX_REG(RING_MI_MODE, 0) + Ring->Base, HDGFX_MI_MODE_STOP_RING << 16);
    }

    return Mmio->Read(Mmio->Context, HDGFX_REG(RING_START, 0) + Ring->Base) == Ring->Address &&
           (Mmio->Read(Mmio->Context, HDGFX_REG(RING_CTL, 0) + Ring->Base) & Mask) == Control &&
           HdgfxIlkRingHead(Mmio, Ring) == 0;
}

bool
HdgfxIlkStopRing(
    const HDGFX_MMIO *Mmio,
    const HDGFX_RING *Ring)
{
    if (HdgfxIlkRingHead(Mmio, Ring) != Ring->Tail)
        return false;

    Mmio->Write(Mmio->Context, HDGFX_REG(RING_CTL, 0) + Ring->Base, 0);
    Mmio->Write(Mmio->Context, HDGFX_REG(RING_HEAD, 0) + Ring->Base, 0);
    Mmio->Write(Mmio->Context, HDGFX_REG(RING_TAIL, 0) + Ring->Base, 0);
    return true;
}

static uint32_t
HdgfxRingSpace(
    const HDGFX_RING *Ring,
    uint32_t Head)
{
    Head &= ~7u;
    return Head > Ring->Tail ? Head - Ring->Tail - 8 : Ring->Size - (Ring->Tail - Head) - 8;
}

uint32_t
HdgfxRingFree(
    const HDGFX_RING *Ring,
    uint32_t Head)
{
    return HdgfxRingSpace(Ring, Head);
}

bool
HdgfxRingEmit(
    HDGFX_RING *Ring,
    uint32_t Head,
    const uint32_t *Dwords,
    uint32_t Count)
{
    uint32_t Bytes = Count * 4;
    uint32_t Padding = 0;
    uint32_t Index;

    if (Count == 0 || Count % 2 != 0 || Bytes >= Ring->Size || Head >= Ring->Size)
        return false;
    if (Ring->Size - Ring->Tail < Bytes)
        Padding = Ring->Size - Ring->Tail;
    if (HdgfxRingSpace(Ring, Head) < Padding + Bytes)
        return false;

    for (Index = 0; Index < Padding / 4; Index++)
        Ring->Commands[Ring->Tail / 4 + Index] = HdgfxMiCommand(HDGFX_MI_NOOP, 0);
    if (Padding != 0)
        Ring->Tail = 0;
    for (Index = 0; Index < Count; Index++)
        Ring->Commands[Ring->Tail / 4 + Index] = Dwords[Index];
    Ring->Tail = (Ring->Tail + Bytes) % Ring->Size;
    return true;
}

bool
HdgfxRingEmitBreadcrumb(
    HDGFX_RING *Ring,
    uint32_t Head,
    uint32_t StatusIndex,
    uint32_t Value)
{
    uint32_t Dwords[2 + HDGFX_BREADCRUMB_STORES * 6];
    uint32_t Count = 0;
    uint32_t Index;

    if (Ring->Gen >= 7)
        return HdgfxIvbEmitBreadcrumb(Ring, Head, StatusIndex, Value);
    if (StatusIndex < HDGFX_HWS_FIRST_FREE || StatusIndex >= HDGFX_HWS_DWORDS || StatusIndex == HDGFX_HWS_SCRATCH)
        return false;

    Dwords[Count++] = HdgfxMiCommand(HDGFX_MI_FLUSH, 0);
    for (Index = 0; Index < HDGFX_BREADCRUMB_STORES * 2; Index++)
    {
        Dwords[Count++] = HdgfxMiCommand(HDGFX_MI_STORE_DATA_INDEX, 1);
        Dwords[Count++] = HDGFX_SET(MI_STORE_INDEX_OFFSET,
                                    Index < HDGFX_BREADCRUMB_STORES ? HDGFX_HWS_SCRATCH : StatusIndex);
        Dwords[Count++] = Value;
    }
    Dwords[Count++] = HdgfxMiCommand(HDGFX_MI_USER_INTERRUPT, 0);
    return HdgfxRingEmit(Ring, Head, Dwords, Count);
}

void
HdgfxIlkAdvanceRing(
    const HDGFX_MMIO *Mmio,
    const HDGFX_RING *Ring)
{
    Mmio->Write(Mmio->Context, HDGFX_REG(RING_TAIL, 0) + Ring->Base, HDGFX_SET(RING_TAIL_OFFSET, Ring->Tail / 8));
}

bool
HdgfxIlkResetRender(
    const HDGFX_MMIO *Mmio)
{
    uint32_t Waited = 0;
    bool Done;

    Mmio->Write(Mmio->Context, HDGFX_REG(GDSR, 0),
                HDGFX_SET(GDSR_DOMAIN, HDGFX_GDSR_RENDER) | HDGFX_SET(GDSR_RESET, 1));
    while (!(Done = !HDGFX_GET(Mmio->Read(Mmio->Context, HDGFX_REG(GDSR, 0)), GDSR_RESET)) &&
           Waited < HDGFX_GDSR_TIMEOUT_US)
    {
        Mmio->Stall(Mmio->Context, HDGFX_GDSR_POLL_US);
        Waited += HDGFX_GDSR_POLL_US;
    }
    Mmio->Write(Mmio->Context, HDGFX_REG(GDSR, 0), 0);
    Mmio->Read(Mmio->Context, HDGFX_REG(GDSR, 0));
    return Done;
}
