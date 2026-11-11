/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The message-signalled interrupts a device is restarted with: the same messages, on the processors its affinity policy names
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <winuser.h>
#include <setupapi.h>
#include <cfgmgr32.h>

#define XHCI_ID L"PCI\\VEN_1B36&DEV_000D"
#define E1000E_ID L"PCI\\VEN_8086&DEV_10D3"
#define MAXIMUM_MESSAGES 64
#define POLICY_KEY L"Device Parameters\\Interrupt Management\\Affinity Policy"

typedef struct _ALLOCATION
{
    ULONG Messages;
    ULONG Vector[MAXIMUM_MESSAGES];
    KAFFINITY Affinity[MAXIMUM_MESSAGES];
    ULONG LineInterrupts;
} ALLOCATION, *PALLOCATION;

static
BOOLEAN
FindController(
    _In_ PCWSTR HardwareId,
    _Out_writes_(Length) PWSTR InstanceId,
    _In_ ULONG Length)
{
    SP_DEVINFO_DATA Device;
    HDEVINFO Devices;
    BOOLEAN Found = FALSE;
    ULONG Index;

    Devices = SetupDiGetClassDevsW(NULL, L"PCI", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (Devices == INVALID_HANDLE_VALUE) return FALSE;

    Device.cbSize = sizeof(Device);
    for (Index = 0; !Found && SetupDiEnumDeviceInfo(Devices, Index, &Device); Index++)
    {
        if (!SetupDiGetDeviceInstanceIdW(Devices, &Device, InstanceId, Length, NULL)) continue;
        Found = (_wcsnicmp(InstanceId, HardwareId, wcslen(HardwareId)) == 0);
    }

    SetupDiDestroyDeviceInfoList(Devices);
    return Found;
}

/* The interrupts the device was started with, as its allocated configuration lists them */
static
VOID
ReadAllocation(
    _In_ PCWSTR InstanceId,
    _Out_ PALLOCATION Allocation)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    PCM_RESOURCE_LIST List;
    WCHAR KeyName[MAX_DEVICE_ID_LEN + 64];
    UCHAR Data[2048];
    ULONG Index, Message;
    DWORD Type, Size = sizeof(Data);
    HKEY Key;
    LONG Error;

    RtlZeroMemory(Allocation, sizeof(*Allocation));

    StringCbPrintfW(KeyName, sizeof(KeyName), L"SYSTEM\\CurrentControlSet\\Enum\\%s\\Control", InstanceId);
    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, KeyName, 0, KEY_QUERY_VALUE, &Key);
    if (Error != ERROR_SUCCESS) return;

    Error = RegQueryValueExW(Key, L"AllocConfig", NULL, &Type, Data, &Size);
    RegCloseKey(Key);
    if (Error != ERROR_SUCCESS || Type != REG_RESOURCE_LIST || Size < sizeof(CM_RESOURCE_LIST)) return;

    List = (PCM_RESOURCE_LIST)Data;
    if (List->Count == 0) return;

    for (Index = 0; Index < List->List[0].PartialResourceList.Count; Index++)
    {
        Descriptor = &List->List[0].PartialResourceList.PartialDescriptors[Index];
        if ((PUCHAR)(Descriptor + 1) > Data + Size) break;
        if (Descriptor->Type != CmResourceTypeInterrupt) continue;

        if (!(Descriptor->Flags & CM_RESOURCE_INTERRUPT_MESSAGE))
        {
            Allocation->LineInterrupts++;
            continue;
        }

        for (Message = 0;
             Message < Descriptor->u.MessageInterrupt.Raw.MessageCount && Allocation->Messages < MAXIMUM_MESSAGES;
             Message++)
        {
            Allocation->Vector[Allocation->Messages] = Descriptor->u.MessageInterrupt.Raw.Vector + Message;
            Allocation->Affinity[Allocation->Messages] = Descriptor->u.MessageInterrupt.Raw.Affinity;
            Allocation->Messages++;
        }
    }
}

/* Disables and enables the controller, then reports whether it started and with which problem */
static
BOOLEAN
RestartController(
    _In_ PCWSTR InstanceId,
    _Out_ PULONG Problem)
{
    ULONG DevStatus = 0, Waited;
    CONFIGRET Result;
    DEVINST DevInst;

    *Problem = 0;

    Result = CM_Locate_DevNodeW(&DevInst, (DEVINSTID_W)InstanceId, CM_LOCATE_DEVNODE_NORMAL);
    ok_eq_ulong(Result, (ULONG)CR_SUCCESS);
    if (Result != CR_SUCCESS) return FALSE;

    Result = CM_Disable_DevNode(DevInst, 0);
    ok_eq_ulong(Result, (ULONG)CR_SUCCESS);

    Result = CM_Enable_DevNode(DevInst, 0);
    ok_eq_ulong(Result, (ULONG)CR_SUCCESS);

    for (Waited = 0; Waited < 1000; Waited += 10)
    {
        if (CM_Get_DevNode_Status(&DevStatus, Problem, DevInst, 0) == CR_SUCCESS &&
            (DevStatus & (DN_STARTED | DN_HAS_PROBLEM)))
        {
            break;
        }

        Sleep(10);
    }

    return (DevStatus & DN_STARTED) != 0;
}

static
VOID
SetPolicy(
    _In_ PCWSTR InstanceId,
    _In_ BOOLEAN Present,
    _In_opt_ PULONG Policy,
    _In_opt_ PKAFFINITY Override)
{
    WCHAR KeyName[MAX_DEVICE_ID_LEN + 96];
    HKEY Key;
    LONG Error;

    StringCbPrintfW(KeyName, sizeof(KeyName), L"SYSTEM\\CurrentControlSet\\Enum\\%s\\" POLICY_KEY, InstanceId);

    if (!Present)
    {
        RegDeleteKeyW(HKEY_LOCAL_MACHINE, KeyName);
        return;
    }

    Error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, KeyName, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &Key, NULL);
    ok_eq_long(Error, ERROR_SUCCESS);
    if (Error != ERROR_SUCCESS) return;

    RegDeleteValueW(Key, L"DevicePolicy");
    RegDeleteValueW(Key, L"AssignmentSetOverride");

    if (Policy != NULL)
        RegSetValueExW(Key, L"DevicePolicy", 0, REG_DWORD, (const BYTE *)Policy, sizeof(*Policy));
    if (Override != NULL)
        RegSetValueExW(Key, L"AssignmentSetOverride", 0, REG_BINARY, (const BYTE *)Override, sizeof(*Override));

    RegCloseKey(Key);
}

static
VOID
CheckMessageAffinity(
    _In_ PCSTR Case,
    _In_ PALLOCATION Allocation,
    _In_ ULONG Messages,
    _In_ BOOLEAN Spread,
    _In_ KAFFINITY Expected)
{
    KAFFINITY Active = 0;
    SYSTEM_INFO Info;
    ULONG Index, Bit, Seen, Count = 0;

    GetSystemInfo(&Info);
    Active = Info.dwActiveProcessorMask;

    for (Bit = 0; Bit < sizeof(KAFFINITY) * 8; Bit++)
    {
        if (Active & ((KAFFINITY)1 << Bit)) Count++;
    }

    ok(Allocation->Messages == Messages, "%s: %lu messages, expected %lu\n", Case, Allocation->Messages, Messages);
    ok(Allocation->LineInterrupts == 0, "%s: %lu line interrupts\n", Case, Allocation->LineInterrupts);

    for (Index = 0; Index < Allocation->Messages; Index++)
    {
        if (Spread)
        {
            Seen = Index % Count;
            Expected = 0;
            for (Bit = 0; Bit < sizeof(KAFFINITY) * 8; Bit++)
            {
                if ((Active & ((KAFFINITY)1 << Bit)) && Seen-- == 0)
                {
                    Expected = (KAFFINITY)1 << Bit;
                    break;
                }
            }
        }

        ok(Allocation->Affinity[Index] == Expected, "%s: message %lu raw affinity 0x%Ix, expected 0x%Ix\n",
           Case, Index, Allocation->Affinity[Index], Expected);
    }
}

/* Disables and enables the xHCI controller of the test VM, so it is never part of a sweep */
START_TEST(IoMessageRestart)
{
    WCHAR InstanceId[MAX_DEVICE_ID_LEN];
    ALLOCATION Before, After;
    ULONG Index, Problem;
    BOOLEAN Started;

    if (skip(FindController(XHCI_ID, InstanceId, RTL_NUMBER_OF(InstanceId)), "No QEMU xHCI controller\n"))
        return;

    ReadAllocation(InstanceId, &Before);
    if (skip(Before.Messages != 0, "The controller has no message interrupts\n"))
        return;

    Started = RestartController(InstanceId, &Problem);
    ok(Started, "the controller did not start again (problem %lu)\n", Problem);

    ReadAllocation(InstanceId, &After);
    ok_eq_ulong(After.Messages, Before.Messages);

    for (Index = 0; Index < min(Before.Messages, After.Messages); Index++)
    {
        ok(After.Vector[Index] == Before.Vector[Index], "message %lu is 0x%lx after the restart, 0x%lx before\n",
           Index, After.Vector[Index], Before.Vector[Index]);
    }
}

/* The adapter's driver starts late in the boot; the test waits for it, not for anything it checks */
static
BOOLEAN
WaitStarted(
    _In_ PCWSTR InstanceId)
{
    ULONG DevStatus = 0, Problem = 0, Waited;
    DEVINST DevInst;

    if (CM_Locate_DevNodeW(&DevInst, (DEVINSTID_W)InstanceId, CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
        return FALSE;

    for (Waited = 0; Waited < 5000; Waited += 10)
    {
        if (CM_Get_DevNode_Status(&DevStatus, &Problem, DevInst, 0) == CR_SUCCESS &&
            (DevStatus & DN_STARTED))
        {
            return TRUE;
        }

        Sleep(10);
    }

    return FALSE;
}

/* Restarts the e1000e adapter of the test VM under each affinity policy, so it is never part of a sweep */
START_TEST(IoAffinityPolicy)
{
    WCHAR InstanceId[MAX_DEVICE_ID_LEN];
    ALLOCATION Allocation;
    KAFFINITY Override;
    ULONG Messages, Policy, Problem;
    BOOLEAN Started;

    if (skip(FindController(E1000E_ID, InstanceId, RTL_NUMBER_OF(InstanceId)), "No QEMU e1000e adapter\n"))
        return;

    if (skip(WaitStarted(InstanceId), "The e1000e adapter did not start\n"))
        return;

    SetPolicy(InstanceId, FALSE, NULL, NULL);
    Started = RestartController(InstanceId, &Problem);
    ok(Started, "no key: the controller did not start (problem %lu)\n", Problem);
    ReadAllocation(InstanceId, &Allocation);
    Messages = Allocation.Messages;
    if (skip(Messages != 0, "The controller has no message interrupts\n"))
        return;
    CheckMessageAffinity("no key", &Allocation, Messages, FALSE, 0);

    SetPolicy(InstanceId, TRUE, NULL, NULL);
    Started = RestartController(InstanceId, &Problem);
    ok(Started, "empty key: the controller did not start (problem %lu)\n", Problem);
    ReadAllocation(InstanceId, &Allocation);
    CheckMessageAffinity("empty key", &Allocation, Messages, FALSE, 0);

    Policy = IrqPolicySpreadMessagesAcrossAllProcessors;
    SetPolicy(InstanceId, TRUE, &Policy, NULL);
    Started = RestartController(InstanceId, &Problem);
    ok(Started, "spread: the controller did not start (problem %lu)\n", Problem);
    ReadAllocation(InstanceId, &Allocation);
    CheckMessageAffinity("spread", &Allocation, Messages, TRUE, 0);

    Policy = IrqPolicySpecifiedProcessors;
    Override = 2;
    SetPolicy(InstanceId, TRUE, &Policy, &Override);
    Started = RestartController(InstanceId, &Problem);
    ok(Started, "specified: the controller did not start (problem %lu)\n", Problem);
    ReadAllocation(InstanceId, &Allocation);
    CheckMessageAffinity("specified", &Allocation, Messages, FALSE, 2);

    Policy = IrqPolicyMachineDefault;
    SetPolicy(InstanceId, TRUE, &Policy, &Override);
    Started = RestartController(InstanceId, &Problem);
    ok(Started, "default with an override: the controller did not start (problem %lu)\n", Problem);
    ReadAllocation(InstanceId, &Allocation);
    CheckMessageAffinity("default with an override", &Allocation, Messages, FALSE, 2);

    Policy = IrqPolicySpecifiedProcessors;
    Override = (KAFFINITY)1 << 2;
    SetPolicy(InstanceId, TRUE, &Policy, &Override);
    Started = RestartController(InstanceId, &Problem);
    ok(Started, "absent processor: the controller did not start (problem %lu)\n", Problem);
    ReadAllocation(InstanceId, &Allocation);
    ok(Allocation.Messages == 0, "absent processor: %lu messages\n", Allocation.Messages);
    ok(Allocation.LineInterrupts == 1, "absent processor: %lu line interrupts\n", Allocation.LineInterrupts);

    Policy = IrqPolicyAllProcessorsInMachineWhenSteered + 1;
    SetPolicy(InstanceId, TRUE, &Policy, NULL);
    Started = RestartController(InstanceId, &Problem);
    ok(!Started, "unknown policy: the controller started\n");
    ok_eq_ulong(Problem, (ULONG)CM_PROB_NORMAL_CONFLICT);

    Override = 1;
    SetPolicy(InstanceId, TRUE, &Policy, &Override);
    Started = RestartController(InstanceId, &Problem);
    ok(Started, "unknown policy with an override: the controller did not start (problem %lu)\n", Problem);
    ReadAllocation(InstanceId, &Allocation);
    ok(Allocation.Messages == 0, "unknown policy with an override: %lu messages\n", Allocation.Messages);
    ok(Allocation.LineInterrupts == 1, "unknown policy with an override: %lu line interrupts\n", Allocation.LineInterrupts);

    SetPolicy(InstanceId, FALSE, NULL, NULL);
    Started = RestartController(InstanceId, &Problem);
    ok(Started, "restored: the controller did not start (problem %lu)\n", Problem);
    ReadAllocation(InstanceId, &Allocation);
    ok_eq_ulong(Allocation.Messages, Messages);
}
