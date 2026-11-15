/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     i386 prefetch routine patched in when the processor has SSE
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <asm.inc>
#include <ks386.inc>

EXTERN _KePrefetchNTAGranularity:DWORD

.code

PUBLIC @RtlPrefetchMemoryNonTemporal@8
@RtlPrefetchMemoryNonTemporal@8:

    ret

    mov eax, [_KePrefetchNTAGranularity]

FetchLine:
    prefetchnta byte ptr [ecx]

    add ecx, eax
    sub edx, eax

    ja FetchLine
    ret

END
