/*
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
