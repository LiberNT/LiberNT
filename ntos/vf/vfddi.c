/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     How the driver verifier tells a driver, a device node or the firmware it is wrong
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

#define VFP_LOGO_VERIFICATION_FLAGS 0x00000001

/* Set in a caller's assertion word once its accusation has been reported */
#define VFP_ASSERTION_REPORTED 0x00000001

/* Set in a caller's assertion word once its accusation has been forgiven */
#define VFP_ASSERTION_IGNORED 0x00000002

/* PRIVATE FUNCTIONS **********************************************************/

/* The verifier flags in force, or zero when nothing is being verified */
static
ULONG
VfpVerifierFlags(VOID)
{
    ULONG Flags = 0;

    if (!NT_SUCCESS(MmIsVerifierEnabled(&Flags))) return 0;

    return Flags;
}

static
BOOLEAN
VfpClaimAssertion(
    _Inout_opt_ PULONG AssertionControl)
{
    ULONG Previous;

    if (AssertionControl == NULL) return TRUE;

    Previous = *AssertionControl;
    *AssertionControl = Previous | VFP_ASSERTION_REPORTED;

    if ((Previous & VFP_ASSERTION_IGNORED) != 0) return FALSE;

    return (Previous & VFP_ASSERTION_REPORTED) == 0;
}

/* Writes the accusation and the caller's own words to the debugger */
static
VOID
VfpReport(
    _In_ PCSTR DebuggerMessageText,
    _In_opt_ PCSTR ParameterFormatString,
    _In_ va_list Parameters)
{
    if (DebuggerMessageText != NULL)
    {
        DbgPrintEx(DPFLTR_VERIFIER_ID, DPFLTR_ERROR_LEVEL, "%s\n", DebuggerMessageText);
    }

    if (ParameterFormatString != NULL)
    {
        vDbgPrintEx(DPFLTR_VERIFIER_ID,
                    DPFLTR_ERROR_LEVEL,
                    ParameterFormatString,
                    Parameters);
    }
}

static
VOID
VfpActOnFailure(
    _In_ ULONG BugCheckMajorCode,
    _In_ ULONG BugCheckMinorCode,
    _In_ VF_FAILURE_CLASS FailureClass,
    _In_ ULONG_PTR Object,
    _In_ ULONG VerifierFlags)
{
    if (FailureClass == VFFAILURE_FAIL_UNDER_DEBUGGER)
    {
        if (!KdDebuggerNotPresent) DbgBreakPoint();
        return;
    }

    if (FailureClass == VFFAILURE_FAIL_LOGO &&
        (VerifierFlags & VFP_LOGO_VERIFICATION_FLAGS) == 0)
    {
        return;
    }

    KeBugCheckEx(BugCheckMajorCode,
                 BugCheckMinorCode,
                 Object,
                 (ULONG_PTR)FailureClass,
                 0);
}

/* PUBLIC FUNCTIONS ***********************************************************/

BOOLEAN
NTAPI
VfIsVerificationEnabled(
    _In_ VF_OBJECT_TYPE VfObjectType,
    _In_opt_ PVOID Object)
{
    PDEVICE_OBJECT DeviceObject;

    if (VfpVerifierFlags() == 0) return FALSE;

    if (VfObjectType == VFOBJTYPE_SYSTEM_BIOS) return TRUE;

    if (Object == NULL) return FALSE;

    if (VfObjectType == VFOBJTYPE_DRIVER)
    {
        return MmIsDriverVerifying((PDRIVER_OBJECT)Object) ? TRUE : FALSE;
    }

    if (VfObjectType != VFOBJTYPE_DEVICE) return FALSE;

    DeviceObject = (PDEVICE_OBJECT)Object;

    if (DeviceObject->DriverObject == NULL) return FALSE;

    return MmIsDriverVerifying(DeviceObject->DriverObject) ? TRUE : FALSE;
}

VOID
__cdecl
VfFailDeviceNode(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _In_ ULONG BugCheckMajorCode,
    _In_ ULONG BugCheckMinorCode,
    _In_ VF_FAILURE_CLASS FailureClass,
    _Inout_ PULONG AssertionControl,
    _In_ PSTR DebuggerMessageText,
    _In_ PSTR ParameterFormatString,
    ...)
{
    ULONG VerifierFlags = VfpVerifierFlags();
    va_list Parameters;

    if (VerifierFlags == 0) return;

    if (!VfIsVerificationEnabled(VFOBJTYPE_DEVICE, PhysicalDeviceObject)) return;

    if (!VfpClaimAssertion(AssertionControl)) return;

    va_start(Parameters, ParameterFormatString);
    VfpReport(DebuggerMessageText, ParameterFormatString, Parameters);
    va_end(Parameters);

    VfpActOnFailure(BugCheckMajorCode,
                    BugCheckMinorCode,
                    FailureClass,
                    (ULONG_PTR)PhysicalDeviceObject,
                    VerifierFlags);
}

VOID
__cdecl
VfFailDriver(
    _In_ ULONG BugCheckMajorCode,
    _In_ ULONG BugCheckMinorCode,
    _In_ VF_FAILURE_CLASS FailureClass,
    _Inout_ PULONG AssertionControl,
    _In_ PSTR DebuggerMessageText,
    _In_ PSTR ParameterFormatString,
    ...)
{
    ULONG VerifierFlags = VfpVerifierFlags();
    va_list Parameters;

    if (VerifierFlags == 0) return;

    if (!VfpClaimAssertion(AssertionControl)) return;

    va_start(Parameters, ParameterFormatString);
    VfpReport(DebuggerMessageText, ParameterFormatString, Parameters);
    va_end(Parameters);

    /* TODO: name the offending driver from the return address of the caller */
    VfpActOnFailure(BugCheckMajorCode,
                    BugCheckMinorCode,
                    FailureClass,
                    0,
                    VerifierFlags);
}

VOID
__cdecl
VfFailSystemBIOS(
    _In_ ULONG BugCheckMajorCode,
    _In_ ULONG BugCheckMinorCode,
    _In_ VF_FAILURE_CLASS FailureClass,
    _Inout_ PULONG AssertionControl,
    _In_ PSTR DebuggerMessageText,
    _In_ PSTR ParameterFormatString,
    ...)
{
    ULONG VerifierFlags = VfpVerifierFlags();
    va_list Parameters;

    if (VerifierFlags == 0) return;

    if (!VfpClaimAssertion(AssertionControl)) return;

    va_start(Parameters, ParameterFormatString);
    VfpReport(DebuggerMessageText, ParameterFormatString, Parameters);
    va_end(Parameters);

    VfpActOnFailure(BugCheckMajorCode,
                    BugCheckMinorCode,
                    FailureClass,
                    0,
                    VerifierFlags);
}

/* EOF */
