#include <rtltests.h>
#include <pseh/pseh2.h>
#include <compat_undoc.h>
#include <compatguid_undoc.h>

#define NDEBUG
#include <debug.h>

BOOLEAN LdrpShutdownInProgress;
HANDLE LdrpShutdownThreadId;
RTL_CRITICAL_SECTION LdrpLoaderLock;
RTL_SRWLOCK LdrpModuleDatatableLock;

VOID
NTAPI
LdrpInitializeProcessCompat(PVOID pProcessActctx, PVOID* pOldShimData)
{
}

VOID NTAPI
AVrfInternalHeapFreeNotification(PVOID AllocationBase, SIZE_T AllocationSize)
{
    /* Stub for linking against rtl */
}

NTSTATUS
NTAPI
LdrpGetAlternateResourceModule(PVOID BaseAddress, ULONG_PTR Type, USHORT LanguageId, PVOID *ResourceBase)
{
    return STATUS_RESOURCE_DATA_NOT_FOUND;
}

NTSTATUS
NTAPI
LdrpGetNeutralResourceModule(PVOID BaseAddress, PVOID *ResourceBase)
{
    return STATUS_RESOURCE_DATA_NOT_FOUND;
}

PVOID
NTAPI
LdrpGetResourceModuleForEntry(PVOID BaseAddress, PVOID Entry)
{
    return BaseAddress;
}

LANGID
NTAPI
RtlpGetThreadUILanguage(VOID)
{
    return 0;
}
