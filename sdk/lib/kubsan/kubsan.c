/*
 * PROJECT:     LiberNT Kernel Undefined Behavior Sanitizer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Report the undefined behavior -fsanitize=undefined catches in kernel-mode modules
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <ntstrsafe.h>

#define KUBSAN_REPORTED 0x80000000
#define KUBSAN_PENDING_MAX 32
#define KUBSAN_TYPE_INTEGER 0

typedef struct _KUBSAN_LOCATION
{
    PCSTR FileName;
    ULONG Line;
    volatile LONG Column;
} KUBSAN_LOCATION, *PKUBSAN_LOCATION;

typedef struct _KUBSAN_TYPE
{
    USHORT Kind;
    USHORT Info;
    CHAR Name[1];
} KUBSAN_TYPE, *PKUBSAN_TYPE;

typedef struct _KUBSAN_OVERFLOW_DATA
{
    KUBSAN_LOCATION Location;
    PKUBSAN_TYPE Type;
} KUBSAN_OVERFLOW_DATA, *PKUBSAN_OVERFLOW_DATA;

typedef struct _KUBSAN_SHIFT_DATA
{
    KUBSAN_LOCATION Location;
    PKUBSAN_TYPE LeftType;
    PKUBSAN_TYPE RightType;
} KUBSAN_SHIFT_DATA, *PKUBSAN_SHIFT_DATA;

typedef struct _KUBSAN_BOUNDS_DATA
{
    KUBSAN_LOCATION Location;
    PKUBSAN_TYPE ArrayType;
    PKUBSAN_TYPE IndexType;
} KUBSAN_BOUNDS_DATA, *PKUBSAN_BOUNDS_DATA;

typedef struct _KUBSAN_VALUE_DATA
{
    KUBSAN_LOCATION Location;
    PKUBSAN_TYPE Type;
} KUBSAN_VALUE_DATA, *PKUBSAN_VALUE_DATA;

typedef struct _KUBSAN_BUILTIN_DATA
{
    KUBSAN_LOCATION Location;
    UCHAR Kind;
} KUBSAN_BUILTIN_DATA, *PKUBSAN_BUILTIN_DATA;

typedef struct _KUBSAN_CAST_DATA
{
    KUBSAN_LOCATION Location;
    PKUBSAN_TYPE FromType;
    PKUBSAN_TYPE ToType;
} KUBSAN_CAST_DATA, *PKUBSAN_CAST_DATA;

typedef struct _KUBSAN_NONNULL_ARG_DATA
{
    KUBSAN_LOCATION Location;
    KUBSAN_LOCATION AttributeLocation;
    INT ArgumentIndex;
} KUBSAN_NONNULL_ARG_DATA, *PKUBSAN_NONNULL_ARG_DATA;

typedef struct _KUBSAN_NONNULL_RETURN_DATA
{
    KUBSAN_LOCATION AttributeLocation;
} KUBSAN_NONNULL_RETURN_DATA, *PKUBSAN_NONNULL_RETURN_DATA;

typedef struct _KUBSAN_REPORT
{
    volatile LONG Ready;
    PKUBSAN_LOCATION Location;
    PVOID Caller;
    CHAR Text[160];
} KUBSAN_REPORT, *PKUBSAN_REPORT;

#ifdef _NTSYSTEM_
extern BOOLEAN KdpDebuggerStructuresInitialized;
#define KubsanCanPrint() (KdpDebuggerStructuresInitialized != FALSE)
#else
#define KubsanCanPrint() TRUE
#endif

static volatile LONG KubsanBusy;
static volatile LONG KubsanPendingNext;
static volatile LONG KubsanPendingFlushed;
static volatile LONG KubsanDropped;
static KUBSAN_REPORT KubsanPending[KUBSAN_PENDING_MAX];

static
BOOLEAN
KubsanClaim(
    _Inout_ PKUBSAN_LOCATION Location)
{
    return (InterlockedOr(&Location->Column, (LONG)KUBSAN_REPORTED) & (LONG)KUBSAN_REPORTED) == 0;
}

static
VOID
KubsanPrint(
    _In_ PKUBSAN_LOCATION Location,
    _In_ PVOID Caller,
    _In_z_ PCSTR Text)
{
    DbgPrintEx(DPFLTR_IHVDRIVER_ID,
               DPFLTR_ERROR_LEVEL,
               "UBSAN: %s at %s:%lu:%lu, caller %p\n",
               Text,
               Location->FileName ? Location->FileName : "<unknown>",
               Location->Line,
               (ULONG)Location->Column & ~KUBSAN_REPORTED,
               Caller);
}

static
VOID
KubsanFlush(VOID)
{
    LONG Index;
    LONG Dropped;

    for (Index = KubsanPendingFlushed;
         Index < KubsanPendingNext && Index < KUBSAN_PENDING_MAX && KubsanPending[Index].Ready;
         Index++)
    {
        KubsanPrint(KubsanPending[Index].Location, KubsanPending[Index].Caller, KubsanPending[Index].Text);
    }
    KubsanPendingFlushed = Index;

    Dropped = InterlockedExchange(&KubsanDropped, 0);
    if (Dropped != 0)
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "UBSAN: %ld report(s) dropped at raised IRQL\n", Dropped);
}

static
KIRQL
KubsanCurrentIrql(VOID)
{
#if defined(_M_IX86)
    return __readfsbyte(FIELD_OFFSET(KPCR, Irql));
#else
    return KeGetCurrentIrql();
#endif
}

static
VOID
KubsanReport(
    _Inout_ PKUBSAN_LOCATION Location,
    _In_ PVOID Caller,
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...)
{
    CHAR Text[RTL_FIELD_SIZE(KUBSAN_REPORT, Text)];
    va_list Arguments;
    LONG Slot;

    if (!KubsanClaim(Location))
        return;

    va_start(Arguments, Format);
    if (!NT_SUCCESS(RtlStringCbVPrintfA(Text, sizeof(Text), Format, Arguments)))
        Text[sizeof(Text) - 1] = ANSI_NULL;
    va_end(Arguments);

    if (KubsanCanPrint() && KubsanCurrentIrql() <= DISPATCH_LEVEL && InterlockedCompareExchange(&KubsanBusy, 1, 0) == 0)
    {
        KubsanFlush();
        KubsanPrint(Location, Caller, Text);
        InterlockedExchange(&KubsanBusy, 0);
        return;
    }

    Slot = InterlockedIncrement(&KubsanPendingNext) - 1;
    if (Slot >= KUBSAN_PENDING_MAX)
    {
        InterlockedIncrement(&KubsanDropped);
        return;
    }

    KubsanPending[Slot].Location = Location;
    KubsanPending[Slot].Caller = Caller;
    RtlCopyMemory(KubsanPending[Slot].Text, Text, sizeof(Text));
    InterlockedExchange(&KubsanPending[Slot].Ready, 1);
}

static
LONG64
KubsanSigned(
    _In_ PKUBSAN_TYPE Type,
    _In_ ULONG_PTR Handle)
{
    ULONG Width = 1u << (Type->Info >> 1);
    ULONG64 Value;
    ULONG Shift;

    if (Width > sizeof(ULONG_PTR) * 8)
        return *(PLONG64)Handle;

    Value = Handle;
    Shift = 64 - Width;
    return Shift ? ((LONG64)(Value << Shift)) >> Shift : (LONG64)Value;
}

static
ULONG64
KubsanUnsigned(
    _In_ PKUBSAN_TYPE Type,
    _In_ ULONG_PTR Handle)
{
    ULONG Width = 1u << (Type->Info >> 1);

    if (Width > sizeof(ULONG_PTR) * 8)
        return *(PULONG64)Handle;

    return Width < 64 ? (ULONG64)Handle & ((1ull << Width) - 1) : (ULONG64)Handle;
}

static
VOID
KubsanValue(
    _Out_writes_z_(Size) PCHAR Buffer,
    _In_ SIZE_T Size,
    _In_ PKUBSAN_TYPE Type,
    _In_ ULONG_PTR Handle)
{
    NTSTATUS Status;

    if (Type->Kind != KUBSAN_TYPE_INTEGER)
        Status = RtlStringCbPrintfA(Buffer, Size, "<%s>", Type->Name);
    else if (Type->Info & 1)
        Status = RtlStringCbPrintfA(Buffer, Size, "%I64d", KubsanSigned(Type, Handle));
    else
        Status = RtlStringCbPrintfA(Buffer, Size, "%I64u", KubsanUnsigned(Type, Handle));

    if (!NT_SUCCESS(Status))
        Buffer[0] = ANSI_NULL;
}

static
VOID
KubsanArithmetic(
    _Inout_ PKUBSAN_OVERFLOW_DATA Data,
    _In_ ULONG_PTR Left,
    _In_ ULONG_PTR Right,
    _In_ CHAR Operator,
    _In_ PVOID Caller)
{
    CHAR LeftText[24], RightText[24];

    KubsanValue(LeftText, sizeof(LeftText), Data->Type, Left);
    KubsanValue(RightText, sizeof(RightText), Data->Type, Right);
    KubsanReport(&Data->Location, Caller, "%s integer overflow: %s %c %s in type %s",
                 (Data->Type->Info & 1) ? "signed" : "unsigned", LeftText, Operator, RightText, Data->Type->Name);
}

VOID
__cdecl
__ubsan_handle_add_overflow(
    _Inout_ PKUBSAN_OVERFLOW_DATA Data,
    _In_ ULONG_PTR Left,
    _In_ ULONG_PTR Right)
{
    KubsanArithmetic(Data, Left, Right, '+', __builtin_return_address(0));
}

VOID
__cdecl
__ubsan_handle_sub_overflow(
    _Inout_ PKUBSAN_OVERFLOW_DATA Data,
    _In_ ULONG_PTR Left,
    _In_ ULONG_PTR Right)
{
    KubsanArithmetic(Data, Left, Right, '-', __builtin_return_address(0));
}

VOID
__cdecl
__ubsan_handle_mul_overflow(
    _Inout_ PKUBSAN_OVERFLOW_DATA Data,
    _In_ ULONG_PTR Left,
    _In_ ULONG_PTR Right)
{
    KubsanArithmetic(Data, Left, Right, '*', __builtin_return_address(0));
}

VOID
__cdecl
__ubsan_handle_negate_overflow(
    _Inout_ PKUBSAN_OVERFLOW_DATA Data,
    _In_ ULONG_PTR Value)
{
    CHAR Text[24];

    KubsanValue(Text, sizeof(Text), Data->Type, Value);
    KubsanReport(&Data->Location, __builtin_return_address(0), "negation of %s cannot be represented in type %s",
                 Text, Data->Type->Name);
}

VOID
__cdecl
__ubsan_handle_divrem_overflow(
    _Inout_ PKUBSAN_OVERFLOW_DATA Data,
    _In_ ULONG_PTR Left,
    _In_ ULONG_PTR Right)
{
    CHAR LeftText[24], RightText[24];

    KubsanValue(LeftText, sizeof(LeftText), Data->Type, Left);
    KubsanValue(RightText, sizeof(RightText), Data->Type, Right);
    KubsanReport(&Data->Location, __builtin_return_address(0), "division overflow or by zero: %s / %s in type %s",
                 LeftText, RightText, Data->Type->Name);
}

VOID
__cdecl
__ubsan_handle_shift_out_of_bounds(
    _Inout_ PKUBSAN_SHIFT_DATA Data,
    _In_ ULONG_PTR Left,
    _In_ ULONG_PTR Right)
{
    CHAR LeftText[24], RightText[24];

    KubsanValue(LeftText, sizeof(LeftText), Data->LeftType, Left);
    KubsanValue(RightText, sizeof(RightText), Data->RightType, Right);
    KubsanReport(&Data->Location, __builtin_return_address(0), "shift out of bounds: %s by %s (left %s, right %s)",
                 LeftText, RightText, Data->LeftType->Name, Data->RightType->Name);
}

VOID
__cdecl
__ubsan_handle_out_of_bounds(
    _Inout_ PKUBSAN_BOUNDS_DATA Data,
    _In_ ULONG_PTR Index)
{
    CHAR Text[24];

    KubsanValue(Text, sizeof(Text), Data->IndexType, Index);
    KubsanReport(&Data->Location, __builtin_return_address(0), "index %s out of bounds for type %s",
                 Text, Data->ArrayType->Name);
}

VOID
__cdecl
__ubsan_handle_load_invalid_value(
    _Inout_ PKUBSAN_VALUE_DATA Data,
    _In_ ULONG_PTR Value)
{
    CHAR Text[24];

    KubsanValue(Text, sizeof(Text), Data->Type, Value);
    KubsanReport(&Data->Location, __builtin_return_address(0), "load of value %s, which is not valid for type %s",
                 Text, Data->Type->Name);
}

VOID
__cdecl
__ubsan_handle_invalid_builtin(
    _Inout_ PKUBSAN_BUILTIN_DATA Data)
{
    KubsanReport(&Data->Location, __builtin_return_address(0), "passing zero to %s, which is not a valid argument",
                 Data->Kind == 0 ? "ctz()" : Data->Kind == 1 ? "clz()" : "a builtin");
}

VOID
__cdecl
__ubsan_handle_vla_bound_not_positive(
    _Inout_ PKUBSAN_VALUE_DATA Data,
    _In_ ULONG_PTR Bound)
{
    CHAR Text[24];

    KubsanValue(Text, sizeof(Text), Data->Type, Bound);
    KubsanReport(&Data->Location, __builtin_return_address(0), "variable length array bound evaluates to non-positive value %s", Text);
}

VOID
__cdecl
__ubsan_handle_float_cast_overflow(
    _Inout_ PKUBSAN_CAST_DATA Data,
    _In_ ULONG_PTR From)
{
    UNREFERENCED_PARAMETER(From);
    KubsanReport(&Data->Location, __builtin_return_address(0), "%s value is outside the range of representable values of type %s",
                 Data->FromType->Name, Data->ToType->Name);
}

VOID
__cdecl
__ubsan_handle_nonnull_arg(
    _Inout_ PKUBSAN_NONNULL_ARG_DATA Data)
{
    KubsanReport(&Data->Location, __builtin_return_address(0), "null pointer passed as argument %d, which is declared to never be null",
                 Data->ArgumentIndex);
}

VOID
__cdecl
__ubsan_handle_nonnull_return_v1(
    _In_ PKUBSAN_NONNULL_RETURN_DATA Data,
    _Inout_ PKUBSAN_LOCATION Location)
{
    UNREFERENCED_PARAMETER(Data);
    KubsanReport(Location, __builtin_return_address(0), "null pointer returned from function declared to never return null");
}
