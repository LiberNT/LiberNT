/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Channel between the Linux virtio-gpu DRM device and the Windows GPU renderer
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define LX_GPU_FILE_NAME            L"\\gpu"

#define IOCTL_LX_GPU_ATTACH         CTL_CODE(FILE_DEVICE_UNKNOWN, 0x820, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_LX_GPU_RECEIVE        CTL_CODE(FILE_DEVICE_UNKNOWN, 0x821, METHOD_OUT_DIRECT, FILE_ANY_ACCESS)
#define IOCTL_LX_GPU_SEND           CTL_CODE(FILE_DEVICE_UNKNOWN, 0x822, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define LX_GPU_MAX_MESSAGE          (16 * 1024 * 1024)

#define LX_GPU_GET_CAPS             1
#define LX_GPU_CTX_CREATE           2
#define LX_GPU_CTX_DESTROY          3
#define LX_GPU_RES_CREATE           4
#define LX_GPU_RES_UNREF            5
#define LX_GPU_SUBMIT               6
#define LX_GPU_TRANSFER             7
#define LX_GPU_BLOB_CREATE          8

#define LX_GPU_REPLY                100
#define LX_GPU_DONE                 101
#define LX_GPU_RING_DONE            102

#define LX_GPU_CAPSET_VIRGL2        2
#define LX_GPU_CAPSET_VENUS         4

typedef struct _LX_GPU_HEADER
{
    ULONG Type;
    ULONG Size;
    ULONG Serial;
    LONG Status;
} LX_GPU_HEADER, *PLX_GPU_HEADER;

typedef struct _LX_GPU_CAPS_MSG
{
    LX_GPU_HEADER Header;
    ULONG Capset;
    ULONG Version;
} LX_GPU_CAPS_MSG, *PLX_GPU_CAPS_MSG;

typedef struct _LX_GPU_CTX_MSG
{
    LX_GPU_HEADER Header;
    ULONG CtxId;
    ULONG Capset;
} LX_GPU_CTX_MSG, *PLX_GPU_CTX_MSG;

typedef struct _LX_GPU_RES_MSG
{
    LX_GPU_HEADER Header;
    ULONG ResId;
    ULONG Target;
    ULONG Format;
    ULONG Bind;
    ULONG Width;
    ULONG Height;
    ULONG Depth;
    ULONG ArraySize;
    ULONG LastLevel;
    ULONG NrSamples;
    ULONG Flags;
    ULONG CtxId;
    ULONG64 Size;
    ULONG64 Section;
} LX_GPU_RES_MSG, *PLX_GPU_RES_MSG;

typedef struct _LX_GPU_SUBMIT_MSG
{
    LX_GPU_HEADER Header;
    ULONG CtxId;
    ULONG Length;
    ULONG ResCount;
    ULONG RingIdx;
} LX_GPU_SUBMIT_MSG, *PLX_GPU_SUBMIT_MSG;

typedef struct _LX_GPU_TRANSFER_MSG
{
    LX_GPU_HEADER Header;
    ULONG CtxId;
    ULONG ResId;
    ULONG ToHost;
    ULONG Level;
    ULONG Stride;
    ULONG LayerStride;
    ULONG Box[6];
    ULONG64 Offset;
} LX_GPU_TRANSFER_MSG, *PLX_GPU_TRANSFER_MSG;

typedef struct _LX_GPU_BLOB_MSG
{
    LX_GPU_HEADER Header;
    ULONG CtxId;
    ULONG ResId;
    ULONG BlobMem;
    ULONG BlobFlags;
    ULONG64 BlobId;
    ULONG64 Size;
} LX_GPU_BLOB_MSG, *PLX_GPU_BLOB_MSG;

typedef struct _LX_GPU_BLOB_REPLY
{
    ULONG64 Section;
    ULONG MapInfo;
    ULONG Reserved;
} LX_GPU_BLOB_REPLY, *PLX_GPU_BLOB_REPLY;

typedef struct _LX_GPU_RING_DONE_MSG
{
    LX_GPU_HEADER Header;
    ULONG CtxId;
    ULONG RingIdx;
    ULONG64 FenceId;
} LX_GPU_RING_DONE_MSG, *PLX_GPU_RING_DONE_MSG;

typedef struct _LX_GPU_DONE_MSG
{
    LX_GPU_HEADER Header;
    ULONG Completed;
    ULONG Reserved;
} LX_GPU_DONE_MSG, *PLX_GPU_DONE_MSG;
