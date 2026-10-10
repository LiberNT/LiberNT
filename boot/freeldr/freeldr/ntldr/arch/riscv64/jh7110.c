/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Keep the StarFive JH7110 HDMI output on the UEFI framebuffer after ExitBootServices
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <freeldr.h>
#include <drivers/bootvid/framebuf.h>
#include <reactos/riscv64/fdtlib.h>
#include <jh7110disp.h>

extern ULONG_PTR VramAddress;
extern PCM_FRAMEBUF_DEVICE_DATA FrameBufferData;

static ULONG RiscvJh7110TimebaseFrequency;

static VOID
RiscvJh7110Delay(
    _In_ ULONG Microseconds)
{
    ULONGLONG Start, Now, Ticks;

    Ticks = ((ULONGLONG)RiscvJh7110TimebaseFrequency * Microseconds + 999999ULL) / 1000000ULL;
    __asm__ __volatile__("rdtime %0" : "=r"(Start));
    do
    {
        __asm__ __volatile__("rdtime %0" : "=r"(Now));
    } while (Now - Start < Ticks);
}

VOID
RiscvJh7110ResumeDisplay(
    _In_reads_bytes_(DeviceTreeSize) const VOID *DeviceTree,
    _In_ ULONG DeviceTreeSize)
{
    const JH7110DISP_MODE *Mode = &Jh7110DispMode1080p60;
    JH7110DISP Display;
    RISCV_FDT Fdt;
    const VOID *Compatible;
    ULONGLONG FrameBuffer;
    ULONG Length, Cpus, Pitch;

    if (!DeviceTree || !RiscvFdtOpen(DeviceTree, DeviceTreeSize, &Fdt))
        return;
    Compatible = RiscvFdtGetProperty(&Fdt, RiscvFdtRootNode(&Fdt), "compatible", &Length);
    if (!RiscvFdtStringListContains(Compatible, Length, "starfive,jh7110") ||
        !RiscvFdtFindNode(&Fdt, "/cpus", &Cpus, NULL) ||
        !RiscvFdtReadU32(&Fdt, Cpus, "timebase-frequency", &RiscvJh7110TimebaseFrequency) ||
        !RiscvJh7110TimebaseFrequency)
    {
        return;
    }

    if (!FrameBufferData || !VramAddress ||
        FrameBufferData->ScreenWidth != Mode->Width ||
        FrameBufferData->ScreenHeight != Mode->Height ||
        FrameBufferData->BitsPerPixel != 32 ||
        FrameBufferData->PixelMasks.RedMask != 0x00FF0000 ||
        FrameBufferData->PixelMasks.GreenMask != 0x0000FF00 ||
        FrameBufferData->PixelMasks.BlueMask != 0x000000FF)
    {
        return;
    }
    FrameBuffer = (ULONGLONG)VramAddress + FrameBufferData->FrameBufferOffset;
    Pitch = FrameBufferData->PixelsPerScanLine * sizeof(ULONG);
    if (FrameBuffer + (ULONGLONG)Pitch * Mode->Height > JH_DC_HIGHEST_ADDRESS + 1)
        return;

    Display.Pmu = (PUCHAR)(ULONG_PTR)JH_PMU_BASE;
    Display.SysCrg = (PUCHAR)(ULONG_PTR)JH_SYSCRG_BASE;
    Display.VoutCrg = (PUCHAR)(ULONG_PTR)JH_VOUTCRG_BASE;
    Display.Dc = (PUCHAR)(ULONG_PTR)JH_DC_BASE;
    Display.Hdmi = (PUCHAR)(ULONG_PTR)JH_HDMI_BASE;
    Display.Mode = Mode;
    Display.Stall = RiscvJh7110Delay;
    Display.Sleep = RiscvJh7110Delay;

    Jh7110DispFlushCache((PUCHAR)(ULONG_PTR)JH_CCACHE_BASE, FrameBuffer, (SIZE_T)Pitch * Mode->Height);
    if (!Jh7110DispIsScanningOut(&Display))
        Jh7110DispStart(&Display, (ULONG)FrameBuffer, Pitch);
}
