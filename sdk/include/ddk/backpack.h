#ifndef _BACKPACK_
#define _BACKPACK_

typedef struct _THROTTLING_STATE
{
    LARGE_INTEGER NextTime;
    volatile ULONG CurrentIncrement;
    ULONG MaximumDelay;
    LARGE_INTEGER Increment;
    volatile ULONG NumberOfQueries;
} THROTTLING_STATE, *PTHROTTLING_STATE;

#define RxInitializeThrottlingState(BP, Inc, MaxDelay) \
{                                                      \
    if ((Inc) > 0)                                     \
    {                                                  \
        (BP)->Increment.QuadPart = (Inc) * 10000;      \
        (BP)->MaximumDelay = (MaxDelay) / (Inc);       \
        (BP)->CurrentIncrement = 0;                    \
    }                                                  \
}


#ifdef __cplusplus
extern "C" {
#endif

/* TODO: LiberNT does not implement the functions below yet */
void NTAPI RxInitiateOrContinueThrottling(PTHROTTLING_STATE pBP);
BOOLEAN NTAPI RxShouldRequestBeThrottled(PTHROTTLING_STATE pBP);

#ifdef __cplusplus
}
#endif

#define RxInitializeBackoffPackage()
#define RxTerminateThrottling(pBP) ((pBP) -> CurrentIncrement = 0)
#define RxUninitializeBackoffPackage()
#define RxUninitializeBackPack(pBP) ()

#endif
