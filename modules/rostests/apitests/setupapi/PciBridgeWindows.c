/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the I/O and memory a PCI device is given lie inside the windows of the bridge above it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>

#define MAX_RANGES 16

typedef struct _RANGES
{
    ULONG Count;
    DWORDLONG Start[MAX_RANGES];
    DWORDLONG End[MAX_RANGES];
} RANGES, *PRANGES;

static VOID
GetAllocated(DEVINST Node, RESOURCEID Type, PRANGES Ranges)
{
    LOG_CONF LogConf;
    RES_DES ResDes, Next;
    ULONG Size;
    BYTE Data[512];

    Ranges->Count = 0;
    if (CM_Get_First_Log_Conf(&LogConf, Node, ALLOC_LOG_CONF) != CR_SUCCESS)
        return;

    if (CM_Get_Next_Res_Des(&ResDes, LogConf, Type, NULL, 0) != CR_SUCCESS)
    {
        CM_Free_Log_Conf_Handle(LogConf);
        return;
    }

    for (;;)
    {
        if (CM_Get_Res_Des_Data_Size(&Size, ResDes, 0) == CR_SUCCESS && Size <= sizeof(Data) &&
            CM_Get_Res_Des_Data(ResDes, Data, Size, 0) == CR_SUCCESS && Ranges->Count < MAX_RANGES)
        {
            if (Type == ResType_IO)
            {
                PIO_DES Io = (PIO_DES)Data;

                Ranges->Start[Ranges->Count] = Io->IOD_Alloc_Base;
                Ranges->End[Ranges->Count] = Io->IOD_Alloc_End;
            }
            else
            {
                PMEM_DES Mem = (PMEM_DES)Data;

                Ranges->Start[Ranges->Count] = Mem->MD_Alloc_Base;
                Ranges->End[Ranges->Count] = Mem->MD_Alloc_End;
            }
            Ranges->Count++;
        }

        if (CM_Get_Next_Res_Des(&Next, ResDes, Type, NULL, 0) != CR_SUCCESS)
            break;
        CM_Free_Res_Des_Handle(ResDes);
        ResDes = Next;
    }

    CM_Free_Res_Des_Handle(ResDes);
    CM_Free_Log_Conf_Handle(LogConf);
}

static BOOL
Inside(const RANGES *Windows, DWORDLONG Start, DWORDLONG End)
{
    ULONG Index;

    for (Index = 0; Index < Windows->Count; Index++)
    {
        if (Windows->Start[Index] <= Start && End <= Windows->End[Index])
            return TRUE;
    }
    return FALSE;
}

static VOID
CheckType(const WCHAR *Id, const WCHAR *Parent, DEVINST Node, DEVINST ParentNode, RESOURCEID Type, const WCHAR *Kind,
          PULONG Checked)
{
    RANGES Own, Windows;
    ULONG Index;

    GetAllocated(Node, Type, &Own);
    if (Own.Count == 0)
        return;
    GetAllocated(ParentNode, Type, &Windows);

    for (Index = 0; Index < Own.Count; Index++)
    {
        ok(Inside(&Windows, Own.Start[Index], Own.End[Index]),
           "%ls: %ls 0x%I64x-0x%I64x is outside every window of %ls\n",
           Id, Kind, Own.Start[Index], Own.End[Index], Parent);
        (*Checked)++;
    }
}

START_TEST(PciBridgeWindows)
{
    WCHAR Id[MAX_DEVICE_ID_LEN], Parent[MAX_DEVICE_ID_LEN];
    SP_DEVINFO_DATA Data;
    DEVINST ParentNode;
    ULONG Status, Problem, Checked = 0;
    HDEVINFO Set;
    DWORD Index;

    Set = SetupDiGetClassDevsW(NULL, L"PCI", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiGetClassDevsW failed with %lu\n", GetLastError());
    if (Set == INVALID_HANDLE_VALUE)
        return;

    Data.cbSize = sizeof(Data);
    for (Index = 0; SetupDiEnumDeviceInfo(Set, Index, &Data); Index++)
    {
        if (!SetupDiGetDeviceInstanceIdW(Set, &Data, Id, ARRAYSIZE(Id), NULL) ||
            CM_Get_Parent(&ParentNode, Data.DevInst, 0) != CR_SUCCESS ||
            CM_Get_Device_IDW(ParentNode, Parent, ARRAYSIZE(Parent), 0) != CR_SUCCESS ||
            _wcsnicmp(Parent, L"PCI\\", 4) != 0)
        {
            continue;
        }

        Status = Problem = 0;
        if (CM_Get_DevNode_Status(&Status, &Problem, ParentNode, 0) == CR_SUCCESS)
            ok(Status & DN_STARTED, "%ls: not started (problem %lu)\n", Parent, Problem);

        CheckType(Id, Parent, Data.DevInst, ParentNode, ResType_IO, L"I/O", &Checked);
        CheckType(Id, Parent, Data.DevInst, ParentNode, ResType_Mem, L"memory", &Checked);
    }

    trace("%lu ranges behind PCI bridges checked\n", Checked);
    if (Checked == 0)
        skip("No PCI device with resources behind a PCI bridge\n");
    SetupDiDestroyDeviceInfoList(Set);
}
