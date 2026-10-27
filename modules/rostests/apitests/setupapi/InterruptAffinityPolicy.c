/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Interrupt affinity policy read from a device's hardware key
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <winreg.h>
#include <cfgmgr32.h>
#include <strsafe.h>

#define TEST_DEVICE_ID L"VEN_1B36&DEV_000D"
#define TEST_DEVICE_SLOT 0x1e
#define MAX_TEST_IRQS 32

typedef enum _EXPECT
{
    ExpectDefault,
    ExpectOverride,
    ExpectSpread,
    ExpectLine,
    ExpectNotStarted
} EXPECT;

typedef struct _SCENARIO
{
    PCSTR Name;
    LONG Policy;
    DWORD OverrideType;
    ULONG64 Override;
    DWORD OverrideSize;
    EXPECT Expect;
} SCENARIO;

typedef struct _IRQ_STATE
{
    BOOL Started;
    ULONG Problem;
    ULONG Count;
    BOOL Message[MAX_TEST_IRQS];
    ULONG64 Affinity[MAX_TEST_IRQS];
} IRQ_STATE;

static const SCENARIO Scenarios[] =
{
    { "none", -1, 0, 0, 0, ExpectDefault },
    { "specified binary", 4, REG_BINARY, 0x2, 1, ExpectOverride },
    { "specified dword", 4, REG_DWORD, 0x4, 4, ExpectOverride },
    { "specified qword", 4, REG_QWORD, 0x8, 8, ExpectOverride },
    { "specified two processors", 4, REG_BINARY, 0x6, 1, ExpectOverride },
    { "specified absent processor", 4, REG_BINARY, 0x100, 2, ExpectOverride },
    { "specified partly absent", 4, REG_BINARY, 0x102, 2, ExpectOverride },
    { "specified eight bytes", 4, REG_BINARY, 0x2, 8, ExpectOverride },
    { "specified without override", 4, 0, 0, 0, ExpectDefault },
    { "specified zero override", 4, REG_BINARY, 0, 1, ExpectDefault },
    { "specified string override", 4, REG_SZ, 0x0032, 4, ExpectDefault },
    { "machine default", 0, 0, 0, 0, ExpectDefault },
    { "all close", 1, 0, 0, 0, ExpectDefault },
    { "one close", 2, 0, 0, 0, ExpectDefault },
    { "all in machine", 3, 0, 0, 0, ExpectDefault },
    { "spread", 5, 0, 0, 0, ExpectSpread },
    { "steered", 6, 0, 0, 0, ExpectDefault },
    { "machine default override", 0, REG_BINARY, 0x2, 1, ExpectOverride },
    { "all close override", 1, REG_BINARY, 0x2, 1, ExpectOverride },
    { "one close override", 2, REG_BINARY, 0x6, 1, ExpectOverride },
    { "all in machine override", 3, REG_BINARY, 0x2, 1, ExpectOverride },
    { "spread override", 5, REG_BINARY, 0x6, 1, ExpectSpread },
    { "steered override", 6, REG_BINARY, 0x2, 1, ExpectOverride },
    { "override only", -1, REG_BINARY, 0x2, 1, ExpectOverride },
    { "invalid policy 7", 7, 0, 0, 0, ExpectNotStarted },
    { "invalid policy 8", 8, 0, 0, 0, ExpectNotStarted },
    { "invalid policy override", 7, REG_BINARY, 0x2, 1, ExpectLine },
    { "cleanup", -1, 0, 0, 0, ExpectDefault },
};

static WCHAR InstanceId[MAX_DEVICE_ID_LEN];
static WCHAR PolicyPath[MAX_PATH * 2];
static DEVINST DevInst;

static BOOL FindTestDevice(void)
{
    WCHAR *List, *Id;
    ULONG Size = 0, Length, Type, Address;
    WCHAR HardwareIds[1024];
    DEVINST Node;

    if (CM_Get_Device_ID_List_SizeW(&Size, L"PCI", CM_GETIDLIST_FILTER_ENUMERATOR) != CR_SUCCESS || !Size)
        return FALSE;
    List = HeapAlloc(GetProcessHeap(), 0, Size * sizeof(WCHAR));
    if (!List)
        return FALSE;
    if (CM_Get_Device_ID_ListW(L"PCI", List, Size, CM_GETIDLIST_FILTER_ENUMERATOR) != CR_SUCCESS)
    {
        HeapFree(GetProcessHeap(), 0, List);
        return FALSE;
    }

    for (Id = List; *Id; Id += wcslen(Id) + 1)
    {
        if (CM_Locate_DevNodeW(&Node, Id, CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
            continue;
        Length = sizeof(HardwareIds);
        if (CM_Get_DevNode_Registry_PropertyW(Node, CM_DRP_HARDWAREID, &Type, HardwareIds, &Length, 0) != CR_SUCCESS)
            continue;
        Length = sizeof(Address);
        if (CM_Get_DevNode_Registry_PropertyW(Node, CM_DRP_ADDRESS, &Type, &Address, &Length, 0) != CR_SUCCESS)
            continue;
        if (wcsstr(HardwareIds, TEST_DEVICE_ID) && Address == (TEST_DEVICE_SLOT << 16))
        {
            StringCchCopyW(InstanceId, _countof(InstanceId), Id);
            DevInst = Node;
            break;
        }
    }

    HeapFree(GetProcessHeap(), 0, List);
    return InstanceId[0] != 0;
}

static LONG ApplyScenario(const SCENARIO *Scenario)
{
    HKEY Key;
    LONG Error;
    DWORD Policy;

    Error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, PolicyPath, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &Key, NULL);
    if (Error != ERROR_SUCCESS)
        return Error;

    RegDeleteValueW(Key, L"DevicePolicy");
    RegDeleteValueW(Key, L"AssignmentSetOverride");
    if (Scenario->Policy >= 0)
    {
        Policy = (DWORD)Scenario->Policy;
        Error = RegSetValueExW(Key, L"DevicePolicy", 0, REG_DWORD, (const BYTE *)&Policy, sizeof(Policy));
    }
    if (Error == ERROR_SUCCESS && Scenario->OverrideType)
    {
        Error = RegSetValueExW(Key, L"AssignmentSetOverride", 0, Scenario->OverrideType,
                               (const BYTE *)&Scenario->Override, Scenario->OverrideSize);
    }
    RegCloseKey(Key);
    return Error;
}

static void RestartDevice(void)
{
    ULONG Status, Problem, Try;

    ok_int(CM_Disable_DevNode(DevInst, CM_DISABLE_UI_NOT_OK), CR_SUCCESS);
    ok_int(CM_Enable_DevNode(DevInst, 0), CR_SUCCESS);
    for (Try = 0; Try < 50; Try++)
    {
        if (CM_Get_DevNode_Status(&Status, &Problem, DevInst, 0) == CR_SUCCESS &&
            ((Status & DN_STARTED) || (Status & DN_HAS_PROBLEM)))
        {
            break;
        }
        Sleep(200);
    }
}

static void ReadState(IRQ_STATE *State)
{
    LOG_CONF Conf;
    RES_DES Res, Next;
    RESOURCEID Resource;
    IRQ_DES Irq[2];
    ULONG Status = 0, Size;

    ZeroMemory(State, sizeof(*State));
    CM_Get_DevNode_Status(&Status, &State->Problem, DevInst, 0);
    State->Started = (Status & DN_STARTED) != 0;
    if (!State->Started || CM_Get_First_Log_Conf(&Conf, DevInst, ALLOC_LOG_CONF) != CR_SUCCESS)
        return;

    Res = Conf;
    while (State->Count < MAX_TEST_IRQS &&
           CM_Get_Next_Res_Des(&Next, Res, ResType_IRQ, &Resource, 0) == CR_SUCCESS)
    {
        if (Res != Conf)
            CM_Free_Res_Des_Handle(Res);
        Res = Next;
        if (CM_Get_Res_Des_Data_Size(&Size, Res, 0) != CR_SUCCESS || Size > sizeof(Irq) ||
            CM_Get_Res_Des_Data(Res, Irq, Size, 0) != CR_SUCCESS)
        {
            continue;
        }
        State->Message[State->Count] = (Irq[0].IRQD_Flags & fIRQD_Edge) != 0;
        State->Affinity[State->Count] = Irq[0].IRQD_Affinity;
        State->Count++;
    }
    if (Res != Conf)
        CM_Free_Res_Des_Handle(Res);
    CM_Free_Log_Conf_Handle(Conf);
}

static ULONG64 SpreadAffinity(ULONG64 Active, ULONG Index)
{
    ULONG Count = 0, Bit;

    for (Bit = 0; Bit < 64; Bit++)
        Count += (Active >> Bit) & 1;
    Index %= Count;
    for (Bit = 0; Bit < 64; Bit++)
    {
        if (((Active >> Bit) & 1) && Index-- == 0)
            break;
    }
    return 1ULL << Bit;
}

static void CheckScenario(const SCENARIO *Scenario, const IRQ_STATE *Baseline, const IRQ_STATE *State, ULONG64 Active)
{
    EXPECT Expect = Scenario->Expect;
    ULONG i;

    if (Expect == ExpectOverride && (Scenario->Override & ~Active))
        Expect = ExpectLine;

    if (Expect == ExpectNotStarted)
    {
        ok(!State->Started, "%s: device started\n", Scenario->Name);
        ok_long(State->Problem, CM_PROB_NORMAL_CONFLICT);
        return;
    }

    ok(State->Started, "%s: device not started, problem %lu\n", Scenario->Name, State->Problem);
    if (Expect == ExpectLine)
    {
        ok(State->Count == 1 && !State->Message[0], "%s: %lu interrupts, message %d\n",
           Scenario->Name, State->Count, State->Count ? State->Message[0] : -1);
        return;
    }

    ok(State->Count == Baseline->Count, "%s: %lu interrupts, expected %lu\n", Scenario->Name, State->Count, Baseline->Count);
    for (i = 0; i < State->Count && i < Baseline->Count; i++)
    {
        ULONG64 Expected = Baseline->Affinity[i];

        if (Expect == ExpectOverride)
            Expected = Scenario->Override;
        else if (Expect == ExpectSpread)
            Expected = SpreadAffinity(Active, i);
        ok(State->Message[i] == Baseline->Message[i], "%s: interrupt %lu changed kind\n", Scenario->Name, i);
        ok(State->Affinity[i] == Expected, "%s: interrupt %lu affinity 0x%I64x, expected 0x%I64x\n",
           Scenario->Name, i, State->Affinity[i], Expected);
    }
}

START_TEST(InterruptAffinityPolicy)
{
    IRQ_STATE Baseline, State;
    SYSTEM_INFO Info;
    ULONG64 Active;
    LONG Error;
    ULONG i;

    if (!FindTestDevice())
    {
        skip("No QEMU xHCI controller in PCI slot 0x%x\n", TEST_DEVICE_SLOT);
        return;
    }

    StringCchPrintfW(PolicyPath, _countof(PolicyPath),
                     L"SYSTEM\\CurrentControlSet\\Enum\\%s\\Device Parameters\\Interrupt Management\\Affinity Policy",
                     InstanceId);
    GetSystemInfo(&Info);
    Active = Info.dwActiveProcessorMask;

    Error = ApplyScenario(&Scenarios[0]);
    if (Error == ERROR_ACCESS_DENIED)
    {
        skip("The device hardware key is not writable\n");
        return;
    }
    ok_long(Error, ERROR_SUCCESS);
    RestartDevice();
    ReadState(&Baseline);
    ok(Baseline.Started, "Test device not started\n");
    ok(Baseline.Count > 0, "Test device has no interrupt\n");
    if (!Baseline.Started || !Baseline.Count)
        return;

    for (i = 0; i < _countof(Scenarios); i++)
    {
        ok_long(ApplyScenario(&Scenarios[i]), ERROR_SUCCESS);
        RestartDevice();
        ReadState(&State);
        CheckScenario(&Scenarios[i], &Baseline, &State, Active);
    }
}
