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
#define LINE_RAW_AFFINITY 0xffffffff
#define RESOURCE_MAP_KEY L"HARDWARE\\RESOURCEMAP\\PnP Manager\\PnpManager"
#define MAP_TYPE_INTERRUPT 2
#define MAP_INTERRUPT_LATCHED 0x1
#define MAP_INTERRUPT_MESSAGE 0x2
#define MAP_INTERRUPT_POLICY_INCLUDED 0x4

typedef enum _EXPECT_KIND
{
    KindMessage,
    KindLine,
    KindNotStarted
} EXPECT_KIND;

typedef enum _EXPECT_RAW
{
    RawZero,
    RawOverride,
    RawSpread
} EXPECT_RAW;

typedef enum _EXPECT_TRANS
{
    TransActive,
    TransOverride,
    TransSpread,
    TransOneClose
} EXPECT_TRANS;

typedef struct _SCENARIO
{
    PCSTR Name;
    LONG Policy;
    DWORD OverrideType;
    ULONG64 Override;
    DWORD OverrideSize;
    EXPECT_KIND Kind;
    EXPECT_RAW Raw;
    EXPECT_TRANS Trans;
} SCENARIO;

typedef struct _MAP_STATE
{
    BOOL Present;
    ULONG Count;
    USHORT Flags[MAX_TEST_IRQS];
    ULONG64 Affinity[MAX_TEST_IRQS];
} MAP_STATE;

typedef struct _IRQ_STATE
{
    BOOL Started;
    ULONG Problem;
    CONFIGRET AllocResult;
    ULONG Count;
    BOOL Message[MAX_TEST_IRQS];
    ULONG Alloc[MAX_TEST_IRQS];
    ULONG64 Affinity[MAX_TEST_IRQS];
    MAP_STATE Raw;
    MAP_STATE Translated;
} IRQ_STATE;

#pragma pack(push, 4)
typedef struct _MAP_FULL_DESCRIPTOR
{
    ULONG InterfaceType;
    ULONG BusNumber;
    USHORT Version;
    USHORT Revision;
    ULONG Count;
} MAP_FULL_DESCRIPTOR;

typedef struct _MAP_INTERRUPT_DESCRIPTOR
{
    UCHAR Type;
    UCHAR ShareDisposition;
    USHORT Flags;
    USHORT Level;
    USHORT Group;
    ULONG Vector;
    KAFFINITY Affinity;
} MAP_INTERRUPT_DESCRIPTOR;
#pragma pack(pop)

static const SCENARIO Scenarios[] =
{
    { "none", -1, 0, 0, 0, KindMessage, RawZero, TransActive },
    { "specified binary", 4, REG_BINARY, 0x2, 1, KindMessage, RawOverride, TransOverride },
    { "specified dword", 4, REG_DWORD, 0x4, 4, KindMessage, RawOverride, TransOverride },
    { "specified qword", 4, REG_QWORD, 0x8, 8, KindMessage, RawOverride, TransOverride },
    { "specified absent processor", 4, REG_BINARY, 0x100, 2, KindMessage, RawOverride, TransOverride },
    { "specified two processors", 4, REG_BINARY, 0x6, 1, KindMessage, RawOverride, TransOverride },
    { "specified without override", 4, 0, 0, 0, KindMessage, RawZero, TransActive },
    { "specified zero override", 4, REG_BINARY, 0, 1, KindMessage, RawZero, TransActive },
    { "machine default", 0, 0, 0, 0, KindMessage, RawZero, TransActive },
    { "all close", 1, 0, 0, 0, KindMessage, RawZero, TransActive },
    { "one close", 2, 0, 0, 0, KindMessage, RawZero, TransOneClose },
    { "all in machine", 3, 0, 0, 0, KindMessage, RawZero, TransActive },
    { "spread", 5, 0, 0, 0, KindMessage, RawSpread, TransSpread },
    { "steered", 6, 0, 0, 0, KindMessage, RawZero, TransActive },
    { "invalid policy 7", 7, 0, 0, 0, KindNotStarted, RawZero, TransActive },
    { "all in machine override", 3, REG_BINARY, 0x2, 1, KindMessage, RawOverride, TransActive },
    { "machine default override", 0, REG_BINARY, 0x2, 1, KindMessage, RawOverride, TransActive },
    { "all close override", 1, REG_BINARY, 0x2, 1, KindMessage, RawOverride, TransActive },
    { "one close override", 2, REG_BINARY, 0x2, 1, KindMessage, RawOverride, TransOneClose },
    { "one close two processors", 2, REG_BINARY, 0x6, 1, KindMessage, RawOverride, TransOneClose },
    { "spread override", 5, REG_BINARY, 0x6, 1, KindMessage, RawSpread, TransSpread },
    { "steered override", 6, REG_BINARY, 0x2, 1, KindMessage, RawOverride, TransActive },
    { "override only", -1, REG_BINARY, 0x2, 1, KindMessage, RawOverride, TransActive },
    { "specified partly absent", 4, REG_BINARY, 0x102, 2, KindMessage, RawOverride, TransOverride },
    { "specified eight processors", 4, REG_BINARY, 0xff, 1, KindMessage, RawOverride, TransOverride },
    { "specified four processors", 4, REG_BINARY, 0xf, 1, KindMessage, RawOverride, TransOverride },
    { "specified eight bytes", 4, REG_BINARY, 0x2, 8, KindMessage, RawOverride, TransOverride },
    { "specified string override", 4, REG_SZ, 0x0032, 4, KindMessage, RawZero, TransActive },
    { "invalid policy 8", 8, 0, 0, 0, KindNotStarted, RawZero, TransActive },
    { "invalid policy override", 7, REG_BINARY, 0x2, 1, KindLine, RawZero, TransOverride },
    { "machine default absent processor", 0, REG_BINARY, 0x100, 2, KindMessage, RawOverride, TransActive },
    { "one close absent processor", 2, REG_BINARY, 0x100, 2, KindMessage, RawOverride, TransOneClose },
    { "spread absent processor", 5, REG_BINARY, 0x100, 2, KindMessage, RawSpread, TransSpread },
    { "override only absent processor", -1, REG_BINARY, 0x100, 2, KindMessage, RawOverride, TransActive },
    { "one close again", 2, 0, 0, 0, KindMessage, RawZero, TransOneClose },
    { "cleanup", -1, 0, 0, 0, KindMessage, RawZero, TransActive },
};

static WCHAR InstanceId[MAX_DEVICE_ID_LEN];
static WCHAR PolicyPath[MAX_PATH * 2];
static DEVINST DevInst;
static LONG LastOneCloseCpu = -1;

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

static void ReadMap(PCWSTR PdoName, PCWSTR Suffix, MAP_STATE *Map)
{
    static BYTE Data[8192];
    WCHAR Name[MAX_PATH];
    HKEY Key;
    DWORD Size = sizeof(Data);
    ULONG Lists, List, Index, Offset;
    MAP_FULL_DESCRIPTOR Full;
    MAP_INTERRUPT_DESCRIPTOR Descriptor;

    ZeroMemory(Map, sizeof(*Map));
    if (FAILED(StringCchPrintfW(Name, _countof(Name), L"%s%s", PdoName, Suffix)))
        return;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, RESOURCE_MAP_KEY, 0, KEY_READ, &Key) != ERROR_SUCCESS)
        return;
    if (RegQueryValueExW(Key, Name, NULL, NULL, Data, &Size) != ERROR_SUCCESS || Size < sizeof(ULONG))
    {
        RegCloseKey(Key);
        return;
    }
    RegCloseKey(Key);

    Map->Present = TRUE;
    CopyMemory(&Lists, Data, sizeof(Lists));
    Offset = sizeof(ULONG);
    for (List = 0; List < Lists && Offset + sizeof(Full) <= Size; List++)
    {
        CopyMemory(&Full, Data + Offset, sizeof(Full));
        Offset += sizeof(Full);
        for (Index = 0; Index < Full.Count && Offset + sizeof(Descriptor) <= Size; Index++)
        {
            CopyMemory(&Descriptor, Data + Offset, sizeof(Descriptor));
            Offset += sizeof(Descriptor);
            if (Descriptor.Type != MAP_TYPE_INTERRUPT || Map->Count >= MAX_TEST_IRQS)
                continue;
            Map->Flags[Map->Count] = Descriptor.Flags;
            Map->Affinity[Map->Count] = Descriptor.Affinity;
            Map->Count++;
        }
    }
}

static void ReadState(IRQ_STATE *State)
{
    LOG_CONF Conf;
    RES_DES Res, Next;
    RESOURCEID Resource;
    IRQ_DES Irq[2];
    WCHAR PdoName[MAX_PATH];
    ULONG Status = 0, Size, Type;

    ZeroMemory(State, sizeof(*State));
    CM_Get_DevNode_Status(&Status, &State->Problem, DevInst, 0);
    State->Started = (Status & DN_STARTED) != 0;
    State->AllocResult = CM_Get_First_Log_Conf(&Conf, DevInst, ALLOC_LOG_CONF);
    if (State->AllocResult != CR_SUCCESS)
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
        State->Alloc[State->Count] = Irq[0].IRQD_Alloc_Num;
        State->Affinity[State->Count] = Irq[0].IRQD_Affinity;
        State->Count++;
    }
    if (Res != Conf)
        CM_Free_Res_Des_Handle(Res);
    CM_Free_Log_Conf_Handle(Conf);

    Size = sizeof(PdoName);
    if (CM_Get_DevNode_Registry_PropertyW(DevInst, CM_DRP_PHYSICAL_DEVICE_OBJECT_NAME, &Type, PdoName, &Size, 0) != CR_SUCCESS)
        return;
    ReadMap(PdoName, L".Raw", &State->Raw);
    ReadMap(PdoName, L".Translated", &State->Translated);
}

static ULONG ProcessorCount(ULONG64 Active)
{
    ULONG Count = 0, Bit;

    for (Bit = 0; Bit < 64; Bit++)
        Count += (Active >> Bit) & 1;
    return Count;
}

static ULONG64 ActiveProcessor(ULONG64 Active, ULONG Index)
{
    ULONG Bit;

    Index %= ProcessorCount(Active);
    for (Bit = 0; Bit < 64; Bit++)
    {
        if (((Active >> Bit) & 1) && Index-- == 0)
            break;
    }
    return 1ULL << Bit;
}

static LONG ActiveIndex(ULONG64 Active, ULONG64 Affinity)
{
    ULONG Bit, Index = 0;

    if (ProcessorCount(Affinity) != 1 || !(Affinity & Active))
        return -1;
    for (Bit = 0; Bit < 64; Bit++)
    {
        if ((Affinity >> Bit) & 1)
            return Index;
        Index += (Active >> Bit) & 1;
    }
    return -1;
}

static void CheckOneClose(const SCENARIO *Scenario, const MAP_STATE *Translated, ULONG64 Active)
{
    ULONG Count = ProcessorCount(Active), i;
    LONG Cpu;

    for (i = 0; i < Translated->Count; i++)
    {
        Cpu = ActiveIndex(Active, Translated->Affinity[i]);
        ok(Cpu >= 0, "%s: message %lu translated affinity 0x%I64x is not one active processor\n",
           Scenario->Name, i, Translated->Affinity[i]);
        if (Cpu < 0)
            return;
        if (LastOneCloseCpu >= 0)
        {
            ok(Cpu == (LONG)((LastOneCloseCpu + 1) % Count),
               "%s: message %lu on processor %ld, expected %ld\n",
               Scenario->Name, i, Cpu, (LONG)((LastOneCloseCpu + 1) % Count));
        }
        LastOneCloseCpu = Cpu;
    }
}

static void CheckMessages(const SCENARIO *Scenario, const IRQ_STATE *Baseline, const IRQ_STATE *State,
                          BOOL PolicyKey, ULONG64 Active)
{
    USHORT Flags = MAP_INTERRUPT_LATCHED | MAP_INTERRUPT_MESSAGE | (PolicyKey ? MAP_INTERRUPT_POLICY_INCLUDED : 0);
    ULONG64 Expected;
    ULONG i;

    ok(State->Count == Baseline->Count, "%s: %lu interrupts, expected %lu\n", Scenario->Name, State->Count, Baseline->Count);
    for (i = 0; i < State->Count && i < Baseline->Count; i++)
    {
        if (Scenario->Raw == RawOverride)
            Expected = Scenario->Override;
        else if (Scenario->Raw == RawSpread)
            Expected = ActiveProcessor(Active, i);
        else
            Expected = 0;
        ok(State->Message[i], "%s: interrupt %lu is not a message\n", Scenario->Name, i);
        ok(State->Alloc[i] == Baseline->Alloc[i], "%s: interrupt %lu token 0x%lx, expected 0x%lx\n",
           Scenario->Name, i, State->Alloc[i], Baseline->Alloc[i]);
        ok(State->Affinity[i] == Expected, "%s: interrupt %lu affinity 0x%I64x, expected 0x%I64x\n",
           Scenario->Name, i, State->Affinity[i], Expected);
    }

    ok(State->Raw.Present && State->Raw.Count == State->Count, "%s: %lu raw map interrupts, expected %lu\n",
       Scenario->Name, State->Raw.Count, State->Count);
    for (i = 0; i < State->Raw.Count && i < State->Count; i++)
    {
        ok(State->Raw.Flags[i] == Flags, "%s: raw map interrupt %lu flags 0x%x, expected 0x%x\n",
           Scenario->Name, i, State->Raw.Flags[i], Flags);
        ok(State->Raw.Affinity[i] == State->Affinity[i], "%s: raw map interrupt %lu affinity 0x%I64x, expected 0x%I64x\n",
           Scenario->Name, i, State->Raw.Affinity[i], State->Affinity[i]);
    }

    ok(State->Translated.Present && State->Translated.Count == State->Count,
       "%s: %lu translated map interrupts, expected %lu\n", Scenario->Name, State->Translated.Count, State->Count);
    if (Scenario->Trans == TransOneClose)
    {
        CheckOneClose(Scenario, &State->Translated, Active);
        return;
    }
    for (i = 0; i < State->Translated.Count; i++)
    {
        if (Scenario->Trans == TransOverride)
            Expected = Scenario->Override;
        else if (Scenario->Trans == TransSpread)
            Expected = ActiveProcessor(Active, i);
        else
            Expected = Active;
        ok(State->Translated.Affinity[i] == Expected, "%s: translated interrupt %lu affinity 0x%I64x, expected 0x%I64x\n",
           Scenario->Name, i, State->Translated.Affinity[i], Expected);
    }
}

static void CheckScenario(const SCENARIO *Scenario, const IRQ_STATE *Baseline, const IRQ_STATE *State,
                          BOOL PolicyKey, ULONG64 Active)
{
    EXPECT_KIND Kind = Scenario->Kind;
    EXPECT_TRANS Trans = Scenario->Trans;
    ULONG64 Expected;

    if (Scenario->Policy == 4 && Scenario->Raw == RawOverride && (Scenario->Override & ~Active))
    {
        Kind = KindLine;
        Trans = TransActive;
    }

    if (Kind == KindNotStarted)
    {
        ok(!State->Started, "%s: device started\n", Scenario->Name);
        ok_long(State->Problem, CM_PROB_NORMAL_CONFLICT);
        ok_long(State->AllocResult, CR_NO_MORE_LOG_CONF);
        return;
    }

    ok(State->Started, "%s: device not started, problem %lu\n", Scenario->Name, State->Problem);
    ok_long(State->AllocResult, CR_SUCCESS);
    if (Kind == KindMessage)
    {
        CheckMessages(Scenario, Baseline, State, PolicyKey, Active);
        return;
    }

    ok(State->Count == 1 && !State->Message[0], "%s: %lu interrupts, message %d\n",
       Scenario->Name, State->Count, State->Count ? State->Message[0] : -1);
    if (State->Count != 1)
        return;
    ok(State->Affinity[0] == LINE_RAW_AFFINITY, "%s: line affinity 0x%I64x\n", Scenario->Name, State->Affinity[0]);
    ok(State->Raw.Count == 1 && State->Raw.Flags[0] == 0, "%s: %lu raw map interrupts, flags 0x%x\n",
       Scenario->Name, State->Raw.Count, State->Raw.Count ? State->Raw.Flags[0] : 0);
    Expected = Trans == TransOverride ? Scenario->Override : Active;
    ok(State->Translated.Count == 1 && State->Translated.Affinity[0] == Expected,
       "%s: %lu translated map interrupts, affinity 0x%I64x, expected 0x%I64x\n", Scenario->Name,
       State->Translated.Count, State->Translated.Count ? State->Translated.Affinity[0] : 0, Expected);
}

START_TEST(InterruptAffinityPolicy)
{
    static const SCENARIO NoKey = { "no policy key", -1, 0, 0, 0, KindMessage, RawZero, TransActive };
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
    if (ProcessorCount(Active) < 4)
    {
        skip("Four processors required\n");
        return;
    }

    Error = RegDeleteKeyW(HKEY_LOCAL_MACHINE, PolicyPath);
    if (Error == ERROR_ACCESS_DENIED)
    {
        skip("The device hardware key is not writable\n");
        return;
    }
    ok(Error == ERROR_SUCCESS || Error == ERROR_FILE_NOT_FOUND, "RegDeleteKeyW returned %ld\n", Error);
    RestartDevice();
    ReadState(&Baseline);
    ok(Baseline.Started, "Test device not started\n");
    ok(Baseline.Count > 0, "Test device has no interrupt\n");
    if (!Baseline.Started || !Baseline.Count)
        return;
    for (i = 1; i < Baseline.Count; i++)
    {
        ok(Baseline.Alloc[i] == Baseline.Alloc[i - 1] - 1, "Token %lu is 0x%lx after 0x%lx\n",
           i, Baseline.Alloc[i], Baseline.Alloc[i - 1]);
    }
    CheckScenario(&NoKey, &Baseline, &Baseline, FALSE, Active);

    for (i = 0; i < _countof(Scenarios); i++)
    {
        ok_long(ApplyScenario(&Scenarios[i]), ERROR_SUCCESS);
        RestartDevice();
        ReadState(&State);
        CheckScenario(&Scenarios[i], &Baseline, &State, TRUE, Active);
    }

    ok_long(RegDeleteKeyW(HKEY_LOCAL_MACHINE, PolicyPath), ERROR_SUCCESS);
    RestartDevice();
    ReadState(&State);
    CheckScenario(&NoKey, &Baseline, &State, FALSE, Active);
}
