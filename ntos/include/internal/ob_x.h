/*

#include "ex.h"

#define OBP_NAME_LOOKASIDE_MAX_SIZE 248

FORCEINLINE
ULONG
{
    if (PreviousMode == KernelMode)
        return Attributes & OBJ_VALID_KERNEL_ATTRIBUTES;
}

FORCEINLINE
POBJECT_TYPE
{

}

FORCEINLINE
VOID
ObpGetQuotaCharges(
    _In_ POBJECT_HEADER ObjectHeader,
    _In_ POBJECT_TYPE ObjectType,
    _Out_ PULONG PagedPoolCharge,
    _Out_ PULONG NonPagedPoolCharge)
{
    POBJECT_HEADER_QUOTA_INFO QuotaInfo = OBJECT_HEADER_TO_QUOTA_INFO(ObjectHeader);

}

FORCEINLINE
VOID
{
    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);

    KeEnterCriticalRegion();
}

FORCEINLINE
VOID
{

}

FORCEINLINE
VOID
{
}

FORCEINLINE
{
}

FORCEINLINE
VOID
{
}

FORCEINLINE
VOID
{
}

FORCEINLINE
VOID
{
}

FORCEINLINE
VOID
{
}

FORCEINLINE
VOID
{
}

FORCEINLINE
VOID
{
}

FORCEINLINE
{



    }
}

FORCEINLINE
VOID
{
    {
    }

}

FORCEINLINE
VOID
{

}

FORCEINLINE
VOID
{

}

FORCEINLINE
VOID
{
    {
    }
}

FORCEINLINE
PVOID
{
    PKPRCB Prcb = KeGetCurrentPrcb();


    {

    }

}

FORCEINLINE
VOID
{
    PKPRCB Prcb = KeGetCurrentPrcb();


    {
        {
        }
    }
}

FORCEINLINE
VOID
{
    ObpReleaseObjectCreateInformation(ObjectCreateInfo);
    ObpFreeCapturedAttributes(ObjectCreateInfo, LookasideCreateInfoList);
}

FORCEINLINE
VOID
{
    *CalloutIrql = KeGetCurrentIrql();
}

FORCEINLINE
VOID
{
    {
        DbgBreakPoint();
    }
#else
    UNREFERENCED_PARAMETER(CalloutIrql);
#endif
