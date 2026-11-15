/*
 * COPYRIGHT:   Copyright 2020-2022 George Bișoc <george.bisoc@reactos.org>
 */

#pragma once

#define ICIF_QUERY               0x1
#define ICIF_SET                 0x2
#define ICIF_QUERY_SIZE_VARIABLE 0x4
#define ICIF_SET_SIZE_VARIABLE   0x8

#define ICIF_PROBE_READ_WRITE          0x0
#define ICIF_PROBE_READ                0x1
#define ICIF_FORCE_RETURN_LENGTH_PROBE 0x2

typedef struct _INFORMATION_CLASS_INFO
{
    USHORT RequiredSizeQUERY;
    UCHAR AlignmentQUERY;
    USHORT RequiredSizeSET;
    UCHAR AlignmentSET;
    USHORT Flags;
} INFORMATION_CLASS_INFO, *PINFORMATION_CLASS_INFO;

