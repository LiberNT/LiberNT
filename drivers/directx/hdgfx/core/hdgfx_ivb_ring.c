/*
 * PROJECT:     LiberNT Intel HD Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Sandy Bridge and Ivy Bridge forcewake, engine setup, reset and ring command sequences
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "hdgfx_core.h"
#include "hdgfx_regs.h"

#define IVB_MASKED_ENABLE(Bits)     (((uint32_t)(Bits) << 16) | (uint32_t)(Bits))
#define IVB_MASKED_DISABLE(Bits)    ((uint32_t)(Bits) << 16)
#define IVB_MASKED_CLEAR_ALL        0xFFFF0000u

#define IVB_FORCEWAKE_KERNEL        0x1u
#define IVB_FORCEWAKE_TIMEOUT_US    50000u
#define IVB_THREAD_STATUS_MASK      0x7u
#define IVB_THREAD_STATUS_TIMEOUT_US 5000u
#define IVB_POLL_US                 10u
#define IVB_ECOBUS_MT_ENABLE        0x20u
#define IVB_GDRST_FULL              0x1u
#define IVB_GDRST_TIMEOUT_US        2000u
#define IVB_GDRST_PASSES            2u
#define IVB_GDRST_SETTLE_US         50u

#define IVB_L3_CONTROL              0x3C47FF8Cu
#define IVB_L3_CHICKEN_MODE         0x20000000u
#define IVB_L3SQ_URB_READ_CAM_MATCH_DISABLE 0x08000000u
#define IVB_CSC1_RHWO_OPT_DISABLE   0x400u
#define IVB_SF_DISABLE_OBJEND_CULL  0x400u
#define IVB_FF_SCHED_MASK           0x0077070u
#define IVB_TLB_INVALIDATE_EXPLICIT 0x2000u
#define IVB_REPLAY_MODE             0x800u
#define IVB_WIZ_HASHING_MASK        0x280u
#define IVB_WIZ_HASHING_16X4        0x200u
#define IVB_ASYNC_FLIP_PERF_DISABLE 0x4000u
#define IVB_INSTPM_FORCE_ORDERING   0x80u
#define IVB_RC_OP_FLUSH_ENABLE      0x1u
#define IVB_PIXEL_SUBSPAN_COLLECT_OPT_DISABLE 0x40u

#define SNB_VS_TIMER_DISPATCH       0x40u
#define SNB_HIZ_PLANE_DISABLE_MSAA_4X 0x400u
#define SNB_SF_DISABLE_FASTCLIP_CULL 0x20u
#define SNB_SF_DISABLE_PIPELINED_ATTR_FETCH 0x2u
#define SNB_STC_EVICT_DISABLE_LRA   0x20u
#define SNB_UCGCTL1_DISABLE         0xA0u
#define SNB_UCGCTL2_DISABLE         0x1800u

#define IVB_MI_NOOP                 0x00000000u
#define IVB_MI_USER_INTERRUPT       0x01000000u
#define IVB_MI_STORE_DWORD_INDEX    0x10800001u
#define IVB_MI_FLUSH_DW             0x13000001u
#define IVB_MI_FLUSH_DW_STORE_INDEX 0x00200000u
#define IVB_MI_FLUSH_DW_INVALIDATE_TLB 0x00040000u
#define IVB_MI_FLUSH_DW_POST_STORE  0x00004000u
#define IVB_MI_FLUSH_DW_USE_GTT     0x4u
#define IVB_MI_BATCH_BUFFER_START   0x18800000u
#define IVB_PIPE_CONTROL            0x7A000002u
#define IVB_PC_GLOBAL_GTT           0x01000000u
#define IVB_PC_CS_STALL             0x00100000u
#define IVB_PC_TLB_INVALIDATE       0x00040000u
#define IVB_PC_QW_WRITE             0x00004000u
#define IVB_PC_RENDER_TARGET_FLUSH  0x00001000u
#define IVB_PC_INSTRUCTION_INVALIDATE 0x00000800u
#define IVB_PC_TEXTURE_INVALIDATE   0x00000400u
#define IVB_PC_FLUSH_ENABLE         0x00000080u
#define IVB_PC_DC_FLUSH             0x00000020u
#define IVB_PC_VF_INVALIDATE        0x00000010u
#define IVB_PC_CONST_INVALIDATE     0x00000008u
#define IVB_PC_STATE_INVALIDATE     0x00000004u
#define IVB_PC_STALL_AT_SCOREBOARD  0x00000002u
#define IVB_PC_DEPTH_FLUSH          0x00000001u
#define SNB_PC_GLOBAL_GTT           0x00000004u
#define SNB_PC_WORKAROUND_DWORDS    8u
#define IVB_XCS_STORES              32u
#define IVB_HWS_FLUSH_INDEX         0x30u
#define IVB_HWS_DWORDS              1024u
#define IVB_HWS_FIRST_FREE          0x10u

static bool
IvbWaitFor(
    const HDGFX_MMIO *Mmio,
    uint32_t Offset,
    uint32_t Mask,
    uint32_t Value,
    uint32_t TimeoutUs)
{
    uint32_t Elapsed;

    for (Elapsed = 0;; Elapsed += IVB_POLL_US)
    {
        if ((Mmio->Read(Mmio->Context, Offset) & Mask) == Value)
            return true;
        if (Elapsed >= TimeoutUs)
            return false;
        Mmio->Stall(Mmio->Context, IVB_POLL_US);
    }
}

static bool
IvbForceWakeAcquire(
    const HDGFX_MMIO *Mmio,
    uint32_t Control,
    uint32_t Ack)
{
    if (!IvbWaitFor(Mmio, Ack, IVB_FORCEWAKE_KERNEL, 0, IVB_FORCEWAKE_TIMEOUT_US))
        return false;
    Mmio->Write(Mmio->Context, Control, IVB_MASKED_ENABLE(IVB_FORCEWAKE_KERNEL));
    if (!IvbWaitFor(Mmio, Ack, IVB_FORCEWAKE_KERNEL, IVB_FORCEWAKE_KERNEL, IVB_FORCEWAKE_TIMEOUT_US))
        return false;
    return IvbWaitFor(Mmio, HDGFX_REG(GT_THREAD_STATUS, 0), IVB_THREAD_STATUS_MASK, 0, IVB_THREAD_STATUS_TIMEOUT_US);
}

bool
HdgfxIvbForceWake(
    const HDGFX_MMIO *Mmio)
{
    Mmio->Write(Mmio->Context, HDGFX_REG(FORCEWAKE, 0), IVB_MASKED_CLEAR_ALL);
    Mmio->Write(Mmio->Context, HDGFX_REG(FORCEWAKE_MT, 0), IVB_MASKED_CLEAR_ALL);
    Mmio->Read(Mmio->Context, HDGFX_REG(ECOBUS, 0));

    if (IvbForceWakeAcquire(Mmio, HDGFX_REG(FORCEWAKE_MT, 0), HDGFX_REG(FORCEWAKE_MT_ACK, 0)) &&
        (Mmio->Read(Mmio->Context, HDGFX_REG(ECOBUS, 0)) & IVB_ECOBUS_MT_ENABLE) != 0)
    {
        return true;
    }
    Mmio->Write(Mmio->Context, HDGFX_REG(FORCEWAKE_MT, 0), IVB_MASKED_CLEAR_ALL);
    return IvbForceWakeAcquire(Mmio, HDGFX_REG(FORCEWAKE, 0), HDGFX_REG(FORCEWAKE_ACK, 0));
}

bool
HdgfxSnbForceWake(
    const HDGFX_MMIO *Mmio)
{
    Mmio->Write(Mmio->Context, HDGFX_REG(FORCEWAKE, 0), IVB_MASKED_CLEAR_ALL);
    Mmio->Read(Mmio->Context, HDGFX_REG(ECOBUS, 0));
    return IvbForceWakeAcquire(Mmio, HDGFX_REG(FORCEWAKE, 0), HDGFX_REG(FORCEWAKE_ACK, 0));
}

void
HdgfxSnbForceWakeRelease(
    const HDGFX_MMIO *Mmio)
{
    Mmio->Write(Mmio->Context, HDGFX_REG(FORCEWAKE, 0), IVB_MASKED_CLEAR_ALL);
}

void
HdgfxIvbForceWakeRelease(
    const HDGFX_MMIO *Mmio)
{
    Mmio->Write(Mmio->Context, HDGFX_REG(FORCEWAKE_MT, 0), IVB_MASKED_CLEAR_ALL);
    Mmio->Write(Mmio->Context, HDGFX_REG(FORCEWAKE, 0), IVB_MASKED_CLEAR_ALL);
}

void
HdgfxSnbInitEngine(
    const HDGFX_MMIO *Mmio)
{
    Mmio->Write(Mmio->Context, HDGFX_REG(SNB_UCGCTL1, 0),
                Mmio->Read(Mmio->Context, HDGFX_REG(SNB_UCGCTL1, 0)) | SNB_UCGCTL1_DISABLE);
    Mmio->Write(Mmio->Context, HDGFX_REG(SNB_UCGCTL2, 0), SNB_UCGCTL2_DISABLE);

    Mmio->Write(Mmio->Context, HDGFX_REG(RING_MI_MODE, 0),
                IVB_MASKED_ENABLE(IVB_ASYNC_FLIP_PERF_DISABLE | SNB_VS_TIMER_DISPATCH));
    Mmio->Write(Mmio->Context, HDGFX_REG(SNB_GFX_MODE, 0), IVB_MASKED_ENABLE(IVB_TLB_INVALIDATE_EXPLICIT));
    Mmio->Write(Mmio->Context, HDGFX_REG(SNB_3D_CHICKEN, 0), IVB_MASKED_ENABLE(SNB_HIZ_PLANE_DISABLE_MSAA_4X));
    Mmio->Write(Mmio->Context, HDGFX_REG(CHICKEN_3D3, 0),
                IVB_MASKED_ENABLE(SNB_SF_DISABLE_FASTCLIP_CULL | SNB_SF_DISABLE_PIPELINED_ATTR_FETCH));
    Mmio->Write(Mmio->Context, HDGFX_REG(SNB_GT_MODE, 0),
                IVB_MASKED_DISABLE(IVB_WIZ_HASHING_MASK) | IVB_WIZ_HASHING_16X4);
    Mmio->Write(Mmio->Context, HDGFX_REG(SNB_CACHE_MODE_0, 0),
                IVB_MASKED_DISABLE(IVB_RC_OP_FLUSH_ENABLE | SNB_STC_EVICT_DISABLE_LRA));
    Mmio->Write(Mmio->Context, HDGFX_REG(RING_INSTPM, 0), IVB_MASKED_ENABLE(IVB_INSTPM_FORCE_ORDERING));
}

void
HdgfxIvbInitEngine(
    const HDGFX_MMIO *Mmio)
{
    uint32_t Value;

    Mmio->Write(Mmio->Context, HDGFX_REG(COMMON_SLICE_CHICKEN1, 0), IVB_MASKED_DISABLE(IVB_CSC1_RHWO_OPT_DISABLE));
    Mmio->Write(Mmio->Context, HDGFX_REG(L3_CONTROL, 0), IVB_L3_CONTROL);
    Mmio->Write(Mmio->Context, HDGFX_REG(L3_CHICKEN_MODE, 0), IVB_L3_CHICKEN_MODE);
    Value = Mmio->Read(Mmio->Context, HDGFX_REG(L3SQ_CONTROL4, 0));
    Mmio->Write(Mmio->Context, HDGFX_REG(L3SQ_CONTROL4, 0), Value & ~IVB_L3SQ_URB_READ_CAM_MATCH_DISABLE);

    Mmio->Write(Mmio->Context, HDGFX_REG(CHICKEN_3D3, 0), IVB_MASKED_ENABLE(IVB_SF_DISABLE_OBJEND_CULL));
    Value = Mmio->Read(Mmio->Context, HDGFX_REG(FF_THREAD_MODE, 0));
    Mmio->Write(Mmio->Context, HDGFX_REG(FF_THREAD_MODE, 0), Value & ~IVB_FF_SCHED_MASK);
    Mmio->Write(Mmio->Context, HDGFX_REG(RENDER_RING_MODE, 0),
                IVB_MASKED_ENABLE(IVB_TLB_INVALIDATE_EXPLICIT | IVB_REPLAY_MODE));
    Mmio->Write(Mmio->Context, HDGFX_REG(GT_MODE, 0),
                IVB_MASKED_DISABLE(IVB_WIZ_HASHING_MASK) | IVB_WIZ_HASHING_16X4);
    Mmio->Write(Mmio->Context, HDGFX_REG(RING_MI_MODE, 0), IVB_MASKED_ENABLE(IVB_ASYNC_FLIP_PERF_DISABLE));
    Mmio->Write(Mmio->Context, HDGFX_REG(RING_INSTPM, 0), IVB_MASKED_ENABLE(IVB_INSTPM_FORCE_ORDERING));
    Mmio->Write(Mmio->Context, HDGFX_REG(CACHE_MODE_0, 0), IVB_MASKED_DISABLE(IVB_RC_OP_FLUSH_ENABLE));
    Mmio->Write(Mmio->Context, HDGFX_REG(CACHE_MODE_1, 0), IVB_MASKED_ENABLE(IVB_PIXEL_SUBSPAN_COLLECT_OPT_DISABLE));
}

bool
HdgfxIvbResetGpu(
    const HDGFX_MMIO *Mmio)
{
    uint32_t Pass;
    bool Done = true;

    for (Pass = 0; Pass < IVB_GDRST_PASSES && Done; Pass++)
    {
        Mmio->Write(Mmio->Context, HDGFX_REG(GDRST, 0), IVB_GDRST_FULL);
        Done = IvbWaitFor(Mmio, HDGFX_REG(GDRST, 0), IVB_GDRST_FULL, 0, IVB_GDRST_TIMEOUT_US);
    }
    Mmio->Stall(Mmio->Context, IVB_GDRST_SETTLE_US);
    return Done;
}

uint32_t
HdgfxIvbMiBatchStart(
    uint32_t *Dwords,
    uint32_t Address)
{
    if (Address % 8 != 0)
        return 0;
    Dwords[0] = IVB_MI_BATCH_BUFFER_START;
    Dwords[1] = Address;
    return 2;
}

static uint32_t
SnbPostSyncWorkaround(
    uint32_t *Dwords,
    uint32_t Scratch)
{
    Dwords[0] = IVB_PIPE_CONTROL;
    Dwords[1] = IVB_PC_CS_STALL | IVB_PC_STALL_AT_SCOREBOARD;
    Dwords[2] = 0;
    Dwords[3] = 0;
    Dwords[4] = IVB_PIPE_CONTROL;
    Dwords[5] = IVB_PC_QW_WRITE;
    Dwords[6] = Scratch | SNB_PC_GLOBAL_GTT;
    Dwords[7] = 0;
    return SNB_PC_WORKAROUND_DWORDS;
}

bool
HdgfxIvbEmitInvalidate(
    HDGFX_RING *Ring,
    uint32_t Head)
{
    uint32_t Dwords[SNB_PC_WORKAROUND_DWORDS + 4];
    uint32_t Scratch = Ring->StatusAddress + IVB_HWS_FLUSH_INDEX * 4;

    if (Ring->Blitter)
    {
        Dwords[0] = IVB_MI_FLUSH_DW | IVB_MI_FLUSH_DW_INVALIDATE_TLB | IVB_MI_FLUSH_DW_POST_STORE |
                    IVB_MI_FLUSH_DW_STORE_INDEX;
        Dwords[1] = (IVB_HWS_FLUSH_INDEX * 4) | IVB_MI_FLUSH_DW_USE_GTT;
        Dwords[2] = 0;
        Dwords[3] = IVB_MI_NOOP;
        return HdgfxRingEmit(Ring, Head, Dwords, 4);
    }

    if (Ring->Gen == 6)
    {
        uint32_t Count = SnbPostSyncWorkaround(Dwords, Scratch);

        Dwords[Count++] = IVB_PIPE_CONTROL;
        Dwords[Count++] = IVB_PC_CS_STALL | IVB_PC_QW_WRITE | IVB_PC_TLB_INVALIDATE |
                          IVB_PC_INSTRUCTION_INVALIDATE | IVB_PC_TEXTURE_INVALIDATE | IVB_PC_VF_INVALIDATE |
                          IVB_PC_CONST_INVALIDATE | IVB_PC_STATE_INVALIDATE;
        Dwords[Count++] = Scratch | SNB_PC_GLOBAL_GTT;
        Dwords[Count++] = 0;
        return HdgfxRingEmit(Ring, Head, Dwords, Count);
    }

    Dwords[0] = IVB_PIPE_CONTROL;
    Dwords[1] = IVB_PC_CS_STALL | IVB_PC_STALL_AT_SCOREBOARD;
    Dwords[2] = 0;
    Dwords[3] = 0;
    Dwords[4] = IVB_PIPE_CONTROL;
    Dwords[5] = IVB_PC_CS_STALL | IVB_PC_QW_WRITE | IVB_PC_GLOBAL_GTT | IVB_PC_TLB_INVALIDATE |
                IVB_PC_INSTRUCTION_INVALIDATE | IVB_PC_TEXTURE_INVALIDATE | IVB_PC_VF_INVALIDATE |
                IVB_PC_CONST_INVALIDATE | IVB_PC_STATE_INVALIDATE;
    Dwords[6] = Scratch;
    Dwords[7] = 0;
    return HdgfxRingEmit(Ring, Head, Dwords, 8);
}

bool
HdgfxIvbEmitBreadcrumb(
    HDGFX_RING *Ring,
    uint32_t Head,
    uint32_t StatusIndex,
    uint32_t Value)
{
    uint32_t Dwords[SNB_PC_WORKAROUND_DWORDS + 8 + IVB_XCS_STORES * 3];
    uint32_t Count = 0;
    uint32_t Index;

    if (StatusIndex < IVB_HWS_FIRST_FREE || StatusIndex >= IVB_HWS_DWORDS || StatusIndex % 2 != 0)
        return false;

    if (Ring->Gen == 6 && !Ring->Blitter)
    {
        Count = SnbPostSyncWorkaround(Dwords, Ring->StatusAddress + IVB_HWS_FLUSH_INDEX * 4);
        Dwords[Count++] = IVB_PIPE_CONTROL;
        Dwords[Count++] = IVB_PC_RENDER_TARGET_FLUSH | IVB_PC_DEPTH_FLUSH | IVB_PC_DC_FLUSH |
                          IVB_PC_QW_WRITE | IVB_PC_CS_STALL;
        Dwords[Count++] = (Ring->StatusAddress + StatusIndex * 4) | SNB_PC_GLOBAL_GTT;
        Dwords[Count++] = Value;
    }
    else if (Ring->Gen == 6)
    {
        Dwords[Count++] = IVB_MI_FLUSH_DW | IVB_MI_FLUSH_DW_POST_STORE | IVB_MI_FLUSH_DW_STORE_INDEX;
        Dwords[Count++] = (StatusIndex * 4) | IVB_MI_FLUSH_DW_USE_GTT;
        Dwords[Count++] = Value;
    }
    else if (!Ring->Blitter)
    {
        Dwords[Count++] = IVB_PIPE_CONTROL;
        Dwords[Count++] = IVB_PC_RENDER_TARGET_FLUSH | IVB_PC_DEPTH_FLUSH | IVB_PC_DC_FLUSH | IVB_PC_FLUSH_ENABLE |
                          IVB_PC_QW_WRITE | IVB_PC_GLOBAL_GTT | IVB_PC_CS_STALL;
        Dwords[Count++] = Ring->StatusAddress + StatusIndex * 4;
        Dwords[Count++] = Value;
    }
    else
    {
        Dwords[Count++] = IVB_MI_FLUSH_DW | IVB_MI_FLUSH_DW_INVALIDATE_TLB | IVB_MI_FLUSH_DW_POST_STORE |
                          IVB_MI_FLUSH_DW_STORE_INDEX;
        Dwords[Count++] = (StatusIndex * 4) | IVB_MI_FLUSH_DW_USE_GTT;
        Dwords[Count++] = Value;
        for (Index = 0; Index < IVB_XCS_STORES; Index++)
        {
            Dwords[Count++] = IVB_MI_STORE_DWORD_INDEX;
            Dwords[Count++] = StatusIndex * 4;
            Dwords[Count++] = Value;
        }
        Dwords[Count++] = IVB_MI_FLUSH_DW;
        Dwords[Count++] = 0;
        Dwords[Count++] = 0;
    }
    Dwords[Count++] = IVB_MI_USER_INTERRUPT;
    if (Count % 2 != 0)
        Dwords[Count++] = IVB_MI_NOOP;
    return HdgfxRingEmit(Ring, Head, Dwords, Count);
}
