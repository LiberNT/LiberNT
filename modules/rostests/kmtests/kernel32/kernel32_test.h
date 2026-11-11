/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPLv2.1+ - See COPYING.LIB in the top level directory
 * PURPOSE:         Kernel32 filesystem test declarations
 * PROGRAMMER:      Thomas Faber <thomas.faber@reactos.org>
 */

#ifndef _KMTEST_FINDFILE_H_
#define _KMTEST_FINDFILE_H_

#define IOCTL_EXPECT_EXPRESSION         1
#define IOCTL_RETURN_QUERY_ATTRIBUTES   2
#define IOCTL_EXPECT_SET_ATTRIBUTES     3
#define IOCTL_SECURE_MEMORY             4
#define IOCTL_UNSECURE_MEMORY           5

typedef struct _KMT_SECURE_MEMORY
{
    PVOID Address;
    SIZE_T Size;
    ULONG ProbeMode;
    HANDLE SecureHandle;
} KMT_SECURE_MEMORY, *PKMT_SECURE_MEMORY;

#endif /* !defined _KMTEST_FINDFILE_H_ */
