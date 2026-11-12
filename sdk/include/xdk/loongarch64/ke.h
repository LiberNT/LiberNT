
$if (_WDMDDK_)

#define PASSIVE_LEVEL  0
#define LOW_LEVEL      0
#define APC_LEVEL      1
#define DISPATCH_LEVEL 2
#define CLOCK_LEVEL    13
#define IPI_LEVEL      14
#define POWER_LEVEL    14
#define PROFILE_LEVEL  15
#define HIGH_LEVEL     15

#define PAGE_SIZE  0x1000
#define PAGE_SHIFT 12L

#define KI_USER_SHARED_DATA     0xFFFFF78000000000ULL
#define SharedUserData          ((KUSER_SHARED_DATA * const)KI_USER_SHARED_DATA)
/* TODO: the LoongArch64 loader has to map the page here and NVS has to reserve it. */

#ifndef __ASSEMBLER__
typedef struct _KFLOATING_SAVE
{
    ULONG Dummy;
} KFLOATING_SAVE, *PKFLOATING_SAVE;
#endif

#define EXCEPTION_READ_FAULT    0
#define EXCEPTION_WRITE_FAULT   1
#define EXCEPTION_EXECUTE_FAULT 8

#ifndef _LOONGARCH64_YIELD_PROCESSOR_DEFINED
#define _LOONGARCH64_YIELD_PROCESSOR_DEFINED
FORCEINLINE VOID YieldProcessor(VOID)
{
    __asm__ __volatile__("dbar 0" ::: "memory");
}
#endif
#define PAUSE_PROCESSOR YieldProcessor()

FORCEINLINE
ULONG64
ReadTimeStampCounter(
    VOID)
{
    ULONG64 Value;

    __asm__ __volatile__("rdtime.d %0, %1" : "=r"(Value) : "r"(0ULL) : "memory");
    return Value;
}

#define DbgRaiseAssertionFailure() __debugbreak()
#define KeMemoryBarrier() MemoryBarrier()

NTKERNELAPI PKTHREAD NTAPI KeGetCurrentThread(VOID);

#if !defined(_NTOSKRNL_) && !defined(_NTSYSTEM_)
FORCEINLINE
ULONG
KeGetCurrentProcessorNumber(VOID)
{
    extern NTKERNELAPI ULONG NTAPI KeGetCurrentProcessorNumberEx(
        _Out_opt_ PPROCESSOR_NUMBER ProcNumber);
    return KeGetCurrentProcessorNumberEx(NULL);
}
#endif

FORCEINLINE ULONG KeGetCurrentProcessorIndex(VOID)
{
    extern NTKERNELAPI ULONG NTAPI KeGetCurrentProcessorNumberEx(
        _Out_opt_ PPROCESSOR_NUMBER ProcNumber);
    return KeGetCurrentProcessorNumberEx(NULL);
}

NTKERNELAPI KIRQL NTAPI KeGetCurrentIrql(VOID);
NTKERNELAPI KIRQL FASTCALL KfRaiseIrql(KIRQL NewIrql);
NTKERNELAPI VOID FASTCALL KfLowerIrql(KIRQL NewIrql);
#define KeRaiseIrql(NewIrql, OldIrql) (*(OldIrql) = KfRaiseIrql(NewIrql))
#define KeLowerIrql(NewIrql) KfLowerIrql(NewIrql)
#if defined(_NTOSKRNL_) || defined(_NTHAL_) || defined(_NTSYSTEM_)
NTKERNELAPI KIRQL NTAPI KeRaiseIrqlToDpcLevel(VOID);
NTKERNELAPI KIRQL NTAPI KeRaiseIrqlToSynchLevel(VOID);
#else
FORCEINLINE KIRQL KeRaiseIrqlToDpcLevel(VOID) { return KfRaiseIrql(DISPATCH_LEVEL); }
FORCEINLINE KIRQL KeRaiseIrqlToSynchLevel(VOID) { return KfRaiseIrql(12); }
#endif

_Always_(_Post_satisfies_(return<=0))
_Must_inspect_result_
_IRQL_requires_max_(DISPATCH_LEVEL)
_Kernel_float_saved_
_At_(*FloatSave, _Kernel_requires_resource_not_held_(FloatState) _Kernel_acquires_resource_(FloatState))
FORCEINLINE
NTSTATUS
KeSaveFloatingPointState(
    _Out_ PKFLOATING_SAVE FloatSave)
{
    UNREFERENCED_PARAMETER(FloatSave);
    return STATUS_SUCCESS;
}

_Success_(1)
_Kernel_float_restored_
_At_(*FloatSave, _Kernel_requires_resource_held_(FloatState) _Kernel_releases_resource_(FloatState))
FORCEINLINE
NTSTATUS
KeRestoreFloatingPointState(
    _In_ PKFLOATING_SAVE FloatSave)
{
    UNREFERENCED_PARAMETER(FloatSave);
    return STATUS_SUCCESS;
}

$endif (_WDMDDK_)
