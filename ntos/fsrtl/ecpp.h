/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What an extra create parameter and its list are made of
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define FSRTL_ECP_TAG 'EpcE'

#define FSRTL_ECP_LIST_SIGNATURE 0x4C504345
#define FSRTL_ECP_SIGNATURE 0x48504345

#define FSRTL_ECP_LIST_FLAG_QUOTA 0x00000001
#define FSRTL_ECP_LIST_FLAG_STATIC 0x00000002

#define FSRTL_ECP_FLAG_ACKNOWLEDGED 0x00000001
#define FSRTL_ECP_FLAG_USER_MODE 0x00000002
#define FSRTL_ECP_FLAG_FROM_LOOKASIDE 0x00000004
#define FSRTL_ECP_FLAG_QUOTA 0x00000008
#define FSRTL_ECP_FLAG_NONPAGED 0x00000010
#define FSRTL_ECP_FLAG_CALLER_OWNED 0x00000020
#define FSRTL_ECP_FLAG_IN_LIST 0x00000040
#define FSRTL_ECP_FLAG_STATIC 0x00000080

/*
 * The list a create carries. Windows 11 keeps a signature, flags and the head
 * of the parameter list in it, in that order, and nothing else; its public
 * symbols give the layout.
 */
struct _ECP_LIST
{
    ULONG Signature;
    ULONG Flags;
    LIST_ENTRY EcpList;
};

/* One parameter. The context the caller sees begins right past this header. */
struct _ECP_HEADER
{
    ULONG Signature;
    ULONG Flags;
    ULONG CallerFlags;
    ULONG TotalSize;
    LIST_ENTRY Links;
    GUID EcpType;
    ULONG PoolTag;
    ULONG Reserved;
    PFSRTL_EXTRA_CREATE_PARAMETER_CLEANUP_CALLBACK CleanupCallback;
    PVOID ListAllocatedFrom;
};

#define FSRTL_ECP_HEADER_SIZE                                                 \
    ((ULONG)ALIGN_UP_BY(sizeof(ECP_HEADER), MEMORY_ALLOCATION_ALIGNMENT))

PECP_HEADER
NTAPI
FsRtlpEcpHeaderOf(
    _In_opt_ PVOID EcpContext);

/* EOF */
