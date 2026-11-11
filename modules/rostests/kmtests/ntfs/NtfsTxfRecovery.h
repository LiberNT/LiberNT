/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Transactional NTFS commit recovery test declarations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _KMTEST_NTFS_TXF_RECOVERY_H_
#define _KMTEST_NTFS_TXF_RECOVERY_H_

#define IOCTL_NTFSTXF_REGISTER      1
#define IOCTL_NTFSTXF_ARM           2
#define IOCTL_NTFSTXF_QUERY         3
#define IOCTL_NTFSTXF_DISARM        4
#define IOCTL_NTFSTXF_UNREGISTER    5

#define NTFSTXF_TRAP_NONE           0
#define NTFSTXF_TRAP_NAMESPACE      1
#define NTFSTXF_TRAP_WRITE          2

typedef struct _NTFSTXF_TRAP
{
    ULONG Kind;
    ULONG FailAt;
} NTFSTXF_TRAP, *PNTFSTXF_TRAP;

#endif
