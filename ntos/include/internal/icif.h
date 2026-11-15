/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Description of an information class: sizes, alignments, directions
 * COPYRIGHT:   Copyright 2020-2022 George Bișoc <george.bisoc@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define ICIF_QUERY               0x1
#define ICIF_SET                 0x2
#define ICIF_QUERY_SIZE_VARIABLE 0x4
#define ICIF_SET_SIZE_VARIABLE   0x8
#define ICIF_SIZE_VARIABLE       (ICIF_QUERY_SIZE_VARIABLE | ICIF_SET_SIZE_VARIABLE)

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

#define IQS(QueryType, QueryAlignment, SetType, SetAlignment, Flags) \
    { sizeof(QueryType), sizeof(QueryAlignment), sizeof(SetType), sizeof(SetAlignment), (Flags) }
#define IQS_SAME(Type, Alignment, Flags) IQS(Type, Alignment, Type, Alignment, Flags)
#define IQS_NO_TYPE_LENGTH(Alignment, Flags) { 0, sizeof(Alignment), 0, sizeof(Alignment), (Flags) }
#define IQS_NONE IQS_NO_TYPE_LENGTH(CHAR, 0)
