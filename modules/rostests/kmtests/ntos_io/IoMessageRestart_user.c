/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A device restarted with message-signalled interrupts gets the same messages again
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <winuser.h>
#include <setupapi.h>
#include <cfgmgr32.h>

#define XHCI_ID L"PCI\\VEN_1B36&DEV_000D"
#define MAXIMUM_MESSAGES 64

static
BOOLEAN
FindController(
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
        Found = (_wcsnicmp(InstanceId, XHCI_ID, wcslen(XHCI_ID)) == 0);
    }

    SetupDiDestroyDeviceInfoList(Devices);
    return Found;
}

/* The raw message values the device was started with, as its allocated configuration lists them */
static
ULONG
ReadMessages(
    _In_ PCWSTR InstanceId,
    _Out_writes_(MAXIMUM_MESSAGES) PULONG Messages)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    PCM_RESOURCE_LIST List;
    WCHAR KeyName[MAX_DEVICE_ID_LEN + 64];
    UCHAR Data[2048];
    ULONG Count = 0, Index, Message;
    DWORD Type, Size = sizeof(Data);
    HKEY Key;
    LONG Error;

    StringCbPrintfW(KeyName, sizeof(KeyName), L"SYSTEM\\CurrentControlSet\\Enum\\%s\\Control", InstanceId);
    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, KeyName, 0, KEY_QUERY_VALUE, &Key);
    if (Error != ERROR_SUCCESS) return 0;

    Error = RegQueryValueExW(Key, L"AllocConfig", NULL, &Type, Data, &Size);
    RegCloseKey(Key);
    if (Error != ERROR_SUCCESS || Type != REG_RESOURCE_LIST || Size < sizeof(CM_RESOURCE_LIST)) return 0;

    List = (PCM_RESOURCE_LIST)Data;
    if (List->Count == 0) return 0;

    for (Index = 0; Index < List->List[0].PartialResourceList.Count; Index++)
    {
        Descriptor = &List->List[0].PartialResourceList.PartialDescriptors[Index];
        if ((PUCHAR)(Descriptor + 1) > Data + Size) break;
        if (Descriptor->Type != CmResourceTypeInterrupt ||
            !(Descriptor->Flags & CM_RESOURCE_INTERRUPT_MESSAGE))
        {
            continue;
        }

        for (Message = 0;
             Message < Descriptor->u.MessageInterrupt.Raw.MessageCount && Count < MAXIMUM_MESSAGES;
             Message++)
        {
            Messages[Count++] = Descriptor->u.MessageInterrupt.Raw.Vector + Message;
        }
    }

    return Count;
}

/* Disables and enables the xHCI controller of the test VM, so it is never part of a sweep */
START_TEST(IoMessageRestart)
{
    WCHAR InstanceId[MAX_DEVICE_ID_LEN];
    ULONG Before[MAXIMUM_MESSAGES], After[MAXIMUM_MESSAGES];
    ULONG BeforeCount, AfterCount, Index, Waited;
    ULONG DevStatus = 0, Problem = 0;
    CONFIGRET Result;
    DEVINST DevInst;

    if (skip(FindController(InstanceId, RTL_NUMBER_OF(InstanceId)), "No QEMU xHCI controller\n"))
        return;

    BeforeCount = ReadMessages(InstanceId, Before);
    if (skip(BeforeCount != 0, "The controller has no message interrupts\n"))
        return;

    Result = CM_Locate_DevNodeW(&DevInst, InstanceId, CM_LOCATE_DEVNODE_NORMAL);
    ok_eq_ulong(Result, (ULONG)CR_SUCCESS);
    if (Result != CR_SUCCESS) return;

    Result = CM_Disable_DevNode(DevInst, 0);
    ok_eq_ulong(Result, (ULONG)CR_SUCCESS);

    Result = CM_Enable_DevNode(DevInst, 0);
    ok_eq_ulong(Result, (ULONG)CR_SUCCESS);

    for (Waited = 0; Waited < 1000; Waited += 10)
    {
        if (CM_Get_DevNode_Status(&DevStatus, &Problem, DevInst, 0) == CR_SUCCESS &&
            (DevStatus & DN_STARTED))
        {
            break;
        }

        Sleep(10);
    }

    ok(DevStatus & DN_STARTED, "the controller did not start again (status 0x%lx, problem %lu)\n", DevStatus, Problem);
    trace("IoMessageRestart: the controller started again after %lu ms\n", Waited);

    AfterCount = ReadMessages(InstanceId, After);
    ok_eq_ulong(AfterCount, BeforeCount);

    for (Index = 0; Index < min(BeforeCount, AfterCount); Index++)
    {
        ok(After[Index] == Before[Index], "message %lu is 0x%lx after the restart, 0x%lx before\n",
           Index, After[Index], Before[Index]);
    }
}
