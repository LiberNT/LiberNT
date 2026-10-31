/*
 * PROJECT:     LiberNT DirectX Graphics Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Graphics driver block lists (GraphicsDrivers\BlockList\Kernel and Runtime)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "dxgkrnl_private.h"
#include <initguid.h>
#include <devpkey.h>

#define DXGKP_BLOCKLIST_KERNEL_KEY \
    L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\GraphicsDrivers\\BlockList\\Kernel"
#define DXGKP_BLOCKLIST_RUNTIME_KEY \
    L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\GraphicsDrivers\\BlockList\\Runtime"

#define DXGKP_BLOCKLIST_MAX_MATCHES 20
#define DXGKP_BLOCKLIST_MAX_STRINGS 30
#define DXGKP_BLOCKLIST_MAX_CHARS 64000
#define DXGKP_BLOCKLIST_MAX_VALUE_BYTES 0xFFFC
#define DXGKP_BLOCKLIST_VERSION_PARTS 4
#define DXGKP_BLOCKLIST_MODEL_PARTS 2
/* Windows 11 asks a miniport for DXGKQAITYPE_WDDMDEVICECAPS only above this DDI interface version. */
#define DXGKP_WDDMDEVICECAPS_INTERFACE_FLOOR 0xB000

typedef struct _DXGKP_BLOCKLIST_TARGET
{
    ULONG VendorId;
    ULONG DeviceId;
    PCWSTR DriverVersion;
    ULONG Model[DXGKP_BLOCKLIST_MODEL_PARTS];
} DXGKP_BLOCKLIST_TARGET;

typedef struct _DXGKP_BLOCKLIST_MATCHES
{
    ULONG Count;
    UNICODE_STRING Strings[DXGKP_BLOCKLIST_MAX_MATCHES];
} DXGKP_BLOCKLIST_MATCHES;

static NTSTATUS
DxgkpBlockListCompareHex(
    _In_ PCWSTR Text,
    _Inout_ PULONG Index,
    _In_ ULONG Value,
    _Out_ PBOOLEAN Match)
{
    ULONG Position = *Index;
    ULONG Current = 0;
    ULONG Digits = 0;
    BOOLEAN Found = FALSE;

    *Match = FALSE;
    for (;;)
    {
        WCHAR Char = Text[Position];
        ULONG Digit;

        if (Char == L'#' || Char == L',')
        {
            if (Current == Value)
                Found = TRUE;
            if (Char == L'#')
            {
                *Match = Found;
                *Index = Position + 1;
                return STATUS_SUCCESS;
            }
            Current = 0;
            Digits = 0;
        }
        else
        {
            if (Char >= L'0' && Char <= L'9')
                Digit = Char - L'0';
            else if ((Char | 0x20) >= L'a' && (Char | 0x20) <= L'f')
                Digit = (Char | 0x20) - L'a' + 10;
            else
                return STATUS_INVALID_PARAMETER;
            if (++Digits > 8)
                return STATUS_INVALID_PARAMETER;
            Current = Current * 16 + Digit;
        }
        Position++;
    }
}

static NTSTATUS
DxgkpBlockListParseInts(
    _In_ PCWSTR Text,
    _Inout_ PULONG Index,
    _In_ ULONG Count,
    _Out_writes_(Count) PULONG Values)
{
    ULONG Position = *Index;
    ULONG Part;

    for (Part = 0; Part < Count; Part++)
    {
        ULONG Digits = 0;

        Values[Part] = 0;
        while (Text[Position] >= L'0' && Text[Position] <= L'9')
        {
            if (++Digits > 4)
                return STATUS_INVALID_PARAMETER;
            Values[Part] = Values[Part] * 10 + (Text[Position] - L'0');
            Position++;
        }
        if (Part + 1 < Count)
        {
            if (Text[Position] != L'.')
                return STATUS_INVALID_PARAMETER;
            Position++;
        }
    }
    *Index = Position;
    return STATUS_SUCCESS;
}

static LONG
DxgkpBlockListCompareVersions(
    _In_reads_(Count) const ULONG *Left,
    _In_reads_(Count) const ULONG *Right,
    _In_ ULONG Count)
{
    ULONG Part;

    for (Part = 0; Part < Count; Part++)
    {
        if (Left[Part] != Right[Part])
            return Left[Part] < Right[Part] ? -1 : 1;
    }
    return 0;
}

static NTSTATUS
DxgkpBlockListCompareInts(
    _In_ PCWSTR Text,
    _Inout_ PULONG Index,
    _In_reads_(Count) const ULONG *Target,
    _In_ ULONG Count,
    _Out_ PBOOLEAN Match)
{
    ULONG Low[DXGKP_BLOCKLIST_VERSION_PARTS];
    ULONG High[DXGKP_BLOCKLIST_VERSION_PARTS];
    ULONG Position = *Index;
    BOOLEAN Found = FALSE;
    NTSTATUS Status;

    ASSERT(Count <= DXGKP_BLOCKLIST_VERSION_PARTS);
    *Match = FALSE;
    while (Text[Position] != L'#' && Text[Position] != UNICODE_NULL)
    {
        Status = DxgkpBlockListParseInts(Text, &Position, Count, Low);
        if (!NT_SUCCESS(Status))
            return Status;
        if (Text[Position] == L'-')
        {
            Position++;
            Status = DxgkpBlockListParseInts(Text, &Position, Count, High);
            if (!NT_SUCCESS(Status))
                return Status;
            if (DxgkpBlockListCompareVersions(Target, Low, Count) >= 0 &&
                DxgkpBlockListCompareVersions(Target, High, Count) <= 0)
            {
                Found = TRUE;
            }
        }
        else if (DxgkpBlockListCompareVersions(Target, Low, Count) == 0)
        {
            Found = TRUE;
        }
        if (Text[Position] != L',')
            break;
        Position++;
    }
    if (Text[Position] != L'#')
        return STATUS_INVALID_PARAMETER;
    *Match = Found;
    *Index = Position + 1;
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpBlockListField(
    _In_ PCWSTR Text,
    _Inout_ PULONG Index,
    _In_ PCWSTR Name,
    _Out_ PBOOLEAN Wildcard)
{
    SIZE_T NameLength = wcslen(Name);

    *Wildcard = FALSE;
    if (wcsncmp(Text + *Index, Name, NameLength) != 0)
        return STATUS_INVALID_PARAMETER;
    *Index += (ULONG)NameLength;
    if (Text[*Index] != L'*')
        return STATUS_SUCCESS;
    if (Text[*Index + 1] != L'#')
        return STATUS_INVALID_PARAMETER;
    *Index += 2;
    *Wildcard = TRUE;
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpBlockListMatchCondition(
    _In_ PCWSTR Condition,
    _In_ const DXGKP_BLOCKLIST_TARGET *Target,
    _Out_ PBOOLEAN Match)
{
    ULONG Version[DXGKP_BLOCKLIST_VERSION_PARTS];
    ULONG VersionIndex = 0;
    ULONG Index = 0;
    BOOLEAN Wildcard;
    BOOLEAN Found;
    NTSTATUS Status;

    *Match = FALSE;

    Status = DxgkpBlockListField(Condition, &Index, L"Vendor=", &Wildcard);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!Wildcard)
    {
        Status = DxgkpBlockListCompareHex(Condition, &Index, Target->VendorId, &Found);
        if (!NT_SUCCESS(Status) || !Found)
            return Status;
    }

    Status = DxgkpBlockListField(Condition, &Index, L"Device=", &Wildcard);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!Wildcard)
    {
        Status = DxgkpBlockListCompareHex(Condition, &Index, Target->DeviceId, &Found);
        if (!NT_SUCCESS(Status) || !Found)
            return Status;
    }

    Status = DxgkpBlockListField(Condition, &Index, L"Version=", &Wildcard);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!Wildcard)
    {
        Status = DxgkpBlockListParseInts(Target->DriverVersion, &VersionIndex, DXGKP_BLOCKLIST_VERSION_PARTS,
                                         Version);
        if (!NT_SUCCESS(Status))
            return Status;
        Status = DxgkpBlockListCompareInts(Condition, &Index, Version, DXGKP_BLOCKLIST_VERSION_PARTS, &Found);
        if (!NT_SUCCESS(Status) || !Found)
            return Status;
    }

    Status = DxgkpBlockListField(Condition, &Index, L"Model=", &Wildcard);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!Wildcard)
    {
        Status = DxgkpBlockListCompareInts(Condition, &Index, Target->Model, DXGKP_BLOCKLIST_MODEL_PARTS, &Found);
        if (!NT_SUCCESS(Status) || !Found)
            return Status;
    }

    *Match = TRUE;
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpBlockListReadValue(
    _In_ HANDLE Key,
    _In_ PUNICODE_STRING ValueName,
    _Out_ PUNICODE_STRING String)
{
    PKEY_VALUE_PARTIAL_INFORMATION Value;
    ULONG Needed = 0;
    ULONG Returned;
    ULONG DataLength;
    PWCHAR Buffer;
    NTSTATUS Status;

    RtlInitEmptyUnicodeString(String, NULL, 0);
    Status = ZwQueryValueKey(Key, ValueName, KeyValuePartialInformation, NULL, 0, &Needed);
    if (Status != STATUS_BUFFER_OVERFLOW && Status != STATUS_BUFFER_TOO_SMALL)
        return NT_SUCCESS(Status) ? STATUS_UNSUCCESSFUL : Status;

    Value = ExAllocatePoolZero(PagedPool, Needed, TAG_DXGK_ADAPTER);
    if (Value == NULL)
        return STATUS_NO_MEMORY;
    Status = ZwQueryValueKey(Key, ValueName, KeyValuePartialInformation, Value, Needed, &Returned);
    if (NT_SUCCESS(Status) && Value->Type != REG_SZ && Value->Type != REG_MULTI_SZ)
        Status = STATUS_OBJECT_TYPE_MISMATCH;
    if (NT_SUCCESS(Status) &&
        (Value->DataLength == 0 ||
         Value->DataLength > Needed - FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data)))
    {
        Status = STATUS_OBJECT_NAME_NOT_FOUND;
    }
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Value, TAG_DXGK_ADAPTER);
        return Status;
    }

    DataLength = min(Value->DataLength, (ULONG)DXGKP_BLOCKLIST_MAX_VALUE_BYTES);
    Buffer = ExAllocatePoolZero(PagedPool, DataLength + 2 * sizeof(WCHAR), TAG_DXGK_ADAPTER);
    if (Buffer == NULL)
    {
        ExFreePoolWithTag(Value, TAG_DXGK_ADAPTER);
        return STATUS_NO_MEMORY;
    }
    RtlCopyMemory(Buffer, Value->Data, DataLength);
    if (Value->Type == REG_SZ)
        DataLength += sizeof(WCHAR);
    ExFreePoolWithTag(Value, TAG_DXGK_ADAPTER);

    DataLength &= ~(ULONG)(sizeof(WCHAR) - 1);
    if (DataLength == 0)
        DataLength = sizeof(WCHAR);
    Buffer[DataLength / sizeof(WCHAR) - 1] = UNICODE_NULL;
    String->Buffer = Buffer;
    String->Length = (USHORT)DataLength;
    String->MaximumLength = (USHORT)(DataLength + sizeof(WCHAR));
    return STATUS_SUCCESS;
}

static BOOLEAN
DxgkpBlockListContains(
    _In_ PCUNICODE_STRING MultiString,
    _In_ PCWSTR Entry)
{
    ULONG Offset = 0;

    while (Offset * sizeof(WCHAR) < MultiString->Length)
    {
        PCWSTR Current = MultiString->Buffer + Offset;
        SIZE_T Length = wcslen(Current);

        if (Length == 0)
            break;
        if (wcscmp(Current, Entry) == 0)
            return TRUE;
        Offset += (ULONG)Length + 1;
    }
    return FALSE;
}

static NTSTATUS
DxgkpBlockListFindStrings(
    _In_ PCWSTR KeyPath,
    _In_ const DXGKP_BLOCKLIST_TARGET *Target,
    _Inout_ DXGKP_BLOCKLIST_MATCHES *Matches)
{
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING Path;
    KEY_FULL_INFORMATION KeyInfo;
    PKEY_VALUE_BASIC_INFORMATION ValueInfo;
    ULONG BufferSize;
    ULONG Returned;
    ULONG ValueIndex;
    HANDLE Key;
    NTSTATUS Status;

    PAGED_CODE();

    RtlInitUnicodeString(&Path, KeyPath);
    InitializeObjectAttributes(&Attributes, &Path, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    if (!NT_SUCCESS(ZwOpenKey(&Key, KEY_READ, &Attributes)))
        return STATUS_SUCCESS;

    Status = ZwQueryKey(Key, KeyFullInformation, &KeyInfo, sizeof(KeyInfo), &Returned);
    if (!NT_SUCCESS(Status) || KeyInfo.Values == 0)
    {
        ZwClose(Key);
        return Status;
    }

    BufferSize = FIELD_OFFSET(KEY_VALUE_BASIC_INFORMATION, Name) + KeyInfo.MaxValueNameLen + 2 * sizeof(WCHAR);
    ValueInfo = ExAllocatePoolZero(PagedPool, BufferSize, TAG_DXGK_ADAPTER);
    if (ValueInfo == NULL)
    {
        ZwClose(Key);
        return Status;
    }

    for (ValueIndex = 0; ValueIndex < KeyInfo.Values; ValueIndex++)
    {
        UNICODE_STRING ValueName;
        BOOLEAN Match;

        Status = ZwEnumerateValueKey(Key, ValueIndex, KeyValueBasicInformation, ValueInfo,
                                     BufferSize - sizeof(WCHAR), &Returned);
        if (!NT_SUCCESS(Status) || ValueInfo->Type != REG_MULTI_SZ)
            continue;
        ValueInfo->Name[ValueInfo->NameLength / sizeof(WCHAR)] = UNICODE_NULL;
        Status = DxgkpBlockListMatchCondition(ValueInfo->Name, Target, &Match);
        if (!NT_SUCCESS(Status) || !Match || Matches->Count >= DXGKP_BLOCKLIST_MAX_MATCHES)
            continue;
        ValueName.Buffer = ValueInfo->Name;
        ValueName.Length = (USHORT)ValueInfo->NameLength;
        ValueName.MaximumLength = (USHORT)ValueInfo->NameLength;
        Status = DxgkpBlockListReadValue(Key, &ValueName, &Matches->Strings[Matches->Count]);
        if (NT_SUCCESS(Status))
            Matches->Count++;
    }

    ExFreePoolWithTag(ValueInfo, TAG_DXGK_ADAPTER);
    ZwClose(Key);
    return Status;
}

static NTSTATUS
DxgkpBlockListMerge(
    _In_ const DXGKP_BLOCKLIST_MATCHES *Matches,
    _Outptr_ PWCHAR *MultiString,
    _Out_ PULONG Bytes)
{
    PCWSTR Unique[DXGKP_BLOCKLIST_MAX_STRINGS];
    ULONG UniqueLength[DXGKP_BLOCKLIST_MAX_STRINGS];
    ULONG UniqueCount = 0;
    ULONG TotalChars = 1;
    ULONG MatchIndex;
    ULONG Offset;
    ULONG Index;
    PWCHAR Buffer;

    for (MatchIndex = 0; MatchIndex < Matches->Count; MatchIndex++)
    {
        const UNICODE_STRING *String = &Matches->Strings[MatchIndex];

        Offset = 0;
        while (Offset * sizeof(WCHAR) < String->Length)
        {
            PCWSTR Current = String->Buffer + Offset;
            ULONG Length = (ULONG)wcslen(Current);
            BOOLEAN Duplicate = FALSE;

            if (Length == 0)
                break;
            for (Index = 0; Index < UniqueCount && !Duplicate; Index++)
                Duplicate = (wcscmp(Unique[Index], Current) == 0);
            if (!Duplicate && UniqueCount < DXGKP_BLOCKLIST_MAX_STRINGS)
            {
                Unique[UniqueCount] = Current;
                UniqueLength[UniqueCount] = Length + 1;
                TotalChars += Length + 1;
                UniqueCount++;
            }
            Offset += Length + 1;
        }
    }

    if (TotalChars >= DXGKP_BLOCKLIST_MAX_CHARS)
        return STATUS_NOT_SUPPORTED;
    Buffer = ExAllocatePoolZero(PagedPool, TotalChars * sizeof(WCHAR), TAG_DXGK_ADAPTER);
    if (Buffer == NULL)
        return STATUS_NO_MEMORY;

    Offset = 0;
    for (Index = 0; Index < UniqueCount; Index++)
    {
        RtlCopyMemory(Buffer + Offset, Unique[Index], UniqueLength[Index] * sizeof(WCHAR));
        Offset += UniqueLength[Index];
    }
    Buffer[Offset] = UNICODE_NULL;
    *MultiString = Buffer;
    *Bytes = TotalChars * sizeof(WCHAR);
    return STATUS_SUCCESS;
}

static VOID
DxgkpBlockListFreeMatches(
    _Inout_ DXGKP_BLOCKLIST_MATCHES *Matches)
{
    ULONG Index;

    for (Index = 0; Index < Matches->Count; Index++)
        ExFreePoolWithTag(Matches->Strings[Index].Buffer, TAG_DXGK_ADAPTER);
    Matches->Count = 0;
}

static NTSTATUS
DxgkpLoadBlockList(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ const DXGKP_BLOCKLIST_TARGET *Target,
    _In_ BOOLEAN Kernel)
{
    static const struct
    {
        PCWSTR Name;
        ULONG Flag;
    } KernelFlags[] =
    {
        { L"GPU_PV_ALL", DXGKP_BLOCKLIST_GPU_PV_ALL },
        { L"GPU_PV_HIGH_SECURITY", DXGKP_BLOCKLIST_GPU_PV_HIGH_SECURITY },
        { L"GPU_P_ALL", DXGKP_BLOCKLIST_GPU_P_ALL },
        { L"DISABLE_MPO", DXGKP_BLOCKLIST_DISABLE_MPO },
        { L"DISABLE_HWSCH", DXGKP_BLOCKLIST_DISABLE_HWSCH },
    };
    DXGKP_BLOCKLIST_MATCHES *Matches;
    PWCHAR MultiString;
    ULONG Bytes;
    ULONG MatchIndex;
    ULONG FlagIndex;
    NTSTATUS Status;

    Matches = ExAllocatePoolZero(PagedPool, sizeof(*Matches), TAG_DXGK_ADAPTER);
    if (Matches == NULL)
        return STATUS_NO_MEMORY;

    Status = DxgkpBlockListFindStrings(Kernel ? DXGKP_BLOCKLIST_KERNEL_KEY : DXGKP_BLOCKLIST_RUNTIME_KEY,
                                       Target, Matches);
    if (Matches->Count != 0)
    {
        if (Kernel)
        {
            for (MatchIndex = 0; MatchIndex < Matches->Count; MatchIndex++)
            {
                for (FlagIndex = 0; FlagIndex < RTL_NUMBER_OF(KernelFlags); FlagIndex++)
                {
                    if (DxgkpBlockListContains(&Matches->Strings[MatchIndex], KernelFlags[FlagIndex].Name))
                        Adapter->BlockListFlags |= KernelFlags[FlagIndex].Flag;
                }
            }
        }

        Status = DxgkpBlockListMerge(Matches, &MultiString, &Bytes);
        if (NT_SUCCESS(Status))
        {
            PWCHAR *List = Kernel ? &Adapter->KernelBlockList : &Adapter->RuntimeBlockList;

            if (*List != NULL)
                ExFreePoolWithTag(*List, TAG_DXGK_ADAPTER);
            *List = MultiString;
            if (Kernel)
                Adapter->KernelBlockListBytes = Bytes;
            else
                Adapter->RuntimeBlockListBytes = Bytes;
        }
        else if (Status == STATUS_NOT_SUPPORTED)
        {
            if (Kernel)
                Adapter->KernelBlockListTooLarge = TRUE;
            else
                Adapter->RuntimeBlockListTooLarge = TRUE;
        }
        DxgkpBlockListFreeMatches(Matches);
    }

    ExFreePoolWithTag(Matches, TAG_DXGK_ADAPTER);
    return Status;
}

VOID
DxgkpDestroyBlockList(
    _Inout_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter->KernelBlockList != NULL)
        ExFreePoolWithTag(Adapter->KernelBlockList, TAG_DXGK_ADAPTER);
    if (Adapter->RuntimeBlockList != NULL)
        ExFreePoolWithTag(Adapter->RuntimeBlockList, TAG_DXGK_ADAPTER);
    Adapter->KernelBlockList = NULL;
    Adapter->KernelBlockListBytes = 0;
    Adapter->RuntimeBlockList = NULL;
    Adapter->RuntimeBlockListBytes = 0;
}

static ULONG
DxgkpBlockListDriverModel(
    _In_ DXGK_WDDMVERSION WddmVersion)
{
    switch (WddmVersion)
    {
        case DXGKDDI_WDDMv3_2: return KMT_DRIVERVERSION_WDDM_3_2;
        case DXGKDDI_WDDMv3_1: return KMT_DRIVERVERSION_WDDM_3_1;
        case DXGKDDI_WDDMv3_0: return KMT_DRIVERVERSION_WDDM_3_0;
        case DXGKDDI_WDDMv2_9: return KMT_DRIVERVERSION_WDDM_2_9;
        case DXGKDDI_WDDMv2_8: return KMT_DRIVERVERSION_WDDM_2_8;
        case DXGKDDI_WDDMv2_7: return KMT_DRIVERVERSION_WDDM_2_7;
        case DXGKDDI_WDDMv2_6: return KMT_DRIVERVERSION_WDDM_2_6;
        case DXGKDDI_WDDMv2_5: return KMT_DRIVERVERSION_WDDM_2_5;
        case DXGKDDI_WDDMv2_4: return KMT_DRIVERVERSION_WDDM_2_4;
        case DXGKDDI_WDDMv2_3: return KMT_DRIVERVERSION_WDDM_2_3;
        case DXGKDDI_WDDMv2_2: return KMT_DRIVERVERSION_WDDM_2_2;
        case DXGKDDI_WDDMv2_1: return KMT_DRIVERVERSION_WDDM_2_1;
        case DXGKDDI_WDDMv2: return KMT_DRIVERVERSION_WDDM_2_0;
        case DXGKDDI_WDDMv1_3: return KMT_DRIVERVERSION_WDDM_1_3;
        case DXGKDDI_WDDMv1_2: return KMT_DRIVERVERSION_WDDM_1_2;
        default: return KMT_DRIVERVERSION_WDDM_1_1;
    }
}

static PWSTR
DxgkpBlockListQueryDriverVersion(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    DEVPROPTYPE Type;
    ULONG Required = 0;
    PWSTR Version;
    NTSTATUS Status;

    Status = IoGetDevicePropertyData(PhysicalDeviceObject, &DEVPKEY_Device_DriverVersion, LOCALE_NEUTRAL, 0, 0,
                                     NULL, &Required, &Type);
    if (Status != STATUS_BUFFER_TOO_SMALL || Type != DEVPROP_TYPE_STRING || Required < sizeof(WCHAR))
        return NULL;
    Version = ExAllocatePoolZero(PagedPool, Required + sizeof(WCHAR), TAG_DXGK_ADAPTER);
    if (Version == NULL)
        return NULL;
    Status = IoGetDevicePropertyData(PhysicalDeviceObject, &DEVPKEY_Device_DriverVersion, LOCALE_NEUTRAL, 0,
                                     Required, Version, &Required, &Type);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Version, TAG_DXGK_ADAPTER);
        return NULL;
    }
    return Version;
}

NTSTATUS
DxgkpInitializeBlockList(
    _Inout_ PDXGKRNL_ADAPTER Adapter,
    _In_ DXGK_WDDMVERSION WddmVersion)
{
    DXGKP_BLOCKLIST_TARGET Target;
    PCI_COMMON_HEADER Header;
    PWSTR DriverVersion;
    ULONG Model;
    NTSTATUS Status;

    PAGED_CODE();

    if (Adapter->BlockListInitialized)
        return STATUS_SUCCESS;

    RtlZeroMemory(&Target, sizeof(Target));
    /* TODO: vendor and device IDs of an adapter without a PCI bus interface (Windows evaluates ACPI methods) */
    if (Adapter->PciBusInterfaceValid && Adapter->PciBusInterface.GetBusData != NULL)
    {
        RtlZeroMemory(&Header, sizeof(Header));
        if (Adapter->PciBusInterface.GetBusData(Adapter->PciBusInterface.Context, PCI_WHICHSPACE_CONFIG, &Header, 0,
                                                FIELD_OFFSET(PCI_COMMON_HEADER, Command)) ==
            FIELD_OFFSET(PCI_COMMON_HEADER, Command))
        {
            Target.VendorId = Header.VendorID;
            Target.DeviceId = Header.DeviceID;
        }
    }
    DriverVersion = DxgkpBlockListQueryDriverVersion(Adapter->PhysicalDeviceObject);
    Target.DriverVersion = DriverVersion != NULL ? DriverVersion : L"";
    Model = DxgkpBlockListDriverModel(WddmVersion);
    Target.Model[0] = Model / 1000;
    Target.Model[1] = (Model % 1000) / 100;

    Status = DxgkpLoadBlockList(Adapter, &Target, TRUE);
    if (NT_SUCCESS(Status))
        Status = DxgkpLoadBlockList(Adapter, &Target, FALSE);
    if (NT_SUCCESS(Status))
        Adapter->BlockListInitialized = TRUE;

    DXGKRNL_TRACE("DxgkpInitializeBlockList: vendor 0x%04lx device 0x%04lx version %S model %lu.%lu "
                  "status 0x%08lx flags 0x%lx kernel %lu runtime %lu\n",
                  Target.VendorId, Target.DeviceId, Target.DriverVersion, Target.Model[0], Target.Model[1], Status,
                  Adapter->BlockListFlags, Adapter->KernelBlockListBytes, Adapter->RuntimeBlockListBytes);
    if (DriverVersion != NULL)
        ExFreePoolWithTag(DriverVersion, TAG_DXGK_ADAPTER);
    return Status;
}

VOID
DxgkpQueryDriverWddmDeviceCaps(
    _Inout_ PDXGKRNL_ADAPTER Adapter)
{
    DXGKARG_QUERYADAPTERINFO QueryArgs;
    DXGK_WDDMDEVICECAPSIN CapsIn;
    DXGK_WDDMDEVICECAPS Caps;
    NTSTATUS Status;

    PAGED_CODE();

    if (Adapter->MiniportContext->InitData.s.Version <= DXGKP_WDDMDEVICECAPS_INTERFACE_FLOOR ||
        DXGK_CB_FULL(Adapter, DxgkDdiQueryAdapterInfo) == NULL ||
        !DxgkAcquireKmdCall(Adapter))
    {
        return;
    }

    RtlZeroMemory(&QueryArgs, sizeof(QueryArgs));
    RtlZeroMemory(&Caps, sizeof(Caps));
    CapsIn.DxgkrnlInterfaceVersion = DXGKDDI_INTERFACE_VERSION;
    QueryArgs.Type = DXGKQAITYPE_WDDMDEVICECAPS;
    QueryArgs.pInputData = &CapsIn;
    QueryArgs.InputDataSize = sizeof(CapsIn);
    QueryArgs.pOutputData = &Caps;
    QueryArgs.OutputDataSize = sizeof(Caps);
    Status = DXGK_CB_FULL(Adapter, DxgkDdiQueryAdapterInfo)(Adapter->MiniportDeviceContext, &QueryArgs);
    DxgkReleaseKmdCall(Adapter);

    DXGKRNL_TRACE("DxgkpQueryDriverWddmDeviceCaps: status 0x%08lx WDDMVersion 0x%x\n", Status, Caps.WDDMVersion);
    if (!NT_SUCCESS(Status))
        return;
    Adapter->DriverWddmVersion = Caps.WDDMVersion;
    if (!NT_SUCCESS(DxgkpInitializeBlockList(Adapter, Caps.WDDMVersion)))
        DxgkpDestroyBlockList(Adapter);
}

NTSTATUS
DxgkpReadBlockList(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Inout_updates_bytes_(BufferSize) D3DKMT_BLOCKLIST_INFO *Info,
    _In_ ULONG BufferSize,
    _In_ BOOLEAN Kernel)
{
    PCWSTR List = Kernel ? Adapter->KernelBlockList : Adapter->RuntimeBlockList;
    ULONG ListBytes = Kernel ? Adapter->KernelBlockListBytes : Adapter->RuntimeBlockListBytes;
    BOOLEAN TooLarge = Kernel ? Adapter->KernelBlockListTooLarge : Adapter->RuntimeBlockListTooLarge;
    ULONG Requested = Info->Size;

    if (TooLarge)
    {
        Info->Size = 0;
        return STATUS_NOT_SUPPORTED;
    }
    if (Requested > MAXULONG - FIELD_OFFSET(D3DKMT_BLOCKLIST_INFO, BlockList) ||
        Requested + FIELD_OFFSET(D3DKMT_BLOCKLIST_INFO, BlockList) > BufferSize ||
        (Requested != 0 && ListBytes > Requested))
    {
        Info->Size = 0;
        return STATUS_BUFFER_TOO_SMALL;
    }
    if (Requested != 0 && ListBytes != 0)
        RtlCopyMemory(Info->BlockList, List, ListBytes);
    Info->Size = ListBytes;
    return STATUS_SUCCESS;
}
