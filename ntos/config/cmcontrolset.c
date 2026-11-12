/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What the system reads out of its control set before it can boot
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#include "cmpriv.h"

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Follows a backslash-separated path of key names down from one cell. Used
 * before the namespace exists, when a path can only be walked in the hive.
 */
static
HCELL_INDEX
CmpWalkPath(
    _In_ PHHIVE Hive,
    _In_ HCELL_INDEX Parent,
    _In_ PCWSTR Path)
{
    UNICODE_STRING Component;
    PCM_KEY_NODE Node;
    HCELL_INDEX Current = Parent;
    HCELL_INDEX Next;
    PCWSTR Start;
    PCWSTR End;

    Start = Path;

    while (*Start != UNICODE_NULL)
    {
        while (*Start == OBJ_NAME_PATH_SEPARATOR) Start++;
        if (*Start == UNICODE_NULL) break;

        End = Start;
        while (*End != UNICODE_NULL && *End != OBJ_NAME_PATH_SEPARATOR) End++;

        Component.Buffer = (PWCHAR)Start;
        Component.Length = (USHORT)((End - Start) * sizeof(WCHAR));
        Component.MaximumLength = Component.Length;

        Node = (PCM_KEY_NODE)HvGetCell(Hive, Current);
        if (Node == NULL) return HCELL_NIL;

        Next = CmpFindSubKeyByName(Hive, Node, &Component);
        HvReleaseCell(Hive, Current);

        if (Next == HCELL_NIL) return HCELL_NIL;

        Current = Next;
        Start = End;
    }

    return Current;
}

static
PCELL_DATA
CmpReadValueOfKey(
    _In_ PHHIVE Hive,
    _In_ HCELL_INDEX KeyCell,
    _In_ PCWSTR ValueName,
    _Out_ PULONG Length,
    _Out_ PULONG Type)
{
    UNICODE_STRING Name;
    PCM_KEY_VALUE Value;
    PCM_KEY_NODE Node;
    HCELL_INDEX Cell;
    PCELL_DATA Data;

    *Length = 0;
    *Type = REG_NONE;

    RtlInitUnicodeString(&Name, ValueName);

    Node = (PCM_KEY_NODE)HvGetCell(Hive, KeyCell);
    if (Node == NULL) return NULL;

    Cell = CmpFindValueByName(Hive, Node, &Name);
    HvReleaseCell(Hive, KeyCell);

    if (Cell == HCELL_NIL) return NULL;

    Value = (PCM_KEY_VALUE)HvGetCell(Hive, Cell);
    if (Value == NULL) return NULL;

    *Type = Value->Type;
    Data = CmpValueToData(Hive, Value, Length);
    HvReleaseCell(Hive, Cell);
    return Data;
}

/* The language a string of hexadecimal digits names */
static
LANGID
CmpConvertLangId(
    _In_reads_bytes_(BufferLength) PWCHAR Buffer,
    _In_ ULONG BufferLength)
{
    ULONG Count = BufferLength / sizeof(WCHAR);
    ULONG Value = 0;
    ULONG Digit;
    ULONG i;

    for (i = 0; i < Count; i++)
    {
        WCHAR Character = Buffer[i];

        if (Character == UNICODE_NULL) break;

        if (Character >= L'0' && Character <= L'9')
        {
            Digit = (ULONG)(Character - L'0');
        }
        else if (Character >= L'A' && Character <= L'F')
        {
            Digit = (ULONG)(Character - L'A') + 10;
        }
        else if (Character >= L'a' && Character <= L'f')
        {
            Digit = (ULONG)(Character - L'a') + 10;
        }
        else
        {
            break;
        }

        Value = (Value * 16) + Digit;
    }

    return (LANGID)Value;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Reads the handful of settings the kernel needs before any of it is running,
 * straight out of the hive image the boot loader left in memory.
 */
CODE_SEG("INIT")
VOID
NTAPI
CmGetSystemControlValues(
    _In_ PVOID SystemHiveData,
    _In_ PCM_SYSTEM_CONTROL_VECTOR ControlVector)
{
    UNICODE_STRING CurrentName = RTL_CONSTANT_STRING(L"Current");
    PCM_SYSTEM_CONTROL_VECTOR Entry;
    HCELL_INDEX ControlSet;
    HCELL_INDEX Control;
    HCELL_INDEX KeyCell;
    BOOLEAN AutoSelect;
    PCELL_DATA Data;
    NTSTATUS Status;
    ULONG Capacity;
    ULONG Length;
    ULONG Type;
    HHIVE Hive;

    if (SystemHiveData == NULL || ControlVector == NULL) return;

    Status = HvInitialize(&Hive,
                          HINIT_FLAT,
                          0,
                          HFILE_TYPE_PRIMARY,
                          SystemHiveData,
                          CmpAllocate,
                          CmpFree,
                          NULL,
                          NULL,
                          0,
                          NULL);
    if (!NT_SUCCESS(Status)) return;

    ControlSet = CmpFindControlSet(&Hive,
                                   Hive.BaseBlock->RootCell,
                                   &CurrentName,
                                   &AutoSelect);
    if (ControlSet == HCELL_NIL) return;

    Control = CmpWalkPath(&Hive, ControlSet, L"Control");
    if (Control == HCELL_NIL) return;

    for (Entry = ControlVector; Entry->KeyPath != NULL; Entry++)
    {
        if (Entry->Buffer == NULL) continue;

        KeyCell = CmpWalkPath(&Hive, Control, Entry->KeyPath);
        if (KeyCell == HCELL_NIL) continue;

        Data = CmpReadValueOfKey(&Hive,
                                 KeyCell,
                                 Entry->ValueName,
                                 &Length,
                                 &Type);
        if (Data == NULL || Length == 0) continue;

        Capacity = (Entry->BufferLength != NULL) ? *Entry->BufferLength
                                                 : sizeof(ULONG);
        if (Length > Capacity) Length = Capacity;

        RtlCopyMemory(Entry->Buffer, Data, Length);

        if (Entry->BufferLength != NULL) *Entry->BufferLength = Length;
        if (Entry->Type != NULL) *Entry->Type = Type;
    }

    if (CmDefaultLanguageIdType == REG_SZ)
    {
        PsDefaultUILanguageId = CmpConvertLangId(CmDefaultLanguageId,
                                                 CmDefaultLanguageIdLength);
        PsDefaultThreadLocaleId = PsDefaultUILanguageId;
        PsDefaultSystemLocaleId = PsDefaultUILanguageId;
    }

    if (CmInstallUILanguageIdType == REG_SZ)
    {
        PsInstallUILanguageId = CmpConvertLangId(CmInstallUILanguageId,
                                                 CmInstallUILanguageIdLength);
    }
}

/* The name a control set goes by, built from its number */
static
VOID
CmpControlSetName(
    _In_ ULONG Number,
    _Out_writes_(16) PWCHAR Buffer,
    _Out_ PUNICODE_STRING Name)
{
    static const WCHAR Prefix[] = L"ControlSet";
    ULONG i;

    for (i = 0; Prefix[i] != UNICODE_NULL; i++) Buffer[i] = Prefix[i];

    Buffer[i + 0] = (WCHAR)(L'0' + ((Number / 100) % 10));
    Buffer[i + 1] = (WCHAR)(L'0' + ((Number / 10) % 10));
    Buffer[i + 2] = (WCHAR)(L'0' + (Number % 10));
    Buffer[i + 3] = UNICODE_NULL;

    RtlInitUnicodeString(Name, Buffer);
}

static
NTSTATUS
CmpOpenSelectKey(
    _Out_ PHANDLE KeyHandle)
{
    UNICODE_STRING SelectPath = RTL_CONSTANT_STRING(
        L"\\REGISTRY\\MACHINE\\SYSTEM\\Select");
    OBJECT_ATTRIBUTES ObjectAttributes;

    InitializeObjectAttributes(&ObjectAttributes,
                               &SelectPath,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    return ZwOpenKey(KeyHandle, KEY_READ | KEY_WRITE, &ObjectAttributes);
}

static
ULONG
CmpReadSelectNumber(
    _In_ HANDLE KeyHandle,
    _In_ PCWSTR ValueName)
{
    UCHAR Room[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION Partial;
    UNICODE_STRING Name;
    ULONG Returned = 0;

    RtlInitUnicodeString(&Name, ValueName);
    Partial = (PKEY_VALUE_PARTIAL_INFORMATION)Room;

    if (!NT_SUCCESS(ZwQueryValueKey(KeyHandle,
                                    &Name,
                                    KeyValuePartialInformation,
                                    Partial,
                                    sizeof(Room),
                                    &Returned)))
    {
        return 0;
    }

    if (Partial->Type != REG_DWORD) return 0;
    if (Partial->DataLength < sizeof(ULONG)) return 0;

    return *(PULONG)Partial->Data;
}

/*
 * Said once the boot is far enough along to be trusted. The control set the
 * system came up on is copied whole, so that a later boot that goes wrong has
 * something known to come back to, and the select key is pointed at the copy.
 */
NTSTATUS
NTAPI
CmpSaveBootControlSet(
    _In_ USHORT ControlSet)
{
    UNICODE_STRING LastKnownGoodName = RTL_CONSTANT_STRING(L"LastKnownGood");
    UNICODE_STRING FailedName = RTL_CONSTANT_STRING(L"Failed");
    UNICODE_STRING CurrentName;
    UNICODE_STRING TargetName;
    WCHAR CurrentBuffer[16];
    WCHAR TargetBuffer[16];
    PCM_KEY_NODE RootNode;
    HCELL_INDEX Source;
    HCELL_INDEX Clone;
    HCELL_INDEX Root;
    ULONG LastKnownGood;
    HANDLE KeyHandle;
    NTSTATUS Status;
    PCMHIVE SystemHive;
    PHHIVE Hive;
    ULONG Value;

    SystemHive = CmpMachineHiveList[CMP_SYSTEM_HIVE].CmHive;
    if (SystemHive == NULL) return STATUS_UNSUCCESSFUL;

    Hive = &SystemHive->Hive;
    if (Hive->BaseBlock == NULL) return STATUS_UNSUCCESSFUL;

    Root = Hive->BaseBlock->RootCell;

    Status = CmpOpenSelectKey(&KeyHandle);
    if (!NT_SUCCESS(Status)) return Status;

    LastKnownGood = CmpReadSelectNumber(KeyHandle, L"LastKnownGood");

    if (LastKnownGood == 0 || LastKnownGood == ControlSet)
    {
        LastKnownGood = (ControlSet == 1) ? 2 : 1;
    }

    CmpControlSetName(ControlSet, CurrentBuffer, &CurrentName);
    CmpControlSetName(LastKnownGood, TargetBuffer, &TargetName);

    CmpLockRegistryExclusive();

    RootNode = (PCM_KEY_NODE)HvGetCell(Hive, Root);

    if (RootNode == NULL)
    {
        CmpUnlockRegistry();
        ZwClose(KeyHandle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Source = CmpFindSubKeyByName(Hive, RootNode, &CurrentName);
    HvReleaseCell(Hive, Root);

    if (Source == HCELL_NIL)
    {
        CmpUnlockRegistry();
        ZwClose(KeyHandle);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    CmpLockHiveFlusherExclusive(SystemHive);
    Status = CmpCloneKeyTree(Hive, Source, Root, &TargetName, &Clone);
    CmpUnlockHiveFlusher(SystemHive);

    if (SystemHive->RootKcb != NULL)
    {
        CmpCleanUpSubKeyInfo(SystemHive->RootKcb);
    }

    CmpUnlockRegistry();

    if (!NT_SUCCESS(Status))
    {
        ZwClose(KeyHandle);
        return Status;
    }

    Value = LastKnownGood;
    Status = ZwSetValueKey(KeyHandle,
                           &LastKnownGoodName,
                           0,
                           REG_DWORD,
                           &Value,
                           sizeof(Value));

    Value = 0;
    ZwSetValueKey(KeyHandle, &FailedName, 0, REG_DWORD, &Value, sizeof(Value));

    ZwFlushKey(KeyHandle);
    ZwClose(KeyHandle);
    return Status;
}
