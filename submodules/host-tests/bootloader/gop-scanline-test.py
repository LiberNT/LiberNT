#!/usr/bin/env python3
# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     The frame buffer handed to the kernel uses the scan line the firmware draws with, not a shorter one it reports
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import pathlib
import re
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
source = (pathlib.Path(args.root) / 'bootloader/uefi/platform.c').read_text()


def extract(pattern, what, required=True):
    match = re.search(pattern, source, re.M | re.S)
    if not match:
        if required:
            raise RuntimeError(what + ' not found in bootloader/uefi/platform.c')
        return ''
    return match.group(0) + '\n'


defines = ''.join(line + '\n' for line in source.splitlines() if line.startswith('#define FW_'))
transform = extract(r'^typedef struct _FW_GOP_TRANSFORM\n\{.*?^\} FW_GOP_TRANSFORM, \*PFW_GOP_TRANSFORM;', 'FW_GOP_TRANSFORM')
measure = extract(r'^static\nUINT32\nFwMeasureScanLine\(.*?^\}\n', 'FwMeasureScanLine', required=False)
handoff = extract(r'^BOOLEAN\nFwGetFrameBuffer\(.*?^\}\n', 'FwGetFrameBuffer')

fixture = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char BOOLEAN, UINT8;
typedef unsigned int ULONG, UINT32, *PULONG;
typedef unsigned long long ULONGLONG, UINT64, *PULONGLONG;
typedef uintptr_t ULONG_PTR;
typedef size_t UINTN;
typedef intptr_t INTN;
typedef UINTN EFI_STATUS;
typedef UINT64 EFI_PHYSICAL_ADDRESS;
#define VOID void
#define TRUE 1
#define FALSE 0
#define EFIAPI
#define _In_
#define _Out_
#define MAXULONG 0xFFFFFFFFu
#define EFI_SUCCESS 0
#define EFI_INVALID_PARAMETER (((UINTN)1 << (sizeof(UINTN) * 8 - 1)) | 2)
#define EFI_DEVICE_ERROR (((UINTN)1 << (sizeof(UINTN) * 8 - 1)) | 7)
#define EFI_ERROR(Status) ((INTN)(Status) < 0)
#define RtlZeroMemory(Destination, Length) memset((Destination), 0, (Length))
#define MemoryBarrier() __sync_synchronize()
#define ASSERT(Condition) do { if (!(Condition)) { printf("ASSERT %s\n", #Condition); abort(); } } while (0)
typedef struct { UINT32 Data1; unsigned short Data2, Data3; UINT8 Data4[8]; } EFI_GUID;
typedef enum { PixelRedGreenBlueReserved8BitPerColor, PixelBlueGreenRedReserved8BitPerColor, PixelBitMask, PixelBltOnly } EFI_GRAPHICS_PIXEL_FORMAT;
typedef struct { UINT32 RedMask, GreenMask, BlueMask, ReservedMask; } EFI_PIXEL_BITMASK;
typedef struct { UINT32 Version, HorizontalResolution, VerticalResolution; EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
                 EFI_PIXEL_BITMASK PixelInformation; UINT32 PixelsPerScanLine; } EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;
typedef struct { UINT32 MaxMode, Mode; EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info; UINTN SizeOfInfo;
                 EFI_PHYSICAL_ADDRESS FrameBufferBase; UINTN FrameBufferSize; } EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;
typedef struct { UINT8 Blue, Green, Red, Reserved; } EFI_GRAPHICS_OUTPUT_BLT_PIXEL;
typedef enum { EfiBltVideoFill, EfiBltVideoToBltBuffer, EfiBltBufferToVideo, EfiBltVideoToVideo } EFI_GRAPHICS_OUTPUT_BLT_OPERATION;
typedef struct _EFI_GRAPHICS_OUTPUT_PROTOCOL EFI_GRAPHICS_OUTPUT_PROTOCOL;
struct _EFI_GRAPHICS_OUTPUT_PROTOCOL
{
    VOID* QueryMode;
    VOID* SetMode;
    EFI_STATUS (EFIAPI* Blt)(EFI_GRAPHICS_OUTPUT_PROTOCOL*, EFI_GRAPHICS_OUTPUT_BLT_PIXEL*, EFI_GRAPHICS_OUTPUT_BLT_OPERATION,
                             UINTN, UINTN, UINTN, UINTN, UINTN, UINTN, UINTN);
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE* Mode;
};
typedef struct { EFI_STATUS (EFIAPI* LocateProtocol)(EFI_GUID*, VOID*, VOID**); } EFI_BOOT_SERVICES;
typedef struct { EFI_BOOT_SERVICES* BootServices; } EFI_SYSTEM_TABLE;
typedef struct { ULONG RedMask, GreenMask, BlueMask, ReservedMask; } PIXEL_BITMASK;
typedef struct { ULONG ScreenWidth, ScreenHeight, PixelsPerScanLine, BitsPerPixel; PIXEL_BITMASK PixelMasks;
                 ULONG LogicalWidth, LogicalHeight, TransformFlags, Rotation; } CM_FRAMEBUF_DEVICE_DATA, *PCM_FRAMEBUF_DEVICE_DATA;
enum { LoaderFramebufferRotationIdentity, LoaderFramebufferRotation90, LoaderFramebufferRotation180, LoaderFramebufferRotation270 };
#define LOADER_PARAMETER_FRAMEBUFFER_FLAG_FIXED_SCANOUT 1
static EFI_SYSTEM_TABLE* GlobalSystemTable;
static EFI_GUID FwGraphicsOutputGuid;
'''

stub = r'''
static PFW_GOP_TRANSFORM FwGetGopTransform(EFI_GRAPHICS_OUTPUT_PROTOCOL* Gop)
{
    (void)Gop;
    return NULL;
}
'''

harness = r'''
typedef struct
{
    EFI_GRAPHICS_OUTPUT_PROTOCOL Protocol;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION Info;
    UINT8* Memory;
    UINT8* Drawn;
    UINT32 DrawnWidth, DrawnStride;
    int FailRead;
    unsigned Blts;
} FAKE_GOP;

static FAKE_GOP* Current;

static EFI_STATUS EFIAPI FakeLocate(EFI_GUID* Guid, VOID* Registration, VOID** Interface)
{
    (void)Guid;
    (void)Registration;
    *Interface = &Current->Protocol;
    return EFI_SUCCESS;
}

static UINT32 Pack(FAKE_GOP* Gop, const EFI_GRAPHICS_OUTPUT_BLT_PIXEL* Pixel)
{
    if (Gop->Info.PixelFormat == PixelRedGreenBlueReserved8BitPerColor)
        return Pixel->Red | (UINT32)Pixel->Green << 8 | (UINT32)Pixel->Blue << 16 | (UINT32)Pixel->Reserved << 24;
    return Pixel->Blue | (UINT32)Pixel->Green << 8 | (UINT32)Pixel->Red << 16 | (UINT32)Pixel->Reserved << 24;
}

static void Unpack(FAKE_GOP* Gop, UINT32 Value, EFI_GRAPHICS_OUTPUT_BLT_PIXEL* Pixel)
{
    UINT8 Low = (UINT8)Value, High = (UINT8)(Value >> 16);

    Pixel->Green = (UINT8)(Value >> 8);
    Pixel->Reserved = (UINT8)(Value >> 24);
    Pixel->Red = (Gop->Info.PixelFormat == PixelRedGreenBlueReserved8BitPerColor) ? Low : High;
    Pixel->Blue = (Gop->Info.PixelFormat == PixelRedGreenBlueReserved8BitPerColor) ? High : Low;
}

static EFI_STATUS EFIAPI FakeBlt(EFI_GRAPHICS_OUTPUT_PROTOCOL* This, EFI_GRAPHICS_OUTPUT_BLT_PIXEL* Buffer,
                                 EFI_GRAPHICS_OUTPUT_BLT_OPERATION Operation, UINTN SourceX, UINTN SourceY,
                                 UINTN DestinationX, UINTN DestinationY, UINTN Width, UINTN Height, UINTN Delta)
{
    FAKE_GOP* Gop = (FAKE_GOP*)This;
    UINTN X = (Operation == EfiBltVideoToBltBuffer) ? SourceX : DestinationX;
    UINTN Y = (Operation == EfiBltVideoToBltBuffer) ? SourceY : DestinationY;
    UINT32 Value;

    (void)Delta;
    Gop->Blts++;
    if (Width != 1 || Height != 1 || X + Width > Gop->DrawnWidth || Y + Height > Gop->Info.VerticalResolution)
        return EFI_INVALID_PARAMETER;

    if (Operation == EfiBltVideoToBltBuffer)
    {
        if (Gop->FailRead)
            return EFI_DEVICE_ERROR;
        memcpy(&Value, Gop->Drawn + (Y * Gop->DrawnStride + X) * 4, 4);
        Unpack(Gop, Value, Buffer);
        return EFI_SUCCESS;
    }

    if (Operation != EfiBltBufferToVideo)
        return EFI_INVALID_PARAMETER;

    Value = Pack(Gop, Buffer);
    memcpy(Gop->Drawn + (Y * Gop->DrawnStride + X) * 4, &Value, 4);
    return EFI_SUCCESS;
}

static void Setup(FAKE_GOP* Gop, UINT32 Width, UINT32 Height, UINT32 Reported, UINT32 Drawn, UINTN Size,
                  EFI_GRAPHICS_PIXEL_FORMAT Format)
{
    UINTN i;
    UINT32 Seed = 0x12345678;

    memset(Gop, 0, sizeof(*Gop));
    Gop->Protocol.Blt = FakeBlt;
    Gop->Protocol.Mode = &Gop->Mode;
    Gop->Mode.Info = &Gop->Info;
    Gop->Mode.SizeOfInfo = sizeof(Gop->Info);
    Gop->Info.HorizontalResolution = Width;
    Gop->Info.VerticalResolution = Height;
    Gop->Info.PixelsPerScanLine = Reported;
    Gop->Info.PixelFormat = Format;
    Gop->Memory = malloc(Size);
    for (i = 0; i < Size; i++)
    {
        Seed = Seed * 1103515245 + 12345;
        Gop->Memory[i] = (UINT8)(Seed >> 16);
    }
    Gop->Mode.FrameBufferBase = (EFI_PHYSICAL_ADDRESS)(ULONG_PTR)Gop->Memory;
    Gop->Mode.FrameBufferSize = Size;
    Gop->Drawn = Gop->Memory;
    Gop->DrawnWidth = Width;
    Gop->DrawnStride = Drawn;
}

static int Failures;

static void Check(int Condition, const char* Case, const char* What)
{
    if (!Condition)
    {
        printf("FAIL %s: %s\n", Case, What);
        Failures++;
    }
}

static void Run(const char* Case, FAKE_GOP* Gop, UINT32 Expected, unsigned ExpectBlts, const UINT8* Before)
{
    CM_FRAMEBUF_DEVICE_DATA Data;
    ULONGLONG Base = 0;
    ULONG Size = 0;
    char What[160];

    Current = Gop;
    Check(FwGetFrameBuffer(&Data, &Base, &Size), Case, "the frame buffer is handed off");
    snprintf(What, sizeof(What), "scan line %u, expected %u", Data.PixelsPerScanLine, Expected);
    Check(Data.PixelsPerScanLine == Expected, Case, What);
    Check(Data.ScreenWidth == Gop->Info.HorizontalResolution && Data.ScreenHeight == Gop->Info.VerticalResolution,
          Case, "the visible size is the reported mode");
    Check(Base == Gop->Mode.FrameBufferBase && Size == Gop->Mode.FrameBufferSize, Case, "base and size are the firmware's");
    snprintf(What, sizeof(What), "%u firmware Blt calls, expected %u", Gop->Blts, ExpectBlts);
    Check(Gop->Blts == ExpectBlts, Case, What);
    Check(memcmp(Before, Gop->Memory, Gop->Mode.FrameBufferSize) == 0, Case, "the screen is left as it was");
}

static UINT8* Snapshot(FAKE_GOP* Gop)
{
    UINT8* Copy = malloc(Gop->Mode.FrameBufferSize);

    memcpy(Copy, Gop->Memory, Gop->Mode.FrameBufferSize);
    return Copy;
}

static void Release(FAKE_GOP* Gop, UINT8* Before)
{
    free(Before);
    free(Gop->Memory);
    if (Gop->Drawn != Gop->Memory)
        free(Gop->Drawn);
}

int main(void)
{
    EFI_BOOT_SERVICES BootServices = { FakeLocate };
    EFI_SYSTEM_TABLE SystemTable = { &BootServices };
    FAKE_GOP Gop;
    UINT8* Before;
    UINT32 Magenta = 0x00FF00FF;
    UINTN i;

    GlobalSystemTable = &SystemTable;

    /* Haswell laptop panel: 1366x768, 1366 reported, 4227072 bytes = 768 rows of 5504 */
    Setup(&Gop, 1366, 768, 1366, 1376, 4227072, PixelBlueGreenRedReserved8BitPerColor);
    Before = Snapshot(&Gop);
    Run("1366 reported, 1376 drawn", &Gop, 1376, 4, Before);
    Release(&Gop, Before);

    Setup(&Gop, 1366, 768, 1366, 1376, 4227072, PixelRedGreenBlueReserved8BitPerColor);
    Before = Snapshot(&Gop);
    Run("1366 reported, 1376 drawn, RGB", &Gop, 1376, 4, Before);
    Release(&Gop, Before);

    Setup(&Gop, 1360, 860, 1360, 1370, 1370 * 860 * 4, PixelBlueGreenRedReserved8BitPerColor);
    Before = Snapshot(&Gop);
    Run("1360 reported, 1370 drawn", &Gop, 1370, 4, Before);
    Release(&Gop, Before);

    Setup(&Gop, 1024, 768, 1024, 1024, 1024 * 768 * 4, PixelBlueGreenRedReserved8BitPerColor);
    Before = Snapshot(&Gop);
    Run("consistent report", &Gop, 1024, 0, Before);
    Release(&Gop, Before);

    Setup(&Gop, 1366, 768, 1376, 1376, 4227072, PixelBlueGreenRedReserved8BitPerColor);
    Before = Snapshot(&Gop);
    Run("padded scan line reported", &Gop, 1376, 0, Before);
    Release(&Gop, Before);

    Setup(&Gop, 1370, 860, 1370, 1370, (1370 * 860 * 4 + 4095) & ~4095u, PixelBlueGreenRedReserved8BitPerColor);
    Before = Snapshot(&Gop);
    Run("page-rounded size, correct report", &Gop, 1370, 4, Before);
    Release(&Gop, Before);

    Setup(&Gop, 1366, 768, 1366, 1376, 4227072, PixelBlueGreenRedReserved8BitPerColor);
    Gop.Drawn = calloc(1, 4227072);
    Before = Snapshot(&Gop);
    Run("Blt draws into a hidden buffer", &Gop, 1366, 3, Before);
    Release(&Gop, Before);

    Setup(&Gop, 1366, 768, 1366, 1376, 4227072, PixelBlueGreenRedReserved8BitPerColor);
    Gop.FailRead = 1;
    Before = Snapshot(&Gop);
    Run("Blt cannot read the screen", &Gop, 1366, 1, Before);
    Release(&Gop, Before);

    Setup(&Gop, 1366, 768, 1366, 1376, 4227072, PixelBlueGreenRedReserved8BitPerColor);
    for (i = 0; i < 4227072 / 4; i++)
        memcpy(Gop.Memory + i * 4, &Magenta, 4);
    Before = Snapshot(&Gop);
    Run("magenta screen", &Gop, 1366, 3, Before);
    Release(&Gop, Before);

    Setup(&Gop, 1366, 768, 1366, 1376, 4227072, PixelBlueGreenRedReserved8BitPerColor);
    memcpy(Gop.Memory + 1370 * 4, &Magenta, 4);
    memcpy(Gop.Memory + 1372 * 4, &Magenta, 4);
    Before = Snapshot(&Gop);
    Run("magenta in the row padding", &Gop, 1376, 4, Before);
    Release(&Gop, Before);

    Setup(&Gop, 1366, 768, 1366, 1366, 1366 * 767 * 4, PixelBlueGreenRedReserved8BitPerColor);
    Before = Snapshot(&Gop);
    Run("frame buffer smaller than the mode", &Gop, 1366, 0, Before);
    Release(&Gop, Before);

    Setup(&Gop, 1366, 1, 1366, 1376, 8192, PixelBlueGreenRedReserved8BitPerColor);
    Before = Snapshot(&Gop);
    Run("single row", &Gop, 1366, 0, Before);
    Release(&Gop, Before);

    printf("%s: %d failure(s)\n", Failures ? "FAIL" : "PASS", Failures);
    return Failures != 0;
}
'''

with tempfile.TemporaryDirectory() as work:
    program = pathlib.Path(work) / 'gop-scanline'
    unit = pathlib.Path(work) / 'gop-scanline.c'
    unit.write_text(fixture + defines + transform + stub + measure + handoff + harness)
    subprocess.run([args.cc, '-std=c11', '-Wall', '-Werror', '-g', '-O1', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', str(unit), '-o', str(program)], check=True)
    sys.exit(subprocess.run([str(program)]).returncode)
