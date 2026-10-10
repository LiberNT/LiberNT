/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Derive the argument widths of the native system services from their prototypes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define WIN32_NO_STATUS
#define NTOS_MODE_USER
#include <windef.h>
#include <winbase.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <ndk/ntndk.h>
#include <type_traits>
#include "signature.h"

namespace
{

struct EMU_UNTYPED_SERVICE
{
};

#define EMU_UNTYPED(Name) constexpr EMU_UNTYPED_SERVICE Nt##Name{};
EMU_UNTYPED(AccessCheckByTypeAndAuditAlarm)
EMU_UNTYPED(AccessCheckByTypeResultListAndAuditAlarm)
EMU_UNTYPED(AccessCheckByTypeResultListAndAuditAlarmByHandle)
EMU_UNTYPED(CommitComplete)
EMU_UNTYPED(CommitEnlistment)
EMU_UNTYPED(CommitTransaction)
EMU_UNTYPED(CreateEnlistment)
EMU_UNTYPED(CreateResourceManager)
EMU_UNTYPED(CreateTransaction)
EMU_UNTYPED(CreateTransactionManager)
EMU_UNTYPED(CreateWnfStateName)
EMU_UNTYPED(DeleteWnfStateData)
EMU_UNTYPED(DeleteWnfStateName)
EMU_UNTYPED(EnumerateTransactionObject)
EMU_UNTYPED(GetNotificationResourceManager)
EMU_UNTYPED(OpenEnlistment)
EMU_UNTYPED(OpenResourceManager)
EMU_UNTYPED(OpenTransaction)
EMU_UNTYPED(OpenTransactionManager)
EMU_UNTYPED(PrepareComplete)
EMU_UNTYPED(PrepareEnlistment)
EMU_UNTYPED(PrePrepareComplete)
EMU_UNTYPED(PrePrepareEnlistment)
EMU_UNTYPED(PropagationComplete)
EMU_UNTYPED(PropagationFailed)
EMU_UNTYPED(QueryInformationEnlistment)
EMU_UNTYPED(QueryInformationResourceManager)
EMU_UNTYPED(QueryInformationTransaction)
EMU_UNTYPED(QueryInformationTransactionManager)
EMU_UNTYPED(QueryWnfStateData)
EMU_UNTYPED(QueryWnfStateNameInformation)
EMU_UNTYPED(ReadOnlyEnlistment)
EMU_UNTYPED(RecoverEnlistment)
EMU_UNTYPED(RecoverResourceManager)
EMU_UNTYPED(RecoverTransactionManager)
EMU_UNTYPED(RegisterProtocolAddressInformation)
EMU_UNTYPED(RenameTransactionManager)
EMU_UNTYPED(RollbackComplete)
EMU_UNTYPED(RollbackEnlistment)
EMU_UNTYPED(RollbackTransaction)
EMU_UNTYPED(RollforwardTransactionManager)
EMU_UNTYPED(SetInformationEnlistment)
EMU_UNTYPED(SetInformationResourceManager)
EMU_UNTYPED(SetInformationTransaction)
EMU_UNTYPED(SetInformationTransactionManager)
EMU_UNTYPED(SinglePhaseReject)
EMU_UNTYPED(SubscribeWnfStateChange)
EMU_UNTYPED(UnsubscribeWnfStateChange)
EMU_UNTYPED(UpdateWnfStateData)
#undef EMU_UNTYPED

template <typename Type>
constexpr ULONG64 EmuArgumentKind()
{
    static_assert(sizeof(Type) <= sizeof(ULONG64));
    if (sizeof(Type) == sizeof(ULONG64)) return EMU_ARGUMENT_NATIVE;
    if (sizeof(Type) == sizeof(ULONG)) return EMU_ARGUMENT_INT32;
    if (sizeof(Type) == sizeof(USHORT)) return std::is_signed<Type>::value ? EMU_ARGUMENT_INT16 : EMU_ARGUMENT_UINT16;
    return std::is_signed<Type>::value ? EMU_ARGUMENT_INT8 : EMU_ARGUMENT_UINT8;
}

template <ULONG Count, typename Function>
struct EMU_SIGNATURE;

template <ULONG Count, typename Result, typename... Types>
struct EMU_SIGNATURE<Count, Result (NTAPI *)(Types...)>
{
    static_assert(sizeof...(Types) * EMU_ARGUMENT_BITS <= 64);

    static constexpr ULONG64 Get()
    {
        ULONG64 Signature = 0;
        ULONG Shift = 0;

        if (sizeof...(Types) != Count) return 0;
        ((Signature |= EmuArgumentKind<Types>() << Shift, Shift += EMU_ARGUMENT_BITS), ...);
        return Signature;
    }
};

template <ULONG Count>
struct EMU_SIGNATURE<Count, const EMU_UNTYPED_SERVICE *>
{
    static constexpr ULONG64 Get()
    {
        return 0;
    }
};

}

#define SVC_WRAP_(Name, Count) SVC_(Name, Count)
#define SVC_(Name, Count) EMU_SIGNATURE<Count, decltype(&Nt##Name)>::Get(),

extern "C" const ULONG64 EmuNtSignatures[] =
{
#include <sysfuncs.h>
};
