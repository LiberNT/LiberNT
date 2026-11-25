/*
 * PROJECT:     LiberNT WIM library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Files of a WIM image as WOF pointer files or extracted files: tree walk, pointer reparse data, overlay registration and extraction
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef LIBWIM_WIMBOOT_H
#define LIBWIM_WIMBOOT_H

#include "wimcore.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIMBOOT_OK              0
#define WIMBOOT_IO_ERROR        1
#define WIMBOOT_BAD_WIM         2
#define WIMBOOT_NO_MEMORY       3
#define WIMBOOT_STOPPED         4

#define WIMBOOT_POINTER_SIZE    96

typedef struct _WIMBOOT_IMAGE
{
    uint8_t Guid[16];
    uint8_t BlobTableHash[20];
    uint32_t ImageIndex;
} WIMBOOT_IMAGE;

typedef struct _WIMBOOT_ENTRY
{
    const char* Path;
    uint32_t Attributes;
    int IsDirectory;
    uint64_t Size;
    uint64_t StoredSize;
    uint64_t Offset;
    uint64_t CreationTime;
    uint64_t LastAccessTime;
    uint64_t LastWriteTime;
    uint8_t Hash[20];
} WIMBOOT_ENTRY;

typedef int (*WIMBOOT_VISIT)(void* Context, const WIMBOOT_ENTRY* Entry);

typedef int (*WIMBOOT_WRITE)(void* Context, const WIMBOOT_ENTRY* Entry, const uint8_t* Data);

int WimBootOpen(WimCoreCtx* Wim, WIMBOOT_IMAGE* Image);

int WimBootOpenImage(WimCoreCtx* Wim, uint32_t Index, WIMBOOT_IMAGE* Image);

int WimBootExtract(WimCoreCtx* Wim, const WIMBOOT_IMAGE* Image, WIMBOOT_WRITE Write, void* Context);

int WimBootWalk(WimCoreCtx* Wim, const WIMBOOT_IMAGE* Image, WIMBOOT_VISIT Visit, void* Context);

void WimBootPointer(uint8_t Buffer[WIMBOOT_POINTER_SIZE], const WIMBOOT_IMAGE* Image, const WIMBOOT_ENTRY* Entry);

uint32_t WimBootOverlay(uint8_t* Buffer,
                        uint32_t Capacity,
                        const WIMBOOT_IMAGE* Image,
                        const uint16_t* Path,
                        uint64_t PartitionOffset,
                        uint32_t DiskSignature);

#ifdef __cplusplus
}
#endif

#endif
